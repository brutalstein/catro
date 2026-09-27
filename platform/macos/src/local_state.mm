#include <catro/platform/macos/local_state.hpp>

#include <catro/community/state_codec.hpp>

#import <Foundation/Foundation.h>
#import <Security/Security.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <pthread.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <variant>

namespace catro::platform::macos {
namespace {

using community::CodecError;
using community::LocalState;

class FileDescriptor {
public:
    explicit FileDescriptor(int value = -1) noexcept : value_(value) {}
    ~FileDescriptor() {
        if (value_ >= 0) {
            close(value_);
        }
    }

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    [[nodiscard]] int get() const noexcept { return value_; }
    [[nodiscard]] explicit operator bool() const noexcept { return value_ >= 0; }

private:
    int value_ = -1;
};

LocalStateError native_error(LocalStateErrorCode code, std::string detail,
                             std::int32_t native = errno) {
    return {code, std::move(detail), native};
}

std::string bounded_codec_detail(const CodecError& error) {
    constexpr std::size_t kMaxDetail = 160;
    return error.detail.substr(0, std::min(error.detail.size(), kMaxDetail));
}

std::optional<LocalStateError> ensure_parent_directory(const std::filesystem::path& path) {
    std::error_code filesystem_error;
    const auto parent = path.parent_path();
    if (parent.empty()) {
        return std::nullopt;
    }
    std::filesystem::create_directories(parent, filesystem_error);
    if (!filesystem_error) {
        return std::nullopt;
    }
    return LocalStateError{LocalStateErrorCode::path_failure,
                           "failed to create local state directory",
                           static_cast<std::int32_t>(filesystem_error.value())};
}

std::variant<std::string, LocalStateError> read_file(const std::filesystem::path& path) {
    FileDescriptor file(open(path.c_str(), O_RDONLY | O_CLOEXEC));
    if (!file) {
        if (errno == ENOENT) {
            return LocalStateError{LocalStateErrorCode::not_found,
                                   "local state does not exist", errno};
        }
        return native_error(LocalStateErrorCode::read_failure, "failed to open local state");
    }

    struct stat metadata {};
    if (fstat(file.get(), &metadata) != 0) {
        return native_error(LocalStateErrorCode::read_failure, "failed to inspect local state");
    }
    if (metadata.st_size < 0 ||
        static_cast<std::uint64_t>(metadata.st_size) > community::kMaxStateBytes) {
        return LocalStateError{LocalStateErrorCode::invalid_state,
                               "local state exceeds the bounded file size", 0};
    }

    std::string bytes(static_cast<std::size_t>(metadata.st_size), '\0');
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto result = read(file.get(), bytes.data() + offset, bytes.size() - offset);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return native_error(LocalStateErrorCode::read_failure, "failed to read local state");
        }
        if (result == 0) {
            return LocalStateError{LocalStateErrorCode::read_failure,
                                   "local state ended before its advertised size", 0};
        }
        offset += static_cast<std::size_t>(result);
    }
    return bytes;
}

std::filesystem::path staging_path(const std::filesystem::path& path) {
    std::uint64_t thread_id = 0;
    (void)pthread_threadid_np(nullptr, &thread_id);
    auto staged = path;
    staged += ".tmp.";
    staged += std::to_string(getpid());
    staged += ".";
    staged += std::to_string(thread_id);
    return staged;
}

std::optional<LocalStateError> write_file_durable(const std::filesystem::path& path,
                                                   std::string_view bytes) {
    FileDescriptor file(open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600));
    if (!file) {
        return native_error(LocalStateErrorCode::write_failure, "failed to create state staging file");
    }

    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto result = write(file.get(), bytes.data() + offset, bytes.size() - offset);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return native_error(LocalStateErrorCode::write_failure, "failed to write local state");
        }
        if (result == 0) {
            return LocalStateError{LocalStateErrorCode::write_failure,
                                   "local state write made no progress", 0};
        }
        offset += static_cast<std::size_t>(result);
    }

    if (fsync(file.get()) != 0) {
        return native_error(LocalStateErrorCode::write_failure, "failed to flush local state");
    }
    return std::nullopt;
}

class FileStateLock {
public:
    explicit FileStateLock(const std::filesystem::path& state_path) : path_(state_path) {
        path_ += ".lock";
    }

    [[nodiscard]] std::optional<LocalStateError> acquire() noexcept {
        file_ = std::make_unique<FileDescriptor>(
            open(path_.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600));
        if (!*file_) {
            return native_error(LocalStateErrorCode::lock_failure,
                                "failed to open local state lock");
        }

        constexpr int kAttempts = 400;
        for (int attempt = 0; attempt < kAttempts; ++attempt) {
            if (flock(file_->get(), LOCK_EX | LOCK_NB) == 0) {
                acquired_ = true;
                return std::nullopt;
            }
            if (errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR) {
                return native_error(LocalStateErrorCode::lock_failure,
                                    "failed to acquire local state lock");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        return LocalStateError{LocalStateErrorCode::lock_failure,
                               "timed out waiting for local state lock", ETIMEDOUT};
    }

    ~FileStateLock() {
        if (acquired_ && file_) {
            (void)flock(file_->get(), LOCK_UN);
        }
    }

private:
    std::filesystem::path path_;
    std::unique_ptr<FileDescriptor> file_;
    bool acquired_ = false;
};

} // namespace

bool SystemEntropy::fill(std::span<std::byte> destination) noexcept {
    if (destination.empty()) {
        return true;
    }
    return SecRandomCopyBytes(kSecRandomDefault, destination.size(),
                              reinterpret_cast<std::uint8_t*>(destination.data())) == errSecSuccess;
}

std::variant<std::filesystem::path, LocalStateError> default_local_state_path() {
    @autoreleasepool {
        NSArray<NSURL*>* urls =
            [[NSFileManager defaultManager] URLsForDirectory:NSApplicationSupportDirectory
                                                   inDomains:NSUserDomainMask];
        NSURL* root = urls.firstObject;
        if (root == nil || root.fileSystemRepresentation == nullptr) {
            return LocalStateError{LocalStateErrorCode::path_failure,
                                   "failed to resolve Application Support", 0};
        }
        return std::filesystem::path(root.fileSystemRepresentation) / "Catro" / "state-v1.json";
    }
}

std::variant<LocalState, LocalStateError> load_local_state(const std::filesystem::path& path) {
    const auto bytes = read_file(path);
    if (const auto* error = std::get_if<LocalStateError>(&bytes)) {
        return *error;
    }

    const auto decoded = community::decode_local_state(std::get<std::string>(bytes));
    if (const auto* error = std::get_if<CodecError>(&decoded)) {
        return LocalStateError{LocalStateErrorCode::invalid_state,
                               bounded_codec_detail(*error), 0};
    }
    return std::get<LocalState>(decoded);
}

std::optional<LocalStateError> save_local_state_atomic(const std::filesystem::path& path,
                                                       const LocalState& state) {
    const auto encoded = community::encode_local_state(state);
    if (const auto* error = std::get_if<CodecError>(&encoded)) {
        return LocalStateError{LocalStateErrorCode::invalid_state,
                               bounded_codec_detail(*error), 0};
    }
    if (const auto error = ensure_parent_directory(path)) {
        return error;
    }

    const auto staged = staging_path(path);
    std::error_code ignored;
    std::filesystem::remove(staged, ignored);
    if (const auto error = write_file_durable(staged, std::get<std::string>(encoded))) {
        std::filesystem::remove(staged, ignored);
        return error;
    }

    if (rename(staged.c_str(), path.c_str()) != 0) {
        const auto error = native_error(LocalStateErrorCode::commit_failure,
                                        "failed to atomically commit local state");
        std::filesystem::remove(staged, ignored);
        return error;
    }
    return std::nullopt;
}

std::variant<LocalState, LocalStateError> load_or_create_local_state(
    const std::filesystem::path& path, community::EntropySource& entropy) {
    // Normal startup is a single bounded read with no lock. Atomic replacement means a reader
    // always observes either the previous complete file or the next complete file.
    auto existing = load_local_state(path);
    if (std::holds_alternative<LocalState>(existing)) {
        return std::get<LocalState>(existing);
    }
    if (std::get<LocalStateError>(existing).code != LocalStateErrorCode::not_found) {
        return std::get<LocalStateError>(existing);
    }

    if (const auto error = ensure_parent_directory(path)) {
        return *error;
    }

    // Only first-run creation takes the cross-process lock. Re-read after acquiring it because
    // another process may have committed the identity while this process was waiting.
    FileStateLock lock(path);
    if (const auto error = lock.acquire()) {
        return *error;
    }

    existing = load_local_state(path);
    if (std::holds_alternative<LocalState>(existing)) {
        return std::get<LocalState>(existing);
    }
    const auto& load_error = std::get<LocalStateError>(existing);
    if (load_error.code != LocalStateErrorCode::not_found) {
        // Corrupt or incompatible state is never silently replaced because that would change the
        // user's stable identity and personal-server id.
        return load_error;
    }

    const auto created = community::bootstrap_personal_state(entropy);
    if (const auto* error = std::get_if<community::StateError>(&created)) {
        return LocalStateError{LocalStateErrorCode::entropy_failure, error->detail, 0};
    }

    const auto& state = std::get<LocalState>(created);
    if (const auto error = save_local_state_atomic(path, state)) {
        return *error;
    }
    return state;
}

std::variant<LocalState, LocalStateError> load_or_create_default_local_state() {
    const auto path = default_local_state_path();
    if (const auto* error = std::get_if<LocalStateError>(&path)) {
        return *error;
    }

    SystemEntropy entropy;
    return load_or_create_local_state(std::get<std::filesystem::path>(path), entropy);
}

} // namespace catro::platform::macos

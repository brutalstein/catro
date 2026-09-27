#include <catro/platform/windows/local_state.hpp>

#include <catro/community/state_codec.hpp>

#include <bcrypt.h>
#include <shlobj.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#include <variant>

namespace catro::platform::windows {
namespace {

using community::CodecError;
using community::LocalState;

struct HandleCloser {
    void operator()(void* value) const noexcept {
        if (value != nullptr && value != INVALID_HANDLE_VALUE) {
            CloseHandle(static_cast<HANDLE>(value));
        }
    }
};

using UniqueHandle = std::unique_ptr<void, HandleCloser>;

LocalStateError native_error(LocalStateErrorCode code, std::string detail,
                             std::uint32_t native = GetLastError()) {
    return {code, std::move(detail), native};
}

std::string bounded_codec_detail(const CodecError& error) {
    constexpr std::size_t kMaxDetail = 160;
    return error.detail.substr(0, std::min(error.detail.size(), kMaxDetail));
}

std::variant<std::string, LocalStateError> read_file(const std::filesystem::path& path) {
    UniqueHandle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file || file.get() == INVALID_HANDLE_VALUE) {
        const auto code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
            return LocalStateError{LocalStateErrorCode::not_found, "local state does not exist", code};
        }
        return native_error(LocalStateErrorCode::read_failure, "failed to open local state", code);
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.get(), &size)) {
        return native_error(LocalStateErrorCode::read_failure, "failed to inspect local state");
    }
    if (size.QuadPart < 0 || static_cast<std::uint64_t>(size.QuadPart) > community::kMaxStateBytes) {
        return LocalStateError{LocalStateErrorCode::invalid_state,
                               "local state exceeds the bounded file size", 0};
    }

    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    if (!bytes.empty() &&
        (!ReadFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) ||
         read != static_cast<DWORD>(bytes.size()))) {
        return native_error(LocalStateErrorCode::read_failure, "failed to read complete local state");
    }
    return bytes;
}

std::optional<LocalStateError> write_file_durable(const std::filesystem::path& path,
                                                   std::string_view bytes) {
    UniqueHandle file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr));
    if (!file || file.get() == INVALID_HANDLE_VALUE) {
        return native_error(LocalStateErrorCode::write_failure, "failed to create state staging file");
    }

    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const auto chunk = static_cast<DWORD>(
            std::min<std::size_t>(remaining, std::numeric_limits<DWORD>::max()));
        DWORD written = 0;
        if (!WriteFile(file.get(), bytes.data() + offset, chunk, &written, nullptr) || written == 0) {
            return native_error(LocalStateErrorCode::write_failure, "failed to write local state");
        }
        offset += written;
    }
    if (!FlushFileBuffers(file.get())) {
        return native_error(LocalStateErrorCode::write_failure, "failed to flush local state");
    }
    return std::nullopt;
}

std::filesystem::path staging_path(const std::filesystem::path& path) {
    auto staged = path;
    staged += L".tmp.";
    staged += std::to_wstring(GetCurrentProcessId());
    staged += L".";
    staged += std::to_wstring(GetCurrentThreadId());
    return staged;
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
                           static_cast<std::uint32_t>(filesystem_error.value())};
}

class FileStateLock {
public:
    explicit FileStateLock(const std::filesystem::path& state_path) : path_(state_path) {
        path_ += L".lock";
    }

    [[nodiscard]] std::optional<LocalStateError> acquire() noexcept {
        constexpr int kAttempts = 400;
        constexpr DWORD kSleepMs = 25;

        for (int attempt = 0; attempt < kAttempts; ++attempt) {
            HANDLE raw = CreateFileW(path_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                     OPEN_ALWAYS,
                                     FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
            if (raw != INVALID_HANDLE_VALUE) {
                handle_.reset(raw);
                return std::nullopt;
            }

            const auto code = GetLastError();
            if (code != ERROR_SHARING_VIOLATION && code != ERROR_LOCK_VIOLATION) {
                return native_error(LocalStateErrorCode::lock_failure,
                                    "failed to acquire local state file lock", code);
            }
            Sleep(kSleepMs);
        }
        return LocalStateError{LocalStateErrorCode::lock_failure,
                               "timed out waiting for local state file lock", ERROR_TIMEOUT};
    }

private:
    std::filesystem::path path_;
    UniqueHandle handle_;
};

} // namespace

bool SystemEntropy::fill(std::span<std::byte> destination) noexcept {
    if (destination.empty()) {
        return true;
    }
    if (destination.size() > std::numeric_limits<ULONG>::max()) {
        return false;
    }
    return BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(destination.data()),
                           static_cast<ULONG>(destination.size()),
                           BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}

std::variant<std::filesystem::path, LocalStateError> default_local_state_path() {
    PWSTR raw = nullptr;
    const auto result = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &raw);
    if (FAILED(result) || raw == nullptr) {
        if (raw != nullptr) {
            CoTaskMemFree(raw);
        }
        return LocalStateError{LocalStateErrorCode::path_failure,
                               "failed to resolve LocalAppData", static_cast<std::uint32_t>(result)};
    }

    const std::filesystem::path root(raw);
    CoTaskMemFree(raw);
    return root / L"Catro" / L"state-v1.json";
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

    std::error_code filesystem_error;
    const auto staged = staging_path(path);
    std::filesystem::remove(staged, filesystem_error);
    filesystem_error.clear();

    if (const auto error = write_file_durable(staged, std::get<std::string>(encoded))) {
        std::filesystem::remove(staged, filesystem_error);
        return error;
    }

    if (!MoveFileExW(staged.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const auto error = native_error(LocalStateErrorCode::commit_failure,
                                        "failed to atomically commit local state");
        std::filesystem::remove(staged, filesystem_error);
        return error;
    }
    return std::nullopt;
}

std::variant<LocalState, LocalStateError> load_or_create_local_state(
    const std::filesystem::path& path, community::EntropySource& entropy) {
    if (const auto error = ensure_parent_directory(path)) {
        return *error;
    }

    FileStateLock lock(path);
    if (const auto error = lock.acquire()) {
        return *error;
    }

    const auto existing = load_local_state(path);
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

} // namespace catro::platform::windows

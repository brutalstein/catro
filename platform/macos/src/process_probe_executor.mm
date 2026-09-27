#include <catro/platform/macos/capability_service.hpp>

#include <catro/reporting/canonical_json.hpp>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>

extern char** environ;

namespace catro::platform::macos {
namespace {

namespace caps = catro::capabilities;
namespace report = catro::reporting;

// Longest wait between exit checks: a descendant may keep the pipe open after the helper exits.
constexpr std::chrono::milliseconds kPollSlice{20};

class UniqueFd {
public:
    UniqueFd() = default;
    explicit UniqueFd(int fd) : fd_(fd) {}
    ~UniqueFd() { reset(); }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            reset(std::exchange(other.fd_, -1));
        }
        return *this;
    }

    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] explicit operator bool() const noexcept { return fd_ >= 0; }

    void reset(int replacement = -1) noexcept {
        if (fd_ >= 0) {
            close(fd_);
        }
        fd_ = replacement;
    }

private:
    int fd_ = -1;
};

caps::ProbeFragment failure(const caps::ProbeSpec& spec, caps::ProbeOutcome outcome,
                            std::chrono::microseconds duration, std::optional<std::int64_t> native_error = std::nullopt) {
    return {
        .probe_id = spec.probe_id,
        .family = spec.family,
        .revision = spec.revision,
        .duration = duration,
        .outcome = outcome,
        .native_error = native_error,
    };
}

class ImmediateProbe final : public caps::RunningProbe {
public:
    explicit ImmediateProbe(caps::ProbeFragment fragment) : fragment_(std::move(fragment)) {}

    std::optional<caps::ProbeFragment> wait_until(std::chrono::steady_clock::time_point) override {
        return std::exchange(fragment_, std::nullopt);
    }
    void terminate() noexcept override {}

private:
    std::optional<caps::ProbeFragment> fragment_;
};

class ProcessProbe final : public caps::RunningProbe {
public:
    ProcessProbe(caps::ProbeSpec spec, pid_t pid, UniqueFd output, std::chrono::steady_clock::time_point started)
        : spec_(std::move(spec)), pid_(pid), output_(std::move(output)), started_(started) {}

    ~ProcessProbe() override { terminate(); }

    std::optional<caps::ProbeFragment> wait_until(std::chrono::steady_clock::time_point deadline) override {
        while (true) {
            // WNOWAIT leaves the helper a zombie, so its pid still names the process group
            // while the group is killed below.
            siginfo_t info{};
            if (waitid(P_PID, static_cast<id_t>(pid_), &info, WEXITED | WNOHANG | WNOWAIT) == -1) {
                if (errno == EINTR) {
                    continue;
                }
                const auto error = errno;
                terminate();
                return failure(spec_, caps::ProbeOutcome::os_failure, elapsed(), error);
            }
            if (info.si_pid == pid_) {
                return finish();
            }

            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                return std::nullopt;
            }
            const auto slice = std::min(std::chrono::ceil<std::chrono::milliseconds>(deadline - now), kPollSlice);
            pollfd entry{output_.get(), POLLIN, 0};
            poll(output_ ? &entry : nullptr, output_ ? 1 : 0, static_cast<int>(slice.count()));
            drain();
        }
    }

    void terminate() noexcept override {
        if (reaped_.exchange(true)) {
            return;
        }
        kill(-pid_, SIGKILL);
        while (waitpid(pid_, nullptr, 0) == -1 && errno == EINTR) {
        }
        output_.reset();
    }

private:
    [[nodiscard]] std::chrono::microseconds elapsed() const {
        return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started_);
    }

    // Reads whatever the pipe holds without blocking; closes it at end of file.
    void drain() {
        std::array<char, 4096> bytes{};
        while (output_) {
            const auto count = read(output_.get(), bytes.data(), bytes.size());
            if (count > 0) {
                const auto size = static_cast<std::size_t>(count);
                if (buffer_.size() + size <= report::kMaxProbeFragmentBytes) {
                    buffer_.append(bytes.data(), size);
                } else {
                    oversized_ = true;
                }
            } else if (count == -1 && errno == EINTR) {
                continue;
            } else if (count == -1 && errno == EAGAIN) {
                return;
            } else {
                output_.reset();
            }
        }
    }

    caps::ProbeFragment finish() {
        // A helper may not leave descendants holding inherited resources after it exits.
        kill(-pid_, SIGKILL);
        drain();
        int status = 0;
        pid_t reaped = -1;
        do {
            reaped = waitpid(pid_, &status, 0);
        } while (reaped == -1 && errno == EINTR);
        const auto wait_error = errno;
        reaped_ = true;
        output_.reset();

        const auto duration = elapsed();
        if (reaped != pid_) {
            return failure(spec_, caps::ProbeOutcome::os_failure, duration, wait_error);
        }
        if (WIFSIGNALED(status)) {
            return failure(spec_, caps::ProbeOutcome::helper_terminated, duration, WTERMSIG(status));
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            return failure(spec_, caps::ProbeOutcome::helper_terminated, duration, WEXITSTATUS(status));
        }
        if (oversized_) {
            return failure(spec_, caps::ProbeOutcome::malformed_output, duration);
        }
        auto parsed = report::parse_probe_fragment(buffer_);
        if (!parsed.ok()) {
            return failure(spec_, caps::ProbeOutcome::malformed_output, duration);
        }
        parsed.fragment->duration = duration;
        return std::move(*parsed.fragment);
    }

    caps::ProbeSpec spec_;
    pid_t pid_;
    UniqueFd output_;
    std::chrono::steady_clock::time_point started_;
    std::string buffer_;
    bool oversized_ = false;
    std::atomic_bool reaped_ = false;
};

// Owns spawn attribute and file action objects for one launch.
struct SpawnSetup {
    SpawnSetup() {
        actions_ready = posix_spawn_file_actions_init(&actions) == 0;
        attributes_ready = posix_spawnattr_init(&attributes) == 0;
    }
    ~SpawnSetup() {
        if (actions_ready) {
            posix_spawn_file_actions_destroy(&actions);
        }
        if (attributes_ready) {
            posix_spawnattr_destroy(&attributes);
        }
    }
    SpawnSetup(const SpawnSetup&) = delete;
    SpawnSetup& operator=(const SpawnSetup&) = delete;

    posix_spawn_file_actions_t actions{};
    posix_spawnattr_t attributes{};
    bool actions_ready = false;
    bool attributes_ready = false;
};

} // namespace

ProcessProbeExecutor::ProcessProbeExecutor(std::filesystem::path helper_path) : helper_path_(std::move(helper_path)) {}

std::unique_ptr<caps::RunningProbe> ProcessProbeExecutor::start(const caps::ProbeSpec& spec) {
    const auto started = std::chrono::steady_clock::now();
    const auto immediate_failure = [&](int error) {
        return std::make_unique<ImmediateProbe>(
            failure(spec, caps::ProbeOutcome::os_failure,
                    std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started),
                    error));
    };

    std::array<int, 2> pipe_fds{-1, -1};
    if (pipe(pipe_fds.data()) != 0) {
        return immediate_failure(errno);
    }
    UniqueFd output_read(pipe_fds[0]);
    UniqueFd output_write(pipe_fds[1]);
    if (fcntl(output_read.get(), F_SETFD, FD_CLOEXEC) == -1 || fcntl(output_write.get(), F_SETFD, FD_CLOEXEC) == -1 ||
        fcntl(output_read.get(), F_SETFL, O_NONBLOCK) == -1) {
        return immediate_failure(errno);
    }

    SpawnSetup setup;
    if (!setup.actions_ready || !setup.attributes_ready) {
        return immediate_failure(ENOMEM);
    }
    sigset_t default_signals;
    sigemptyset(&default_signals);
    sigaddset(&default_signals, SIGPIPE);
    sigset_t unblocked;
    sigemptyset(&unblocked);
    // A new process group makes the helper and all of its descendants killable together; the
    // close-on-exec default keeps every descriptor but the three below out of the helper.
    const short flags = POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_CLOEXEC_DEFAULT | POSIX_SPAWN_SETSIGDEF |
                        POSIX_SPAWN_SETSIGMASK;
    int error = posix_spawnattr_setflags(&setup.attributes, flags);
    if (error == 0) {
        error = posix_spawnattr_setpgroup(&setup.attributes, 0);
    }
    if (error == 0) {
        error = posix_spawnattr_setsigdefault(&setup.attributes, &default_signals);
    }
    if (error == 0) {
        error = posix_spawnattr_setsigmask(&setup.attributes, &unblocked);
    }
    if (error == 0) {
        error = posix_spawn_file_actions_addopen(&setup.actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    }
    if (error == 0) {
        error = posix_spawn_file_actions_adddup2(&setup.actions, output_write.get(), STDOUT_FILENO);
    }
    if (error == 0) {
        error = posix_spawn_file_actions_adddup2(&setup.actions, output_write.get(), STDERR_FILENO);
    }
    if (error != 0) {
        return immediate_failure(error);
    }

    std::string helper = helper_path_.native();
    std::string option = "--probe";
    std::string probe_id = spec.probe_id;
    std::array<char*, 4> arguments{helper.data(), option.data(), probe_id.data(), nullptr};
    pid_t pid = -1;
    error = posix_spawn(&pid, helper.c_str(), &setup.actions, &setup.attributes, arguments.data(), environ);
    if (error != 0) {
        return immediate_failure(error);
    }

    output_write.reset();
    return std::make_unique<ProcessProbe>(spec, pid, std::move(output_read), started);
}

} // namespace catro::platform::macos

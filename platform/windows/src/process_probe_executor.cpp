#include <catro/platform/windows/capability_service.hpp>

#include <catro/reporting/canonical_json.hpp>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace catro::platform::windows {
namespace {

namespace caps = catro::capabilities;
namespace report = catro::reporting;

class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) : handle_(handle) {}
    ~UniqueHandle() { reset(); }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            reset(std::exchange(other.handle_, nullptr));
        }
        return *this;
    }

    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

    void reset(HANDLE replacement = nullptr) noexcept {
        if (*this) {
            CloseHandle(handle_);
        }
        handle_ = replacement;
    }

private:
    HANDLE handle_ = nullptr;
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
    ProcessProbe(caps::ProbeSpec spec, UniqueHandle process, UniqueHandle job, UniqueHandle output,
                 std::chrono::steady_clock::time_point started)
        : spec_(std::move(spec)), process_(std::move(process)), job_(std::move(job)), output_(std::move(output)),
          started_(started), reader_([this] { read_output(); }) {}

    // Always kills the job first: a descendant still holding the output pipe would otherwise
    // keep the reader blocked after the helper itself exited.
    ~ProcessProbe() override { terminate(); }

    std::optional<caps::ProbeFragment> wait_until(std::chrono::steady_clock::time_point deadline) override {
        const auto now = std::chrono::steady_clock::now();
        DWORD wait_milliseconds = 0;
        if (deadline > now) {
            const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - now).count();
            wait_milliseconds = static_cast<DWORD>(std::min<std::int64_t>(remaining, MAXDWORD - 1));
        }
        const auto wait = WaitForSingleObject(process_.get(), wait_milliseconds);
        if (wait == WAIT_TIMEOUT) {
            return std::nullopt;
        }

        const auto duration = elapsed();
        if (wait == WAIT_FAILED) {
            return failure(spec_, caps::ProbeOutcome::os_failure, duration, GetLastError());
        }

        // A helper may not leave descendants holding inherited resources after it exits.
        TerminateJobObject(job_.get(), 0);
        finish_reader();

        DWORD exit_code = 0;
        if (!GetExitCodeProcess(process_.get(), &exit_code)) {
            return failure(spec_, caps::ProbeOutcome::os_failure, duration, GetLastError());
        }
        if (exit_code != 0) {
            return failure(spec_, caps::ProbeOutcome::helper_terminated, duration, exit_code);
        }
        if (oversized_) {
            return failure(spec_, caps::ProbeOutcome::malformed_output, duration);
        }

        auto parsed = report::parse_probe_fragment(buffer_);
        if (!parsed.ok()) {
            return failure(spec_, caps::ProbeOutcome::malformed_output, duration);
        }
        parsed.fragment->duration = duration;
        return std::move(parsed.fragment);
    }

    void terminate() noexcept override {
        if (terminated_.exchange(true)) {
            return;
        }
        if (job_) {
            TerminateJobObject(job_.get(), ERROR_TIMEOUT);
        }
        if (process_) {
            WaitForSingleObject(process_.get(), 5000);
        }
        finish_reader();
    }

private:
    [[nodiscard]] std::chrono::microseconds elapsed() const {
        return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started_);
    }

    void read_output() {
        std::array<char, 4096> bytes{};
        DWORD count = 0;
        while (ReadFile(output_.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) && count != 0) {
            if (buffer_.size() + count <= report::kMaxProbeFragmentBytes) {
                buffer_.append(bytes.data(), count);
            } else {
                oversized_ = true;
            }
        }
    }

    void finish_reader() noexcept {
        if (reader_.joinable()) {
            reader_.join();
        }
    }

    caps::ProbeSpec spec_;
    UniqueHandle process_;
    UniqueHandle job_;
    UniqueHandle output_;
    std::chrono::steady_clock::time_point started_;
    std::string buffer_;
    bool oversized_ = false;
    std::atomic_bool terminated_ = false;
    // Last: the reader starts in the constructor and writes buffer_ and oversized_.
    std::thread reader_;
};

std::wstring command_line(const std::filesystem::path& helper, const caps::ProbeSpec& spec) {
    std::wstring line = L"\"";
    line += helper.native();
    line += L"\" --probe ";
    line.append(spec.probe_id.begin(), spec.probe_id.end());
    return line;
}

} // namespace

ProcessProbeExecutor::ProcessProbeExecutor(std::filesystem::path helper_path) : helper_path_(std::move(helper_path)) {}

std::unique_ptr<caps::RunningProbe> ProcessProbeExecutor::start(const caps::ProbeSpec& spec) {
    const auto started = std::chrono::steady_clock::now();
    const auto immediate_failure = [&](DWORD error) {
        return std::make_unique<ImmediateProbe>(
            failure(spec, caps::ProbeOutcome::os_failure, std::chrono::duration_cast<std::chrono::microseconds>(
                                                                  std::chrono::steady_clock::now() - started),
                    error));
    };

    SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE output_read_raw = nullptr;
    HANDLE output_write_raw = nullptr;
    if (!CreatePipe(&output_read_raw, &output_write_raw, &inheritable, 0)) {
        return immediate_failure(GetLastError());
    }
    UniqueHandle output_read(output_read_raw);
    UniqueHandle output_write(output_write_raw);
    if (!SetHandleInformation(output_read.get(), HANDLE_FLAG_INHERIT, 0)) {
        return immediate_failure(GetLastError());
    }

    UniqueHandle null_input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!null_input) {
        return immediate_failure(GetLastError());
    }

    UniqueHandle job(CreateJobObjectW(nullptr, nullptr));
    if (!job) {
        return immediate_failure(GetLastError());
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        return immediate_failure(GetLastError());
    }

    SIZE_T attribute_bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_bytes);
    std::vector<std::byte> attribute_storage(attribute_bytes);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attribute_bytes)) {
        return immediate_failure(GetLastError());
    }
    const auto delete_attributes = [&] { DeleteProcThreadAttributeList(attributes); };
    std::array inherited_handles{output_write.get(), null_input.get()};
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited_handles.data(),
                                   sizeof(inherited_handles), nullptr, nullptr)) {
        const auto error = GetLastError();
        delete_attributes();
        return immediate_failure(error);
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = null_input.get();
    startup.StartupInfo.hStdOutput = output_write.get();
    startup.StartupInfo.hStdError = output_write.get();
    startup.lpAttributeList = attributes;

    auto line = command_line(helper_path_, spec);
    std::vector<wchar_t> writable_line(line.begin(), line.end());
    writable_line.push_back(L'\0');
    PROCESS_INFORMATION process_info{};
    const auto created = CreateProcessW(helper_path_.c_str(), writable_line.data(), nullptr, nullptr, TRUE,
                                        CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr,
                                        nullptr, &startup.StartupInfo, &process_info);
    const auto create_error = created ? ERROR_SUCCESS : GetLastError();
    delete_attributes();
    if (!created) {
        return immediate_failure(create_error);
    }

    UniqueHandle process(process_info.hProcess);
    UniqueHandle thread(process_info.hThread);
    if (!AssignProcessToJobObject(job.get(), process.get())) {
        const auto error = GetLastError();
        TerminateProcess(process.get(), error);
        WaitForSingleObject(process.get(), 5000);
        return immediate_failure(error);
    }
    if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
        const auto error = GetLastError();
        TerminateJobObject(job.get(), error);
        WaitForSingleObject(process.get(), 5000);
        return immediate_failure(error);
    }

    output_write.reset();
    null_input.reset();
    thread.reset();
    return std::make_unique<ProcessProbe>(spec, std::move(process), std::move(job), std::move(output_read), started);
}

} // namespace catro::platform::windows

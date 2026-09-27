#include <catro/platform/windows/capability_service.hpp>

#include <catch2/catch_test_macros.hpp>

#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>

using namespace std::chrono_literals;
using namespace catro::capabilities;
using namespace catro::platform::windows;

namespace {

std::optional<DWORD> read_pid(const std::filesystem::path& path) {
    std::ifstream input(path);
    DWORD pid = 0;
    if (input >> pid) {
        return pid;
    }
    return std::nullopt;
}

} // namespace

TEST_CASE("a timed-out helper and its descendants are terminated by the job") {
    const auto pid_path = std::filesystem::temp_directory_path() /
                          (L"catro-hanging-" + std::to_wstring(GetCurrentProcessId()) + L".pid");
    std::filesystem::remove(pid_path);
    REQUIRE(SetEnvironmentVariableW(L"CATRO_TEST_PID_FILE", pid_path.c_str()));

    ProcessProbeExecutor executor{CATRO_HANGING_PROBE_HELPER};
    const ProbeSpec spec{"windows.system.v1", ProbeFamily::system, ProbeAccess::passive,
                         ProbeDomain::platform_hardware, 1, 50ms};
    auto running = executor.start(spec);
    REQUIRE(running);

    std::optional<DWORD> pid;
    const auto pid_deadline = std::chrono::steady_clock::now() + 1s;
    while (!pid && std::chrono::steady_clock::now() < pid_deadline) {
        pid = read_pid(pid_path);
        std::this_thread::sleep_for(5ms);
    }
    REQUIRE(pid);

    REQUIRE_FALSE(running->wait_until(std::chrono::steady_clock::now() + 50ms));
    running->terminate();
    running.reset();

    const HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, *pid);
    if (process != nullptr) {
        REQUIRE(WaitForSingleObject(process, 0) == WAIT_OBJECT_0);
        CloseHandle(process);
    }

    REQUIRE(SetEnvironmentVariableW(L"CATRO_TEST_PID_FILE", nullptr));
    std::filesystem::remove(pid_path);
}

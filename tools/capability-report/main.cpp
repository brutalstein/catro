#include "report_cli.hpp"

#include <catro/platform/windows/capability_service.hpp>

#include <Windows.h>
#include <fcntl.h>
#include <io.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace caps = catro::capabilities;

// Generous next to the publication budget: covers helper process start-up on a cold machine.
constexpr std::chrono::seconds kCollectionTimeout{10};

std::string utf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const auto length = static_cast<int>(text.size());
    const auto size = WideCharToMultiByte(CP_UTF8, 0, text.data(), length, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), length, result.data(), size, nullptr, nullptr);
    return result;
}

std::optional<std::filesystem::path> executable_directory() {
    std::wstring buffer(MAX_PATH, L'\0');
    while (true) {
        const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return std::nullopt;
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            return std::filesystem::path(buffer).parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::optional<catro::reporting::CapabilityReport> collect_report() {
    const auto directory = executable_directory();
    if (!directory) {
        return std::nullopt;
    }
    std::mutex mutex;
    std::condition_variable published;
    std::optional<caps::CapabilitySnapshot> snapshot;
    catro::platform::windows::CapabilityService service{*directory / L"catro-capability-probe.exe"};
    service.start([&](caps::SnapshotUpdate update) {
        {
            const std::scoped_lock lock(mutex);
            if (!snapshot) {
                snapshot = std::move(update.snapshot);
            }
        }
        published.notify_all();
    });
    {
        std::unique_lock lock(mutex);
        published.wait_for(lock, kCollectionTimeout, [&] { return snapshot.has_value(); });
    }
    service.stop();
    if (!snapshot) {
        return std::nullopt;
    }
    return catro::reporting::make_report(std::move(*snapshot), catro::reporting::representative_request());
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> storage;
    for (int index = 1; index < argc; ++index) {
        storage.push_back(utf8(argv[index]));
    }
    const std::vector<std::string_view> arguments(storage.begin(), storage.end());
    // Reports use LF line endings on every platform.
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
        return catro::tools::report_write_failed;
    }
    return catro::tools::run_report_cli(arguments, collect_report, std::cout, std::cerr);
}

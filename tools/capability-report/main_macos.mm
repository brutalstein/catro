#include "report_cli.hpp"

#include <catro/platform/macos/capability_service.hpp>

#import <Foundation/Foundation.h>
#include <mach-o/dyld.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
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

std::optional<std::filesystem::path> executable_directory() {
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return std::nullopt;
    }
    buffer.resize(std::char_traits<char>::length(buffer.c_str()));
    std::error_code error;
    const auto resolved = std::filesystem::canonical(buffer, error);
    if (error) {
        return std::nullopt;
    }
    return resolved.parent_path();
}

std::optional<catro::reporting::CapabilityReport> collect_report() {
    const auto directory = executable_directory();
    if (!directory) {
        return std::nullopt;
    }
    std::mutex mutex;
    std::condition_variable published;
    std::optional<caps::CapabilitySnapshot> snapshot;
    catro::platform::macos::CapabilityService service{*directory / "catro-capability-probe"};
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

int main(int argc, char** argv) {
    @autoreleasepool {
        const std::vector<std::string_view> arguments(argv + 1, argv + argc);
        return catro::tools::run_report_cli(arguments, collect_report, std::cout, std::cerr);
    }
}

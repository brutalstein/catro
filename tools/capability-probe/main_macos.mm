#include <catro/capabilities/probe_coordinator.hpp>
#include <catro/platform/macos/capability_service.hpp>
#include <catro/reporting/canonical_json.hpp>

#import <Foundation/Foundation.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>

namespace caps = catro::capabilities;

int main(int argc, char** argv) {
    if (argc != 3 || std::string_view(argv[1]) != "--probe") {
        return 2;
    }
    const std::string requested(argv[2]);
    if (!std::ranges::all_of(requested, [](char c) { return c > ' ' && c <= '~'; })) {
        return 2;
    }

    const auto schedule = caps::make_initial_probe_schedule(caps::OperatingSystem::macos, 1, {});
    const auto found = std::ranges::find(schedule.probes, requested, &caps::ProbeSpec::probe_id);
    if (found == schedule.probes.end()) {
        return 2;
    }

    std::string output;
    @autoreleasepool {
        output = catro::reporting::to_canonical_json(catro::platform::macos::run_passive_probe(*found));
    }
    return std::fwrite(output.data(), 1, output.size(), stdout) == output.size() && std::fflush(stdout) == 0 ? 0 : 4;
}

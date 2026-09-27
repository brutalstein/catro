#include <catro/capabilities/probe_coordinator.hpp>
#include <catro/platform/windows/capability_service.hpp>
#include <catro/reporting/canonical_json.hpp>

#include <Windows.h>
#include <fcntl.h>
#include <io.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>

namespace caps = catro::capabilities;

int wmain(int argc, wchar_t** argv) {
    if (argc != 3 || std::wstring_view(argv[1]) != L"--probe") {
        return 2;
    }
    std::string requested;
    for (const wchar_t value : std::wstring_view(argv[2])) {
        if (value < 0 || value > 0x7f) {
            return 2;
        }
        requested.push_back(static_cast<char>(value));
    }

    // Effective per-monitor DPI and physical display sizes are only reported to aware processes.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const auto schedule = caps::make_initial_probe_schedule(caps::OperatingSystem::windows, 1, {});
    const auto found = std::ranges::find(schedule.probes, requested, &caps::ProbeSpec::probe_id);
    if (found == schedule.probes.end()) {
        return 2;
    }

    const auto fragment = catro::platform::windows::run_passive_probe(*found);
    const auto output = catro::reporting::to_canonical_json(fragment);
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
        return 3;
    }
    return std::fwrite(output.data(), 1, output.size(), stdout) == output.size() ? 0 : 4;
}

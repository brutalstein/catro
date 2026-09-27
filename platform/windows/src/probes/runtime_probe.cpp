#include <catro/platform/windows/capability_service.hpp>

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace catro::platform::windows {
namespace {

namespace caps = catro::capabilities;

caps::Provenance measured(std::string_view probe_id) {
    return {.probe_id = std::string(probe_id), .method = caps::EvidenceMethod::measured};
}

caps::Provenance absent(std::string_view probe_id, caps::IssueCode issue) {
    return {std::string(probe_id), caps::EvidenceMethod::measured, caps::Confidence::degraded, issue};
}

template <class T>
caps::Observed<T> known(T value, std::string_view probe_id) {
    return caps::Observed<T>::known(std::move(value), measured(probe_id));
}

template <class T>
caps::Observed<T> unknown(std::string_view probe_id, caps::IssueCode issue) {
    return caps::Observed<T>::unknown(absent(probe_id, issue));
}

void add_issue(std::vector<caps::ProbeIssue>& issues, std::string_view probe_id, caps::IssueCode code) {
    const caps::ProbeIssue issue{std::string(probe_id), code};
    if (std::ranges::find(issues, issue) == issues.end()) {
        issues.push_back(issue);
    }
}

} // namespace

caps::ProbeFragment run_runtime_probe(const caps::ProbeSpec& spec) {
    const auto started = std::chrono::steady_clock::now();
    caps::ProbeFragment fragment{
        .probe_id = spec.probe_id,
        .family = spec.family,
        .revision = spec.revision,
        .outcome = caps::ProbeOutcome::partial,
    };
    caps::RuntimeProbeFacts facts;

    SYSTEM_POWER_STATUS power{};
    if (GetSystemPowerStatus(&power)) {
        if (power.ACLineStatus == 0 || power.ACLineStatus == 1) {
            facts.power_source = known(power.ACLineStatus == 1 ? caps::PowerSource::ac : caps::PowerSource::battery,
                                       spec.probe_id);
        } else {
            facts.power_source = unknown<caps::PowerSource>(spec.probe_id, caps::IssueCode::not_reported);
        }
        if (power.BatteryFlag == 255) {
            facts.battery_present = unknown<bool>(spec.probe_id, caps::IssueCode::not_reported);
        } else {
            facts.battery_present = known((power.BatteryFlag & 128U) == 0, spec.probe_id);
        }
        facts.low_power_mode = known(power.SystemStatusFlag == 1, spec.probe_id);
    } else {
        fragment.native_error = GetLastError();
        facts.power_source = unknown<caps::PowerSource>(spec.probe_id, caps::IssueCode::os_failure);
        facts.battery_present = unknown<bool>(spec.probe_id, caps::IssueCode::os_failure);
        facts.low_power_mode = unknown<bool>(spec.probe_id, caps::IssueCode::os_failure);
        add_issue(fragment.issues, spec.probe_id, caps::IssueCode::os_failure);
    }

    facts.thermal = unknown<caps::ThermalPressure>(spec.probe_id, caps::IssueCode::not_reported);
    facts.memory_pressure = unknown<caps::MemoryPressure>(spec.probe_id, caps::IssueCode::not_reported);
    facts.remote_session = known(GetSystemMetrics(SM_REMOTESESSION) != 0, spec.probe_id);
    facts.headless = known(GetSystemMetrics(SM_CMONITORS) == 0, spec.probe_id);
    add_issue(fragment.issues, spec.probe_id, caps::IssueCode::not_reported);

    fragment.runtime = std::move(facts);
    fragment.duration = std::max(std::chrono::microseconds{1},
                                 std::chrono::duration_cast<std::chrono::microseconds>(
                                     std::chrono::steady_clock::now() - started));
    return fragment;
}

} // namespace catro::platform::windows

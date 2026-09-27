#include "../probe_support.hpp"

#import <Foundation/Foundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <IOKit/ps/IOPSKeys.h>
#include <IOKit/ps/IOPowerSources.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <optional>

namespace catro::platform::macos {
namespace {

caps::Observed<caps::PowerSource> power_source(std::string_view probe_id) {
    const CFTypeRef info = IOPSCopyPowerSourcesInfo();
    if (info == nullptr) {
        return unknown<caps::PowerSource>(probe_id, caps::IssueCode::not_reported);
    }
    // Get rule: the string is owned by `info`.
    const CFStringRef providing = IOPSGetProvidingPowerSourceType(info);
    std::optional<caps::PowerSource> source;
    if (providing != nullptr && CFStringCompare(providing, CFSTR(kIOPSACPowerValue), 0) == kCFCompareEqualTo) {
        source = caps::PowerSource::ac;
    } else if (providing != nullptr &&
               CFStringCompare(providing, CFSTR(kIOPSBatteryPowerValue), 0) == kCFCompareEqualTo) {
        source = caps::PowerSource::battery;
    }
    CFRelease(info);
    // UPS power has no counterpart in the model and stays unknown.
    return source ? known(*source, probe_id) : unknown<caps::PowerSource>(probe_id, caps::IssueCode::not_reported);
}

std::optional<caps::ThermalPressure> thermal(NSProcessInfoThermalState state) {
    switch (state) {
    case NSProcessInfoThermalStateNominal:
        return caps::ThermalPressure::nominal;
    case NSProcessInfoThermalStateFair:
        return caps::ThermalPressure::fair;
    case NSProcessInfoThermalStateSerious:
        return caps::ThermalPressure::serious;
    case NSProcessInfoThermalStateCritical:
        return caps::ThermalPressure::critical;
    }
    return std::nullopt;
}

// Values of kern.memorystatus_vm_pressure_level, as in DISPATCH_MEMORYPRESSURE_*.
std::optional<caps::MemoryPressure> memory_pressure(std::int64_t level) {
    switch (level) {
    case 1:
        return caps::MemoryPressure::normal;
    case 2:
        return caps::MemoryPressure::warning;
    case 4:
        return caps::MemoryPressure::critical;
    default:
        return std::nullopt;
    }
}

} // namespace

caps::ProbeFragment run_runtime_probe(const caps::ProbeSpec& spec) {
    const auto started = std::chrono::steady_clock::now();
    auto fragment = begin_fragment(spec);
    caps::RuntimeProbeFacts facts;

    facts.power_source = power_source(spec.probe_id);
    if (const auto battery = internal_battery_present()) {
        facts.battery_present = known(*battery, spec.probe_id);
    } else {
        facts.battery_present = unknown<bool>(spec.probe_id, caps::IssueCode::not_reported);
    }

    @autoreleasepool {
        NSProcessInfo* process = NSProcessInfo.processInfo;
        facts.low_power_mode = known(static_cast<bool>(process.lowPowerModeEnabled), spec.probe_id);
        if (const auto pressure = thermal(process.thermalState)) {
            facts.thermal = known(*pressure, spec.probe_id);
        } else {
            facts.thermal = unknown<caps::ThermalPressure>(spec.probe_id, caps::IssueCode::not_reported);
        }
    }

    int pressure_error = 0;
    const auto level = sysctl_integer("kern.memorystatus_vm_pressure_level", pressure_error);
    if (const auto pressure = level ? memory_pressure(*level) : std::nullopt) {
        facts.memory_pressure = known(*pressure, spec.probe_id);
    } else if (!level && pressure_error != ENOENT) {
        fragment.native_error = pressure_error;
        facts.memory_pressure = unknown<caps::MemoryPressure>(spec.probe_id, caps::IssueCode::os_failure);
        add_issue(fragment.issues, spec.probe_id, caps::IssueCode::os_failure);
    } else {
        facts.memory_pressure = unknown<caps::MemoryPressure>(spec.probe_id, caps::IssueCode::not_reported);
    }

    // No public API tells a screen-sharing session from a local one without asking for access.
    facts.remote_session = unknown<bool>(spec.probe_id, caps::IssueCode::not_reported);

    std::uint32_t active_displays = 0;
    const auto display_error = CGGetActiveDisplayList(0, nullptr, &active_displays);
    if (display_error == kCGErrorSuccess) {
        facts.headless = known(active_displays == 0, spec.probe_id);
    } else {
        if (!fragment.native_error) {
            fragment.native_error = display_error;
        }
        facts.headless = unknown<bool>(spec.probe_id, caps::IssueCode::os_failure);
        add_issue(fragment.issues, spec.probe_id, caps::IssueCode::os_failure);
    }

    // Remote session state is never reported, so the family is always partial.
    add_issue(fragment.issues, spec.probe_id, caps::IssueCode::not_reported);
    fragment.runtime = std::move(facts);
    finish_fragment(fragment, started);
    return fragment;
}

} // namespace catro::platform::macos

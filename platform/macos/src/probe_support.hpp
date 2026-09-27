#pragma once

#include "macos_translation.hpp"

#include <catro/capabilities/probe.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Evidence helpers shared by the macOS probe families. Plain C++: no Objective-C types.
namespace catro::platform::macos {

namespace caps = catro::capabilities;

inline caps::Provenance measured(std::string_view probe_id) {
    return {.probe_id = std::string(probe_id), .method = caps::EvidenceMethod::measured};
}

inline caps::Provenance inferred(std::string_view probe_id, caps::IssueCode issue) {
    return {std::string(probe_id), caps::EvidenceMethod::inferred, caps::Confidence::degraded, issue};
}

inline caps::Provenance absent(std::string_view probe_id, caps::IssueCode issue) {
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

inline void add_issue(std::vector<caps::ProbeIssue>& issues, std::string_view probe_id, caps::IssueCode code) {
    const caps::ProbeIssue issue{std::string(probe_id), code};
    if (std::ranges::find(issues, issue) == issues.end()) {
        issues.push_back(issue);
    }
}

inline caps::ProbeFragment begin_fragment(const caps::ProbeSpec& spec) {
    return {
        .probe_id = spec.probe_id,
        .family = spec.family,
        .revision = spec.revision,
        .outcome = caps::ProbeOutcome::partial,
    };
}

inline void finish_fragment(caps::ProbeFragment& fragment, std::chrono::steady_clock::time_point started) {
    fragment.duration = std::max(std::chrono::microseconds{1}, std::chrono::duration_cast<std::chrono::microseconds>(
                                                                   std::chrono::steady_clock::now() - started));
}

// A 32- or 64-bit integer sysctl. On failure returns nothing and stores errno in `error`;
// ENOENT means this system does not define the key.
[[nodiscard]] std::optional<std::int64_t> sysctl_integer(const char* name, int& error);

// Whether an internal battery is attached, from the IOKit power source list. Nothing when the
// list is unavailable.
[[nodiscard]] std::optional<bool> internal_battery_present();

// Metal devices with their PCI identity where the IORegistry has one. Nothing when Metal
// returns no device list.
[[nodiscard]] std::optional<std::vector<NativeGpu>> enumerate_gpus();

inline void settle_outcome(caps::ProbeFragment& fragment) {
    fragment.outcome = fragment.issues.empty() ? caps::ProbeOutcome::success : caps::ProbeOutcome::partial;
}

} // namespace catro::platform::macos

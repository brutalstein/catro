#pragma once

#include <catro/capabilities/media_plan.hpp>
#include <catro/capabilities/model.hpp>
#include <catro/capabilities/policy.hpp>

#include <utility>

namespace catro::reporting {

// What diagnostics export: a snapshot and the plan for one explicit request. The plan carries
// the request, the policy version, the local envelope, the fallback chain, and the bounded trace.
struct CapabilityReport {
    capabilities::CapabilitySnapshot snapshot;
    capabilities::MediaPlan plan;

    friend bool operator==(const CapabilityReport&, const CapabilityReport&) = default;
};

inline CapabilityReport make_report(capabilities::CapabilitySnapshot snapshot,
                                    const capabilities::MediaDecisionRequest& request) {
    auto plan = capabilities::derive_media_plan(snapshot, request, capabilities::kPolicyVersion);
    return {std::move(snapshot), std::move(plan)};
}

} // namespace catro::reporting

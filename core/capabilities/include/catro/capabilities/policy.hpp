#pragma once

#include <catro/capabilities/media_plan.hpp>
#include <catro/capabilities/model.hpp>
#include <catro/capabilities/version.hpp>

// Pure local media policy. Inputs are validated snapshots and explicit media-domain requests;
// no UI state, peer, or network input participates, and no vendor or model name is consulted.
namespace catro::capabilities {

// Precedence: thermal pressure, constrained session (headless, remote, critical memory),
// explicit preference, battery or low-power mode, unknown power source, mains power by role.
[[nodiscard]] ProfileDecision derive_operating_profile(const CapabilitySnapshot& snapshot, OperatingPreference preference);

// Probes emit one transfer relationship per capture path and encoder pair (kind `unknown` when
// unprovable), so an encoder without a relationship to a usable capture path is unreachable.
[[nodiscard]] LocalQualityEnvelope derive_local_envelope(const CapabilitySnapshot& snapshot,
                                                        const MediaDecisionRequest& request,
                                                        OperatingProfile profile);

// Rejects invalid snapshots, requests, and unsupported policy versions, then ranks every
// capture/transfer/encoder/mode path by the contract's rule precedence: hard request
// constraints, known-unsupported removal, known evidence, GPU affinity, transfer cost,
// low-latency hardware, then the profile-bounded quality, the most conservative codec, and
// typed identifiers. Software encoding is an explicit candidate with stated consequences and
// is never selected in the safe local envelope. Deterministic for any inventory order.
[[nodiscard]] MediaPlan derive_media_plan(const CapabilitySnapshot& snapshot, const MediaDecisionRequest& request,
                                          PolicyVersion policy_version);

} // namespace catro::capabilities

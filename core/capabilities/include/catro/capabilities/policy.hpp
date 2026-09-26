#pragma once

#include <catro/capabilities/media_plan.hpp>
#include <catro/capabilities/model.hpp>

#include <optional>

// Pure local media policy. Inputs are validated snapshots and explicit media-domain requests;
// no UI state, peer, or network input participates, and no vendor or model name is consulted.
namespace catro::capabilities {

enum class LatencyClass {
    interactive,
    standard,
};

enum class OperatingPreference {
    automatic,
    performance,
    balanced,
    efficiency,
};

// What the caller would like; never evidence of what the machine can do.
struct RequestedQuality {
    Dimensions resolution{1920, 1080};
    Rational frame_rate{60, 1};
    bool hdr = false;

    friend bool operator==(const RequestedQuality&, const RequestedQuality&) = default;
};

struct MediaDecisionRequest {
    SourceKind source = SourceKind::display;
    // The display being captured, or the display the window/application is on. When absent,
    // the display the platform reports as primary is used.
    std::optional<DisplayId> display;
    LatencyClass latency = LatencyClass::interactive;
    OperatingPreference preference = OperatingPreference::automatic;
    RequestedQuality quality;

    friend bool operator==(const MediaDecisionRequest&, const MediaDecisionRequest&) = default;
};

// Precedence: thermal pressure, constrained session (headless, remote, critical memory),
// explicit preference, battery or low-power mode, unknown power source, mains power by role.
[[nodiscard]] ProfileDecision derive_operating_profile(const CapabilitySnapshot& snapshot, OperatingPreference preference);

// Probes emit one transfer relationship per capture path and encoder pair (kind `unknown` when
// unprovable), so an encoder without a relationship to a usable capture path is unreachable.
[[nodiscard]] LocalQualityEnvelope derive_local_envelope(const CapabilitySnapshot& snapshot,
                                                        const MediaDecisionRequest& request,
                                                        OperatingProfile profile);

} // namespace catro::capabilities

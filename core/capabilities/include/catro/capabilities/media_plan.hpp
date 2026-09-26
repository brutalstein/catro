#pragma once

#include <catro/capabilities/model.hpp>

#include <cstdint>
#include <vector>

namespace catro::capabilities {

enum class OperatingProfile {
    performance,
    balanced,
    efficiency,
    thermal_constrained,
    safe_local_envelope,
};

// Stable rule identifiers for the operating-profile decision, in precedence order.
enum class ProfileRule {
    thermal_pressure,
    constrained_session,
    explicit_preference,
    battery_or_low_power,
    unknown_power_source,
    mains_power_desktop,
    mains_power,
};

struct ProfileDecision {
    OperatingProfile profile = OperatingProfile::safe_local_envelope;
    ProfileRule rule = ProfileRule::unknown_power_source;

    friend bool operator==(const ProfileDecision&, const ProfileDecision&) = default;
};

// Stable explanation codes shared by envelope derivation and plan traces.
enum class ReasonCode {
    no_source_display,
    no_capture_path,
    no_encoder_mode,
    limited_by_source,
    limited_by_capture,
    limited_by_encoder,
    limited_by_unknown_limits,
    limited_by_profile,
    hdr_unavailable,
};

// Locally achievable bounds for one request. Requested quality and future negotiated quality
// are separate concepts; negotiation may only narrow these bounds.
struct LocalQualityEnvelope {
    bool viable = false;
    // Largest achievable size with even dimensions and the source aspect ratio.
    Dimensions resolution;
    // Highest achievable frame rate at that size, exact.
    Rational frame_rate;
    std::uint8_t bit_depth = 8;
    bool hdr = false;
    // Degraded when a bound comes from unknown limits or unknown support.
    Confidence confidence = Confidence::high;
    // Constraints that bound the envelope, or why no local path exists. Sorted, unique.
    std::vector<ReasonCode> reasons;

    friend bool operator==(const LocalQualityEnvelope&, const LocalQualityEnvelope&) = default;
};

} // namespace catro::capabilities

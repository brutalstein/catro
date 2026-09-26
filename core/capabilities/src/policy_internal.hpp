#pragma once

#include <catro/capabilities/policy.hpp>

#include <cstdint>
#include <optional>
#include <vector>

// Internals shared by envelope derivation, candidate ranking, and plan assembly. Not installed.
namespace catro::capabilities::detail {

template <class T>
const T* known_value(const Observed<T>& observed) {
    return observed.knowledge() == Knowledge::known && observed.value() ? &*observed.value() : nullptr;
}

inline bool usable(const SupportFact& fact) {
    return fact.status != Support::unsupported;
}

inline bool supported(const SupportFact& fact) {
    return fact.status == Support::supported;
}

struct Ceiling {
    Dimensions box;
    Rational frame_rate;
    bool hdr = false;
};

Ceiling ceiling_for(OperatingProfile profile);

std::uint64_t pixels(Dimensions dimensions);
// A landscape box applies to landscape sources and is turned for portrait ones.
Dimensions oriented(Dimensions box, Dimensions source);
// Largest size with the source aspect ratio inside the bound, never upscaled.
Dimensions fit(Dimensions source, Dimensions bound);

struct QualityPoint {
    Dimensions resolution;
    Rational frame_rate;
    bool degraded = false;
    std::vector<ReasonCode> reasons;

    void bound_size(Dimensions box, ReasonCode reason);
    void bound_rate(Rational limit, ReasonCode reason);
};

// The requested display, else the primary, else the only display; none when headless.
const DisplayState* source_display(const CapabilitySnapshot& snapshot, const MediaDecisionRequest& request);
bool permission_denied(const CapabilitySnapshot& snapshot, const CapturePathId& path);
// What one display, capture path, and encoder mode achieve under a ceiling; empty when the
// source is below the mode's minimum size.
std::optional<QualityPoint> quality_point(const DisplayState& display, const CapturePathCapability& capture,
                                          const EncoderModeCapability& mode, const Ceiling& ceiling);
// A supported HDR mode with a PQ or HLG transfer function and at least 10 bits.
bool hdr_mode(const EncoderModeCapability& mode);
// Capture emits HDR and the transfer provably keeps it.
bool preserves_hdr(const CapturePathCapability& capture, const TransferPathCapability& transfer);

struct Ranking {
    // Best first; unbounded.
    std::vector<MediaCandidate> ranked;
    // Ranked path records in rank order plus every rejection, in no particular order; unbounded.
    std::vector<TraceRecord> records;
    // Why nothing was ranked.
    ReasonCode failure = ReasonCode::no_encoder_mode;
};

Ranking rank_candidates(const CapabilitySnapshot& snapshot, const MediaDecisionRequest& request,
                        const DisplayState& display, OperatingProfile profile, bool hdr_wanted);

} // namespace catro::capabilities::detail

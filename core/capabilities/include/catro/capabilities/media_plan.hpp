#pragma once

#include <catro/capabilities/model.hpp>
#include <catro/capabilities/version.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

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

// Stable explanation codes shared by envelope derivation, candidate ranking, and plan traces.
// New codes are appended so existing values keep their order.
enum class ReasonCode {
    // Envelope bounds and plan outcomes.
    no_source_display,
    no_capture_path,
    no_encoder_mode,
    limited_by_source,
    limited_by_capture,
    limited_by_encoder,
    limited_by_unknown_limits,
    limited_by_profile,
    hdr_unavailable,
    // Invalid input.
    unsupported_policy_version,
    unsupported_schema,
    invalid_snapshot,
    invalid_request,
    // Candidate rejections.
    source_kind_mismatch,
    capture_permission_denied,
    hdr_mode_not_requested,
    capture_unsupported,
    transfer_unsupported,
    encoder_unsupported,
    mode_unsupported,
    no_transfer_path,
    below_encoder_minimum,
    software_excluded_by_profile,
    // Ranking drawbacks, in rule precedence order.
    unknown_support,
    unknown_limits,
    gpu_affinity_unknown,
    gpu_affinity_broken,
    same_adapter_copy,
    cross_adapter_transfer,
    cpu_staging_transfer,
    unknown_transfer,
    software_encoder,
    low_latency_unproven,
    low_latency_unsupported,
    sdr_only,
    lower_quality,
    less_conservative_codec,
    stable_order,
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

// Stable policy rule IDs, in the precedence order of the policy contract.
enum class PolicyRule {
    reject_invalid_input,
    hard_request_constraints,
    remove_unsupported,
    prefer_known_evidence,
    preserve_gpu_affinity,
    prefer_cheaper_transfer,
    prefer_low_latency_hardware,
    derive_operating_profile,
    apply_profile_ceilings,
    select_conservative_start,
    rank_fallbacks,
};

// What activating a candidate costs, stated explicitly rather than hidden in its rank.
enum class Consequence {
    cpu_load,
    power_draw,
    added_latency,
    limited_quality,
};

// One end-to-end local media path: capture, transfer, encoder, and one concrete mode.
struct MediaCandidate {
    CapturePathId capture;
    TransferKind transfer = TransferKind::unknown;
    // The encoder's adapter when known; absent for software encoders.
    std::optional<GpuId> gpu;
    EncoderId encoder;
    ImplementationClass implementation = ImplementationClass::hardware;
    Codec codec = Codec::h264;
    EncoderModeKey mode;
    // True only when display, capture, transfer, and mode all keep HDR for an HDR request.
    bool hdr = false;
    // This path's local limits within the operating profile.
    Dimensions resolution;
    Rational frame_rate;
    Confidence confidence = Confidence::high;
    // Sorted, unique.
    std::vector<Consequence> consequences;

    friend bool operator==(const MediaCandidate&, const MediaCandidate&) = default;
};

struct StartingQuality {
    Dimensions resolution;
    Rational frame_rate;
    std::uint8_t bit_depth = 8;
    bool hdr = false;

    friend bool operator==(const StartingQuality&, const StartingQuality&) = default;
};

enum class DowngradeTrigger {
    battery_power,
    thermal_pressure,
};

// The quality the selected path drops to when a runtime condition moves the profile.
struct Downgrade {
    DowngradeTrigger trigger = DowngradeTrigger::battery_power;
    OperatingProfile profile = OperatingProfile::efficiency;
    StartingQuality quality;

    friend bool operator==(const Downgrade&, const Downgrade&) = default;
};

enum class DecisionCategory {
    input,
    capture,
    transfer,
    encoder,
    mode,
    path,
};
inline constexpr std::size_t kDecisionCategoryCount = 6;

enum class CandidateOutcome {
    selected,
    fallback,
    rejected,
};

// What a trace record decided about; fields that do not apply to its category are empty.
struct CandidateRef {
    std::optional<CapturePathId> capture;
    std::optional<EncoderId> encoder;
    std::optional<EncoderModeKey> mode;

    friend std::strong_ordering operator<=>(const CandidateRef&, const CandidateRef&) = default;
    friend bool operator==(const CandidateRef&, const CandidateRef&) = default;
};

inline constexpr std::size_t kMaxTraceCandidates = 32;
inline constexpr std::size_t kMaxTraceReasons = 8;
inline constexpr std::size_t kMaxTraceRecords = 256;

struct TraceRecord {
    DecisionCategory category = DecisionCategory::path;
    CandidateOutcome outcome = CandidateOutcome::rejected;
    CandidateRef subject;
    PolicyRule rule = PolicyRule::rank_fallbacks;
    // Most significant first. For a rejection or fallback the first entry is the decisive one;
    // for the selected candidate these are its drawbacks and the limits on its quality.
    std::vector<ReasonCode> reasons;
    std::uint32_t omitted_reasons = 0;

    friend std::strong_ordering operator<=>(const TraceRecord&, const TraceRecord&) = default;
    friend bool operator==(const TraceRecord&, const TraceRecord&) = default;
};

struct CategoryTruncation {
    DecisionCategory category = DecisionCategory::path;
    std::uint32_t omitted = 0;

    friend bool operator==(const CategoryTruncation&, const CategoryTruncation&) = default;
};

// Bounded explanation of one evaluation. Re-evaluation replaces it; history is never appended.
struct DecisionTrace {
    // Grouped by category; ranked candidates in rank order, then rejections in a stable order.
    // At most kMaxTraceCandidates per category and kMaxTraceRecords in total.
    std::vector<TraceRecord> records;
    std::vector<CategoryTruncation> truncated;

    // Whether any candidate that was not selected carries the reason.
    [[nodiscard]] bool has_rejection(ReasonCode reason) const {
        return std::ranges::any_of(records, [reason](const TraceRecord& record) {
            return record.outcome != CandidateOutcome::selected && std::ranges::find(record.reasons, reason) != record.reasons.end();
        });
    }

    friend bool operator==(const DecisionTrace&, const DecisionTrace&) = default;
};

enum class PlanStatus {
    planned,
    no_viable_path,
    invalid_input,
};

// The best intended local plan for one request. It is not an activation guarantee: activation
// attempts the selected candidate and then advances through the fallbacks in order.
struct MediaPlan {
    SchemaVersion schema_version = kSchemaVersion;
    PolicyVersion policy_version = kPolicyVersion;
    PlanStatus status = PlanStatus::invalid_input;
    // Why nothing was selected; empty when planned.
    std::vector<ReasonCode> reasons;
    MediaDecisionRequest request;
    ProfileDecision profile;
    LocalQualityEnvelope envelope;
    std::optional<MediaCandidate> selected;
    // Conservative start: the selected path's limits narrowed to the requested quality.
    StartingQuality start;
    std::vector<Downgrade> downgrades;
    // Ranked alternatives after the selected candidate, at most kMaxTraceCandidates - 1.
    std::vector<MediaCandidate> fallbacks;
    DecisionTrace trace;

    friend bool operator==(const MediaPlan&, const MediaPlan&) = default;
};

} // namespace catro::capabilities

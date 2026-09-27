#include "policy_internal.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace catro::capabilities::detail {
namespace {

// Ranking criteria in rule precedence order. Each holds a penalty where zero is ideal and a
// lower value always ranks first.
enum Criterion : std::size_t {
    kUnknownSupport,  // unknown support facts along the path
    kUnknownLimits,   // unreported mode size and frame-rate limits
    kAffinity,        // display, capture, and encoder GPUs: 0 preserved, 1 unprovable, 2 broken
    kTransfer,        // same resource, same-adapter copy, cross-adapter copy, CPU staging, unknown
    kImplementation,  // hardware, software
    kLatency,         // low-latency evidence for interactive requests
    kDynamicRange,    // SDR where HDR was wanted and achievable
    kCriterionCount,
};

constexpr int kLatencyUnsupported = 4;

struct Scored {
    MediaCandidate candidate;
    std::array<int, kCriterionCount> penalties{};
    // Distinguishes two transfers between the same capture path and encoder.
    std::optional<GpuId> source_gpu;
    // Limits on this path's quality, for the selected record. Sorted, unique.
    std::vector<ReasonCode> limits;
};

int transfer_penalty(TransferKind kind) {
    switch (kind) {
    case TransferKind::same_resource:
        return 0;
    case TransferKind::same_adapter_copy:
        return 1;
    case TransferKind::cross_adapter_copy:
        return 2;
    case TransferKind::cpu_staging:
        return 3;
    case TransferKind::unknown:
        return 4;
    }
    return 4;
}

int latency_penalty(const SupportFact& low_latency, LatencyClass latency) {
    if (latency != LatencyClass::interactive) {
        return 0;
    }
    switch (low_latency.status) {
    case Support::unsupported:
        return kLatencyUnsupported;
    case Support::unknown:
        return 3;
    case Support::supported:
        break;
    }
    switch (low_latency.provenance.method) {
    case EvidenceMethod::measured:
    case EvidenceMethod::probe_validated:
        return 0;
    case EvidenceMethod::advertised:
    case EvidenceMethod::cached:
        return 1;
    case EvidenceMethod::inferred:
        return 2;
    }
    return 2;
}

// Known GPU links must agree; any unknown link leaves affinity unprovable. Software encoders
// have no encoder-side link.
int affinity_penalty(const GpuId* display_gpu, const CapturePathCapability& capture,
                     const TransferPathCapability& transfer, const EncoderCapability& encoder) {
    const GpuId* capture_gpu = known_value(capture.gpu);
    if (capture_gpu == nullptr && transfer.source_gpu) {
        capture_gpu = &*transfer.source_gpu;
    }
    std::vector<const GpuId*> links{display_gpu, capture_gpu};
    if (encoder.implementation == ImplementationClass::hardware) {
        links.push_back(transfer.destination_gpu ? &*transfer.destination_gpu : known_value(encoder.gpu));
    }
    const GpuId* first = nullptr;
    bool complete = true;
    for (const auto* link : links) {
        if (link == nullptr) {
            complete = false;
        } else if (first == nullptr) {
            first = link;
        } else if (!(*link == *first)) {
            return 2;
        }
    }
    return complete ? 0 : 1;
}

// Most widely decodable first; the conservative start prefers it.
int codec_order(Codec codec) {
    switch (codec) {
    case Codec::h264:
        return 0;
    case Codec::hevc:
        return 1;
    case Codec::av1:
        return 2;
    }
    return 2;
}

std::vector<Consequence> consequences(ImplementationClass implementation, TransferKind transfer) {
    std::vector<Consequence> result;
    if (implementation == ImplementationClass::software) {
        result = {Consequence::cpu_load, Consequence::power_draw, Consequence::added_latency, Consequence::limited_quality};
    }
    if (transfer == TransferKind::cross_adapter_copy) {
        result.insert(result.end(), {Consequence::power_draw, Consequence::added_latency});
    }
    if (transfer == TransferKind::cpu_staging) {
        result.insert(result.end(), {Consequence::cpu_load, Consequence::added_latency});
    }
    std::ranges::sort(result);
    const auto duplicates = std::ranges::unique(result);
    result.erase(duplicates.begin(), duplicates.end());
    return result;
}

ReasonCode drawback(std::size_t criterion, int penalty) {
    switch (criterion) {
    case kUnknownSupport:
        return ReasonCode::unknown_support;
    case kUnknownLimits:
        return ReasonCode::unknown_limits;
    case kAffinity:
        return penalty == 1 ? ReasonCode::gpu_affinity_unknown : ReasonCode::gpu_affinity_broken;
    case kTransfer: {
        constexpr std::array codes{ReasonCode::same_adapter_copy, ReasonCode::cross_adapter_transfer,
                                   ReasonCode::cpu_staging_transfer, ReasonCode::unknown_transfer};
        return codes[static_cast<std::size_t>(penalty - 1)];
    }
    case kImplementation:
        return ReasonCode::software_encoder;
    case kLatency:
        return penalty == kLatencyUnsupported ? ReasonCode::low_latency_unsupported : ReasonCode::low_latency_unproven;
    default:
        return ReasonCode::sdr_only;
    }
}

PolicyRule rule_for(ReasonCode reason) {
    switch (reason) {
    case ReasonCode::unsupported_policy_version:
    case ReasonCode::unsupported_schema:
    case ReasonCode::invalid_snapshot:
    case ReasonCode::invalid_request:
        return PolicyRule::reject_invalid_input;
    case ReasonCode::no_source_display:
    case ReasonCode::source_kind_mismatch:
    case ReasonCode::capture_permission_denied:
    case ReasonCode::hdr_mode_not_requested:
        return PolicyRule::hard_request_constraints;
    case ReasonCode::no_capture_path:
    case ReasonCode::no_encoder_mode:
    case ReasonCode::capture_unsupported:
    case ReasonCode::transfer_unsupported:
    case ReasonCode::encoder_unsupported:
    case ReasonCode::mode_unsupported:
    case ReasonCode::no_transfer_path:
    case ReasonCode::below_encoder_minimum:
        return PolicyRule::remove_unsupported;
    case ReasonCode::unknown_support:
    case ReasonCode::unknown_limits:
        return PolicyRule::prefer_known_evidence;
    case ReasonCode::gpu_affinity_unknown:
    case ReasonCode::gpu_affinity_broken:
        return PolicyRule::preserve_gpu_affinity;
    case ReasonCode::same_adapter_copy:
    case ReasonCode::cross_adapter_transfer:
    case ReasonCode::cpu_staging_transfer:
    case ReasonCode::unknown_transfer:
        return PolicyRule::prefer_cheaper_transfer;
    case ReasonCode::software_encoder:
    case ReasonCode::low_latency_unproven:
    case ReasonCode::low_latency_unsupported:
        return PolicyRule::prefer_low_latency_hardware;
    case ReasonCode::limited_by_source:
    case ReasonCode::limited_by_capture:
    case ReasonCode::limited_by_encoder:
    case ReasonCode::limited_by_unknown_limits:
    case ReasonCode::limited_by_profile:
    case ReasonCode::hdr_unavailable:
    case ReasonCode::software_excluded_by_profile:
        return PolicyRule::apply_profile_ceilings;
    case ReasonCode::sdr_only:
    case ReasonCode::lower_quality:
    case ReasonCode::less_conservative_codec:
        return PolicyRule::select_conservative_start;
    case ReasonCode::stable_order:
        return PolicyRule::rank_fallbacks;
    }
    return PolicyRule::rank_fallbacks;
}

std::tuple<std::uint64_t, Rational> quality(const Scored& scored) {
    return {pixels(scored.candidate.resolution), scored.candidate.frame_rate};
}

// Strict total order: penalties in rule precedence, then higher quality, then the more
// conservative codec, then typed identifiers, so inventory order never matters. Validation
// keeps (encoder, mode, capture, source GPU) unique.
bool ranks_before(const Scored& lhs, const Scored& rhs) {
    if (lhs.penalties != rhs.penalties) {
        return lhs.penalties < rhs.penalties;
    }
    if (quality(lhs) != quality(rhs)) {
        return quality(lhs) > quality(rhs);
    }
    const auto& a = lhs.candidate;
    const auto& b = rhs.candidate;
    const int a_codec = codec_order(a.codec);
    const int b_codec = codec_order(b.codec);
    return std::tie(a_codec, a.encoder, a.mode, a.capture, lhs.source_gpu) <
           std::tie(b_codec, b.encoder, b.mode, b.capture, rhs.source_gpu);
}

// Why a fallback ranks after the selected candidate; the first entry is the decisive rule.
std::vector<ReasonCode> demotion(const Scored& fallback, const Scored& selected) {
    std::vector<ReasonCode> reasons;
    for (std::size_t criterion = 0; criterion < kCriterionCount; ++criterion) {
        if (fallback.penalties[criterion] > selected.penalties[criterion]) {
            reasons.push_back(drawback(criterion, fallback.penalties[criterion]));
        }
    }
    if (quality(fallback) < quality(selected)) {
        reasons.push_back(ReasonCode::lower_quality);
    }
    if (codec_order(fallback.candidate.codec) > codec_order(selected.candidate.codec)) {
        reasons.push_back(ReasonCode::less_conservative_codec);
    }
    if (reasons.empty()) {
        reasons.push_back(ReasonCode::stable_order);
    }
    return reasons;
}

// The selected candidate's own drawbacks, then the limits on its quality.
std::vector<ReasonCode> drawbacks(const Scored& selected) {
    std::vector<ReasonCode> reasons;
    for (std::size_t criterion = 0; criterion < kCriterionCount; ++criterion) {
        if (selected.penalties[criterion] > 0) {
            reasons.push_back(drawback(criterion, selected.penalties[criterion]));
        }
    }
    reasons.insert(reasons.end(), selected.limits.begin(), selected.limits.end());
    return reasons;
}

struct PathContext {
    const MediaDecisionRequest& request;
    const GpuId* display_gpu = nullptr;
    bool hdr_wanted = false;
};

Scored score(const PathContext& context, const CapturePathCapability& capture, const TransferPathCapability& transfer,
             const EncoderCapability& encoder, const EncoderModeCapability& mode, QualityPoint point, bool hdr) {
    const auto unknown = [](const SupportFact& fact) { return fact.status == Support::unknown ? 1 : 0; };
    const int unknown_support = unknown(capture.support) + unknown(transfer.evidence) + unknown(encoder.support) + unknown(mode.support);
    const int unknown_limits = (known_value(mode.dimensions) == nullptr ? 1 : 0) + (known_value(mode.frame_rates) == nullptr ? 1 : 0);

    Scored scored;
    scored.penalties = {
        unknown_support,
        unknown_limits,
        affinity_penalty(context.display_gpu, capture, transfer, encoder),
        transfer_penalty(transfer.transfer),
        encoder.implementation == ImplementationClass::software ? 1 : 0,
        latency_penalty(mode.low_latency, context.request.latency),
        context.hdr_wanted && !hdr ? 1 : 0,
    };
    scored.source_gpu = transfer.source_gpu;

    auto& candidate = scored.candidate;
    candidate.capture = capture.id;
    candidate.transfer = transfer.transfer;
    candidate.gpu = transfer.destination_gpu;
    if (const auto* gpu = known_value(encoder.gpu); gpu != nullptr && !candidate.gpu) {
        candidate.gpu = *gpu;
    }
    candidate.encoder = encoder.id;
    candidate.implementation = encoder.implementation;
    candidate.codec = encoder.codec;
    candidate.mode = mode_key(mode);
    candidate.hdr = hdr;
    candidate.resolution = point.resolution;
    candidate.frame_rate = point.frame_rate;
    candidate.confidence = point.degraded || unknown_support > 0 || transfer.transfer == TransferKind::unknown
                               ? Confidence::degraded
                               : Confidence::high;
    candidate.consequences = consequences(encoder.implementation, transfer.transfer);

    scored.limits = std::move(point.reasons);
    std::ranges::sort(scored.limits);
    const auto duplicates = std::ranges::unique(scored.limits);
    scored.limits.erase(duplicates.begin(), duplicates.end());
    return scored;
}

} // namespace

Ranking rank_candidates(const CapabilitySnapshot& snapshot, const MediaDecisionRequest& request,
                        const DisplayState& display, OperatingProfile profile, bool hdr_wanted) {
    Ranking ranking;
    const auto ceiling = ceiling_for(profile);
    const auto& devices = snapshot.devices;
    const auto reject = [&ranking](DecisionCategory category, CandidateRef subject, std::vector<ReasonCode> reasons) {
        const auto rule = rule_for(reasons.front());
        ranking.records.push_back(
            {.category = category, .subject = std::move(subject), .rule = rule, .reasons = std::move(reasons)});
    };

    // Hard request constraints, then known-unsupported capture paths.
    std::vector<const CapturePathCapability*> captures;
    for (const auto& capture : devices.capture_paths) {
        std::vector<ReasonCode> reasons;
        if (capture.source != request.source) {
            reasons.push_back(ReasonCode::source_kind_mismatch);
        }
        if (permission_denied(snapshot, capture.id)) {
            reasons.push_back(ReasonCode::capture_permission_denied);
        }
        if (!usable(capture.support)) {
            reasons.push_back(ReasonCode::capture_unsupported);
        }
        if (reasons.empty()) {
            captures.push_back(&capture);
        } else {
            reject(DecisionCategory::capture, {.capture = capture.id}, std::move(reasons));
        }
    }
    if (captures.empty()) {
        ranking.failure = ReasonCode::no_capture_path;
        return ranking;
    }
    const auto usable_capture = [&captures](const CapturePathId& id) -> const CapturePathCapability* {
        const auto found = std::ranges::find(captures, id, &CapturePathCapability::id);
        return found != captures.end() ? *found : nullptr;
    };

    const auto display_capability = std::ranges::find(devices.displays, display.display, &DisplayCapability::id);
    const PathContext context{
        .request = request,
        .display_gpu = display_capability != devices.displays.end() ? known_value(display_capability->gpu) : nullptr,
        .hdr_wanted = hdr_wanted,
    };

    std::vector<Scored> scored;
    for (const auto& encoder : devices.encoders) {
        std::vector<const TransferPathCapability*> transfers;
        for (const auto& transfer : devices.transfer_paths) {
            if (transfer.destination == encoder.id && usable_capture(transfer.source) != nullptr) {
                transfers.push_back(&transfer);
            }
        }

        std::vector<ReasonCode> reasons;
        if (!usable(encoder.support)) {
            reasons.push_back(ReasonCode::encoder_unsupported);
        }
        // The safe envelope never silently moves encoding onto the CPU.
        if (encoder.implementation == ImplementationClass::software && profile == OperatingProfile::safe_local_envelope) {
            reasons.push_back(ReasonCode::software_excluded_by_profile);
        }
        if (transfers.empty()) {
            reasons.push_back(ReasonCode::no_transfer_path);
        }
        if (!reasons.empty()) {
            reject(DecisionCategory::encoder, {.encoder = encoder.id}, std::move(reasons));
            continue;
        }

        std::vector<const EncoderModeCapability*> modes;
        for (const auto& mode : encoder.modes) {
            std::vector<ReasonCode> mode_reasons;
            if (!usable(mode.support)) {
                mode_reasons.push_back(ReasonCode::mode_unsupported);
            }
            if (const auto* hdr = known_value(mode.hdr); hdr != nullptr && *hdr != HdrMode::sdr && !hdr_wanted) {
                mode_reasons.push_back(request.quality.hdr ? ReasonCode::hdr_unavailable : ReasonCode::hdr_mode_not_requested);
            }
            if (mode_reasons.empty()) {
                modes.push_back(&mode);
            } else {
                reject(DecisionCategory::mode, {.encoder = encoder.id, .mode = mode_key(mode)}, std::move(mode_reasons));
            }
        }

        for (const auto* transfer : transfers) {
            const auto& capture = *usable_capture(transfer->source);
            if (!usable(transfer->evidence)) {
                reject(DecisionCategory::transfer, {.capture = capture.id, .encoder = encoder.id},
                       {ReasonCode::transfer_unsupported});
                continue;
            }
            for (const auto* mode : modes) {
                const CandidateRef path{.capture = capture.id, .encoder = encoder.id, .mode = mode_key(*mode)};
                auto point = quality_point(display, capture, *mode, ceiling);
                if (!point) {
                    reject(DecisionCategory::path, path, {ReasonCode::below_encoder_minimum});
                    continue;
                }
                // An HDR mode survives mode checks only when HDR is wanted; this path must keep it.
                const bool hdr = hdr_wanted && hdr_mode(*mode) && preserves_hdr(capture, *transfer);
                if (const auto* mode_hdr = known_value(mode->hdr); mode_hdr != nullptr && *mode_hdr != HdrMode::sdr && !hdr) {
                    reject(DecisionCategory::path, path, {ReasonCode::hdr_unavailable});
                    continue;
                }
                scored.push_back(score(context, capture, *transfer, encoder, *mode, std::move(*point), hdr));
            }
        }
    }
    if (scored.empty()) {
        ranking.failure = ReasonCode::no_encoder_mode;
        return ranking;
    }

    std::ranges::sort(scored, ranks_before);
    const auto& selected = scored.front();
    for (const auto& entry : scored) {
        const bool is_selected = &entry == &selected;
        TraceRecord record{
            .category = DecisionCategory::path,
            .outcome = is_selected ? CandidateOutcome::selected : CandidateOutcome::fallback,
            .subject = {.capture = entry.candidate.capture, .encoder = entry.candidate.encoder, .mode = entry.candidate.mode},
            .reasons = is_selected ? drawbacks(entry) : demotion(entry, selected),
        };
        record.rule = is_selected ? PolicyRule::select_conservative_start : rule_for(record.reasons.front());
        ranking.records.push_back(std::move(record));
        ranking.ranked.push_back(entry.candidate);
    }
    return ranking;
}

} // namespace catro::capabilities::detail

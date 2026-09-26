#include "policy_internal.hpp"

#include <catro/capabilities/validation.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <utility>
#include <vector>

namespace catro::capabilities {
namespace {

using namespace detail;

static_assert(kDecisionCategoryCount * kMaxTraceCandidates <= kMaxTraceRecords,
              "per-category candidate bounds must keep the whole trace within its record bound");

std::vector<ReasonCode> input_problems(const CapabilitySnapshot& snapshot, const MediaDecisionRequest& request,
                                       PolicyVersion policy_version) {
    std::vector<ReasonCode> problems;
    if (!is_supported(policy_version)) {
        problems.push_back(ReasonCode::unsupported_policy_version);
    }
    if (const auto report = validate(snapshot); !report.ok()) {
        problems.push_back(report.contains(ValidationCode::unsupported_schema) ? ReasonCode::unsupported_schema
                                                                               : ReasonCode::invalid_snapshot);
    }
    const auto& quality = request.quality;
    if (quality.resolution.width == 0 || quality.resolution.height == 0 || !quality.frame_rate.valid() ||
        quality.frame_rate.numerator() == 0) {
        problems.push_back(ReasonCode::invalid_request);
    }
    return problems;
}

// Groups records by category with ranked candidates first in rank order and rejections after
// them in a total order, then applies the per-category and per-record bounds. Truncation keeps
// the head of each group, so the selected candidate and every retained record's decisive
// reason survive.
DecisionTrace bound_trace(std::vector<TraceRecord> records) {
    std::ranges::stable_sort(records, [](const TraceRecord& lhs, const TraceRecord& rhs) {
        if (lhs.category != rhs.category) {
            return lhs.category < rhs.category;
        }
        const bool lhs_rejected = lhs.outcome == CandidateOutcome::rejected;
        const bool rhs_rejected = rhs.outcome == CandidateOutcome::rejected;
        if (lhs_rejected != rhs_rejected) {
            return rhs_rejected;
        }
        return lhs_rejected && lhs < rhs;
    });

    DecisionTrace trace;
    for (auto begin = records.begin(); begin != records.end();) {
        const auto category = begin->category;
        const auto end = std::find_if(begin, records.end(),
                                      [category](const TraceRecord& record) { return record.category != category; });
        const auto count = static_cast<std::size_t>(std::distance(begin, end));
        const auto kept = std::min(count, kMaxTraceCandidates);
        for (auto record = begin; record != begin + static_cast<std::ptrdiff_t>(kept); ++record) {
            if (record->reasons.size() > kMaxTraceReasons) {
                record->omitted_reasons = static_cast<std::uint32_t>(record->reasons.size() - kMaxTraceReasons);
                record->reasons.resize(kMaxTraceReasons);
            }
            trace.records.push_back(std::move(*record));
        }
        if (count > kept) {
            trace.truncated.push_back({category, static_cast<std::uint32_t>(count - kept)});
        }
        begin = end;
    }
    return trace;
}

// Narrows quality to a ceiling without ever raising it.
StartingQuality within(StartingQuality quality, const Ceiling& ceiling) {
    quality.resolution = fit(quality.resolution, oriented(ceiling.box, quality.resolution));
    quality.frame_rate = std::min(quality.frame_rate, ceiling.frame_rate);
    quality.hdr = quality.hdr && ceiling.hdr;
    quality.bit_depth = quality.hdr ? 10 : 8;
    return quality;
}

} // namespace

MediaPlan derive_media_plan(const CapabilitySnapshot& snapshot, const MediaDecisionRequest& request,
                            PolicyVersion policy_version) {
    MediaPlan plan;
    plan.policy_version = policy_version;
    plan.request = request;
    if (auto problems = input_problems(snapshot, request, policy_version); !problems.empty()) {
        plan.trace = bound_trace(
            {TraceRecord{.category = DecisionCategory::input, .rule = PolicyRule::reject_invalid_input, .reasons = problems}});
        plan.reasons = std::move(problems);
        return plan;
    }

    plan.status = PlanStatus::no_viable_path;
    plan.profile = derive_operating_profile(snapshot, request.preference);
    plan.envelope = derive_local_envelope(snapshot, request, plan.profile.profile);
    const auto* display = source_display(snapshot, request);
    if (display == nullptr) {
        plan.reasons = {ReasonCode::no_source_display};
        return plan;
    }

    auto ranking = rank_candidates(snapshot, request, *display, plan.profile.profile, request.quality.hdr && plan.envelope.hdr);
    plan.trace = bound_trace(std::move(ranking.records));
    if (ranking.ranked.empty()) {
        plan.reasons = {ranking.failure};
        return plan;
    }

    plan.status = PlanStatus::planned;
    const auto retained = static_cast<std::ptrdiff_t>(std::min(ranking.ranked.size(), kMaxTraceCandidates));
    plan.fallbacks.assign(std::make_move_iterator(ranking.ranked.begin() + 1),
                          std::make_move_iterator(ranking.ranked.begin() + retained));
    const auto& selected = ranking.ranked.front();
    const auto& requested = request.quality;
    plan.start = within({selected.resolution, selected.frame_rate, 8, selected.hdr},
                        Ceiling{requested.resolution, requested.frame_rate, requested.hdr});
    plan.downgrades = {
        {DowngradeTrigger::battery_power, OperatingProfile::efficiency,
         within(plan.start, ceiling_for(OperatingProfile::efficiency))},
        {DowngradeTrigger::thermal_pressure, OperatingProfile::thermal_constrained,
         within(plan.start, ceiling_for(OperatingProfile::thermal_constrained))},
    };
    plan.selected = selected;
    return plan;
}

} // namespace catro::capabilities

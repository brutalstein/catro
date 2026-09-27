#include "DiagnosticsViewModel.hpp"

#include <catro/reporting/canonical_json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <string_view>

namespace catro::app {
namespace {

namespace caps = catro::capabilities;
using reporting::PresentedRow;
using reporting::PresentedSection;

struct SectionLayout {
    std::string_view id;
    std::string_view title;
    // Unused trailing slots stay empty.
    std::array<std::string_view, 5> keys;
};

// Plan first: what Catro intends to do and why, then the evidence behind it.
const std::array kLayout{
    SectionLayout{"plan", "Intended plan",
                  {"plan.status", "plan.reasons", "plan.selected", "plan.start", "plan.downgrades"}},
    SectionLayout{"fallbacks", "Fallback order", {"plan.fallbacks"}},
    SectionLayout{"decisions", "Decisions and rejections", {"plan.trace"}},
    SectionLayout{"profile", "Profile and local envelope",
                  {"plan.profile", "plan.envelope", "plan.request", "plan.policy_version", "plan.schema_version"}},
    SectionLayout{"probes", "Probe health", {"snapshot.probes", "snapshot.issues"}},
    SectionLayout{"devices", "Devices and topology", {"snapshot.devices"}},
    SectionLayout{"system", "System", {"snapshot.platform", "snapshot.hardware"}},
    SectionLayout{"runtime", "Runtime state", {"snapshot.runtime"}},
    SectionLayout{"snapshot", "Snapshot", {"snapshot.header"}},
};

std::string_view outcome_name(caps::ProbeOutcome outcome) {
    switch (outcome) {
    case caps::ProbeOutcome::success:
        return "success";
    case caps::ProbeOutcome::partial:
        return "partial";
    case caps::ProbeOutcome::api_unavailable:
        return "API unavailable";
    case caps::ProbeOutcome::permission_unavailable:
        return "permission unavailable";
    case caps::ProbeOutcome::timeout:
        return "timed out";
    case caps::ProbeOutcome::os_failure:
        return "OS failure";
    case caps::ProbeOutcome::malformed_output:
        return "rejected output";
    case caps::ProbeOutcome::helper_terminated:
        return "helper terminated";
    }
    return "unknown";
}

Tone outcome_tone(caps::ProbeOutcome outcome) {
    switch (outcome) {
    case caps::ProbeOutcome::success:
        return Tone::positive;
    case caps::ProbeOutcome::partial:
        return Tone::caution;
    default:
        return Tone::critical;
    }
}

constexpr std::array<std::string_view, caps::kChangeDomainCount> kDomainNames{
    "gpu",   "encoder", "display",         "capture", "audio input", "audio output", "capture permission",
    "power", "thermal", "memory pressure", "session", "validation",
};

// Milliseconds with one decimal, computed on integers so no locale can change the separator.
std::string duration_text(std::chrono::microseconds duration) {
    const auto tenths = (duration.count() + 50) / 100;
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " ms";
}

std::string value_of(const PresentedSection& summary, std::string_view label) {
    const auto found = std::ranges::find(summary.rows, label, &PresentedRow::label);
    return found != summary.rows.end() && found->value ? *found->value : std::string{};
}

DiagnosticsSection section(const SectionLayout& layout, const std::vector<PresentedSection>& presented) {
    DiagnosticsSection result{std::string(layout.id), std::string(layout.title), {}, 0};
    const bool several = !layout.keys[1].empty();
    for (const auto key : layout.keys) {
        if (key.empty()) {
            continue;
        }
        const auto found = std::ranges::find(presented, key, &PresentedSection::key);
        if (found == presented.end()) {
            continue;
        }
        // A block field inside a combined section keeps its name as a heading.
        const bool heading = several && !(found->rows.size() == 1 && found->rows.front().value);
        if (heading) {
            result.rows.push_back({0, std::string(key.substr(key.find('.') + 1)), {}, reporting::FactState::plain, false});
        }
        const std::uint32_t indent = heading ? 1 : 0;
        for (const auto& row : found->rows) {
            result.rows.push_back(
                {row.depth + indent, row.label, row.value.value_or(std::string{}), row.state, row.list_item});
        }
    }
    if (result.rows.size() > kMaxSectionRows) {
        result.omitted_rows = result.rows.size() - kMaxSectionRows;
        result.rows.resize(kMaxSectionRows);
    }
    return result;
}

std::vector<std::string> change_lines(const caps::ChangeSet& changes) {
    std::vector<std::string> lines;
    for (std::size_t index = 0; index < caps::kChangeDomainCount; ++index) {
        if (changes.contains(static_cast<caps::ChangeDomain>(index))) {
            lines.emplace_back(kDomainNames[index]);
        }
    }
    const auto add = [&lines](std::string_view kind, const auto& ids) {
        for (const auto& id : ids) {
            lines.push_back(std::string(kind) + ": " + id.value);
        }
    };
    add("gpu", changes.gpus);
    add("encoder", changes.encoders);
    add("display", changes.displays);
    add("capture", changes.capture_paths);
    add("audio", changes.audio_endpoints);
    return lines;
}

} // namespace

DiagnosticsModel build_diagnostics(const reporting::CapabilityReport& report, const caps::ChangeSet& changes) {
    const auto presented = reporting::present_report(report, reporting::RedactionMode::device_names);
    const auto& summary = presented.front();
    DiagnosticsModel model;
    model.generation = report.snapshot.header.generation;

    switch (report.plan.status) {
    case caps::PlanStatus::planned:
        model.headline = "Planned";
        model.detail = value_of(summary, "Selected") + "\nStart: " + value_of(summary, "Start");
        model.tone = Tone::positive;
        break;
    case caps::PlanStatus::no_viable_path:
        model.headline = "No viable path";
        model.detail = value_of(summary, "Status");
        model.tone = Tone::critical;
        break;
    case caps::PlanStatus::invalid_input:
        model.headline = "Invalid input";
        model.detail = value_of(summary, "Status");
        model.tone = Tone::critical;
        break;
    }

    for (const auto& record : report.snapshot.probes) {
        model.probes.push_back({record.probe_id, std::string(outcome_name(record.outcome)),
                                duration_text(record.duration), record.fact_count, outcome_tone(record.outcome)});
        if (model.tone == Tone::positive && record.outcome != caps::ProbeOutcome::success) {
            model.tone = Tone::caution;
        }
    }
    std::ranges::sort(model.probes, {}, &ProbeHealth::probe_id);

    // The first publication diffs against nothing, so every domain reads as changed.
    if (model.generation > 1) {
        model.changes = change_lines(changes);
    }
    for (const auto& layout : kLayout) {
        model.sections.push_back(section(layout, presented));
    }
    return model;
}

std::string export_report(const reporting::CapabilityReport& report, ExportFormat format) {
    return format == ExportFormat::json ? reporting::to_canonical_json(report)
                                        : reporting::to_human_report(report, reporting::RedactionMode::device_names);
}

} // namespace catro::app

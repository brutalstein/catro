#pragma once

#include <catro/reporting/report.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace catro::reporting {

enum class RedactionMode {
    none,
    // Device display names are replaced; identifiers stay so reports remain diagnosable.
    device_names,
};

// Engineering diagnostics as plain text: a plan summary, then every snapshot fact and plan
// decision with its knowledge state. Redaction is stated in the report and never implies
// anonymity. Locale-independent and deterministic.
[[nodiscard]] std::string to_human_report(const CapabilityReport& report,
                                          RedactionMode redaction = RedactionMode::device_names);

// How certain a presented value is. Plain rows are structure or plan data, not evidence.
enum class FactState {
    plain,
    known,
    degraded,
    unknown,
    unavailable,
};

// One line of the human report, kept structured for native diagnostics views.
struct PresentedRow {
    std::uint32_t depth = 0;
    std::string label;
    // Absent on a row that heads nested rows.
    std::optional<std::string> value;
    FactState state = FactState::plain;
    // The first row of a list entry.
    bool list_item = false;

    friend bool operator==(const PresentedRow&, const PresentedRow&) = default;
};

struct PresentedSection {
    // "summary", or the dotted path of a top-level field such as "snapshot.devices" or "plan.trace".
    std::string key;
    std::vector<PresentedRow> rows;

    friend bool operator==(const PresentedSection&, const PresentedSection&) = default;
};

// The human report's rows and values, one section for the summary and one per top-level
// snapshot or plan field, each re-based to depth 0. Same order, redaction, and determinism.
[[nodiscard]] std::vector<PresentedSection> present_report(const CapabilityReport& report,
                                                           RedactionMode redaction = RedactionMode::device_names);

} // namespace catro::reporting

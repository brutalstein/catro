#pragma once

#include <catro/capabilities/snapshot_diff.hpp>
#include <catro/reporting/human_report.hpp>
#include <catro/reporting/report.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Plain C++ presentation of a capability report for the native diagnostics workspace. No COM,
// WinRT, or XAML types: the shell maps these records onto controls and tests read them directly.
namespace catro::app {

// Semantic status; the shell maps each tone to a theme color and an accessible label.
enum class Tone {
    neutral,
    positive,
    caution,
    critical,
};

struct DiagnosticsRow {
    std::uint32_t depth = 0;
    std::string label;
    // Empty on a heading row.
    std::string value;
    reporting::FactState state = reporting::FactState::plain;
    bool list_item = false;

    friend bool operator==(const DiagnosticsRow&, const DiagnosticsRow&) = default;
};

struct DiagnosticsSection {
    std::string id;
    std::string title;
    std::vector<DiagnosticsRow> rows;
    // Rows past the presentation bound; the exported report still holds every one.
    std::size_t omitted_rows = 0;

    friend bool operator==(const DiagnosticsSection&, const DiagnosticsSection&) = default;
};

struct ProbeHealth {
    std::string probe_id;
    std::string outcome;
    std::string duration;
    std::uint32_t facts = 0;
    Tone tone = Tone::neutral;

    friend bool operator==(const ProbeHealth&, const ProbeHealth&) = default;
};

struct DiagnosticsModel {
    std::string headline;
    std::string detail;
    Tone tone = Tone::neutral;
    std::uint64_t generation = 0;
    std::vector<ProbeHealth> probes;
    // Classified changes of the latest refresh: changed domains, then changed identifiers as
    // "kind: identifier". Empty on the first publication.
    std::vector<std::string> changes;
    // Fixed order: plan, fallbacks, decisions, profile, probes, devices, system, runtime, snapshot.
    std::vector<DiagnosticsSection> sections;

    friend bool operator==(const DiagnosticsModel&, const DiagnosticsModel&) = default;
};

// Keeps a pathological trace from producing an unusable view; export is never bounded.
inline constexpr std::size_t kMaxSectionRows = 600;

// Device names are redacted on screen as in the default export.
[[nodiscard]] DiagnosticsModel build_diagnostics(const reporting::CapabilityReport& report,
                                                 const capabilities::ChangeSet& changes);

enum class ExportFormat {
    human,
    json,
};

// Human export redacts device names; JSON is the complete canonical report.
[[nodiscard]] std::string export_report(const reporting::CapabilityReport& report, ExportFormat format);

} // namespace catro::app

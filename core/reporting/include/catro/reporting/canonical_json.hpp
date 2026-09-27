#pragma once

#include <catro/capabilities/probe.hpp>
#include <catro/capabilities/validation.hpp>
#include <catro/reporting/report.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace catro::reporting {

// Canonical reports larger than this are rejected unparsed.
inline constexpr std::size_t kMaxReportBytes = 1024 * 1024;
inline constexpr std::size_t kMaxProbeFragmentBytes = 1024 * 1024;

// Deterministic UTF-8 JSON with LF line endings: fixed field order, stable enum names, explicit
// knowledge and provenance on every fact, reduced rationals, integer quantities with declared
// units, unordered inventories and sets sorted, and plan data kept in rank order. The
// serializer reads neither the clock nor the locale, and the report is never redacted.
[[nodiscard]] std::string to_canonical_json(const CapabilityReport& report);

enum class ReportErrorCode {
    input_too_large,
    malformed_json,
    duplicate_key,
    unsupported_schema,
    missing_field,
    unexpected_field,
    invalid_type,
    invalid_enum,
    invalid_value,
    invalid_snapshot,
};

struct ReportParseError {
    ReportErrorCode code = ReportErrorCode::malformed_json;
    // Dotted JSON path of the offending value; empty for the whole document.
    std::string path;

    friend bool operator==(const ReportParseError&, const ReportParseError&) = default;
};

struct ReportParseResult {
    std::optional<CapabilityReport> report;
    std::optional<ReportParseError> error;
    // Findings when the snapshot parsed but failed structural validation.
    capabilities::ValidationReport validation;

    [[nodiscard]] bool ok() const { return report.has_value(); }
};

// Parses untrusted canonical JSON. Oversized input, malformed or duplicate-key JSON,
// incompatible schemas, unknown or missing fields, invalid enums, non-canonical values, and
// snapshots that fail validation are all rejected before any report is returned.
[[nodiscard]] ReportParseResult parse_report(std::string_view text);

// Cross-process probe transport. The C++ fragment remains authoritative; this is a strict,
// size-bounded representation used only by isolated helper processes.
[[nodiscard]] std::string to_canonical_json(const capabilities::ProbeFragment& fragment);

struct FragmentParseResult {
    std::optional<capabilities::ProbeFragment> fragment;
    std::optional<ReportParseError> error;

    [[nodiscard]] bool ok() const { return fragment.has_value(); }
};

[[nodiscard]] FragmentParseResult parse_probe_fragment(std::string_view text);

} // namespace catro::reporting

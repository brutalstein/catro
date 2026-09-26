#pragma once

#include <catro/reporting/report.hpp>

#include <string>

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

} // namespace catro::reporting

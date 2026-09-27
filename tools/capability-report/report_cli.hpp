#pragma once

#include <catro/reporting/report.hpp>

#include <filesystem>
#include <functional>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>

// The platform-independent half of catro-capability-report: argument parsing, rendering, and
// the no-replace file write. The platform entry point supplies only snapshot collection.
namespace catro::tools {

enum class ReportFormat {
    human,
    json,
};

struct ReportOptions {
    ReportFormat format = ReportFormat::human;
    std::optional<std::filesystem::path> output;

    friend bool operator==(const ReportOptions&, const ReportOptions&) = default;
};

inline constexpr std::string_view kReportUsage =
    "usage: catro-capability-report [--format human|json] [--output <path>]\n"
    "  human  engineering report with device names redacted (default)\n"
    "  json   canonical JSON; complete and unredacted\n"
    "  --output writes a new file and refuses to replace an existing one\n";

enum ReportExit : int {
    report_ok = 0,
    report_invalid_arguments = 2,
    report_collection_failed = 3,
    report_write_failed = 4,
};

// UTF-8 arguments without the program name. Absent on unknown, repeated, or incomplete options.
[[nodiscard]] std::optional<ReportOptions> parse_report_arguments(std::span<const std::string_view> arguments);

[[nodiscard]] std::string render_report(const reporting::CapabilityReport& report, ReportFormat format);

// Writes a temporary sibling, then moves it into place only if the target does not exist; the
// target is never replaced or partially written. Returns an error message on failure.
[[nodiscard]] std::optional<std::string> write_new_file(const std::filesystem::path& target,
                                                        std::string_view contents);

// Collection returns no report when no snapshot could be published.
using ReportCollector = std::function<std::optional<reporting::CapabilityReport>()>;

[[nodiscard]] int run_report_cli(std::span<const std::string_view> arguments, const ReportCollector& collect,
                                 std::ostream& out, std::ostream& error);

} // namespace catro::tools

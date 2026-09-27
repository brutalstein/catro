#include "report_cli.hpp"

#include <catro/reporting/canonical_json.hpp>
#include <catro/reporting/human_report.hpp>

#include <fstream>
#include <random>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#elif defined(__APPLE__)
#include <stdio.h>
#else
#include <unistd.h>
#endif

namespace catro::tools {
namespace {

std::string display(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

std::filesystem::path temporary_sibling(const std::filesystem::path& target) {
    auto temporary = target;
    temporary += "." + std::to_string(std::random_device{}()) + ".tmp";
    return temporary;
}

// Atomic rename that fails instead of replacing an existing target.
bool move_without_replacing(const std::filesystem::path& from, const std::filesystem::path& to) {
#ifdef _WIN32
    return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
#elif defined(__APPLE__)
    return renamex_np(from.c_str(), to.c_str(), RENAME_EXCL) == 0;
#else
    // A hard link fails on an existing target; the caller removes the temporary name.
    return link(from.c_str(), to.c_str()) == 0;
#endif
}

} // namespace

std::optional<ReportOptions> parse_report_arguments(std::span<const std::string_view> arguments) {
    ReportOptions options;
    bool format_seen = false;
    for (std::size_t index = 0; index < arguments.size(); index += 2) {
        if (index + 1 == arguments.size()) {
            return std::nullopt;
        }
        const auto option = arguments[index];
        const auto value = arguments[index + 1];
        if (option == "--format" && !format_seen) {
            format_seen = true;
            if (value == "human") {
                options.format = ReportFormat::human;
            } else if (value == "json") {
                options.format = ReportFormat::json;
            } else {
                return std::nullopt;
            }
        } else if (option == "--output" && !options.output && !value.empty()) {
            options.output = std::filesystem::path(std::u8string(value.begin(), value.end()));
        } else {
            return std::nullopt;
        }
    }
    return options;
}

std::string render_report(const reporting::CapabilityReport& report, ReportFormat format) {
    return format == ReportFormat::json ? reporting::to_canonical_json(report)
                                        : reporting::to_human_report(report, reporting::RedactionMode::device_names);
}

std::optional<std::string> write_new_file(const std::filesystem::path& target, std::string_view contents) {
    std::error_code status;
    if (std::filesystem::exists(target, status) || status) {
        return "output already exists: " + display(target);
    }
    const auto temporary = temporary_sibling(target);
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        file.flush();
        if (!file) {
            file.close();
            std::filesystem::remove(temporary, status);
            return "cannot write a temporary file next to " + display(target);
        }
    }
    const bool moved = move_without_replacing(temporary, target);
    // Only the temporary name is ever removed; after a rename it is already gone.
    std::filesystem::remove(temporary, status);
    if (!moved) {
        return "cannot create output (it may already exist): " + display(target);
    }
    return std::nullopt;
}

int run_report_cli(std::span<const std::string_view> arguments, const ReportCollector& collect, std::ostream& out,
                   std::ostream& error) {
    const auto options = parse_report_arguments(arguments);
    if (!options) {
        error << kReportUsage;
        return report_invalid_arguments;
    }
    const auto report = collect();
    if (!report) {
        error << "no capability snapshot was published\n";
        return report_collection_failed;
    }
    const auto text = render_report(*report, options->format);
    if (!options->output) {
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        return out ? report_ok : report_write_failed;
    }
    if (const auto failure = write_new_file(*options->output, text)) {
        error << *failure << '\n';
        return report_write_failed;
    }
    return report_ok;
}

} // namespace catro::tools

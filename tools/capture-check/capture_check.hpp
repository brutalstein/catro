#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace catro::tools {

struct CaptureCheckOptions {
    std::chrono::seconds duration{10};
    // Print the share picker's sources and the backend each one would use, then exit.
    bool list = false;
    // Capture the first source whose title or process name contains this text, through the
    // backend the app picks for it. Empty captures the primary display.
    std::string source;
};

inline constexpr std::string_view kCaptureCheckUsage =
    "usage: catro-capture-check [--seconds 1-60] [--list] [--source TEXT]\n"
    "  captures the primary display, or with --source the matching window or display, the way\n"
    "  the share picker would; --list prints the picker's sources and backends\n"
    "  pixels stay on the GPU; the tool only consumes D3D11 texture handles and metadata\n";

[[nodiscard]] std::optional<CaptureCheckOptions> parse_capture_check_arguments(
    std::span<const std::string_view> arguments);

} // namespace catro::tools

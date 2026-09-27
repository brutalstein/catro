#pragma once

#include <chrono>
#include <optional>
#include <span>
#include <string_view>

namespace catro::tools {

struct CaptureCheckOptions {
    std::chrono::seconds duration{10};
};

inline constexpr std::string_view kCaptureCheckUsage =
    "usage: catro-capture-check [--seconds 1-60]\n"
    "  captures the primary display through Windows Graphics Capture\n"
    "  pixels stay on the GPU; the tool only consumes D3D11 texture handles and metadata\n";

[[nodiscard]] std::optional<CaptureCheckOptions> parse_capture_check_arguments(
    std::span<const std::string_view> arguments);

} // namespace catro::tools

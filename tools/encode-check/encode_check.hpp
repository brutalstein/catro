#pragma once

#include <catro/platform/windows/video_encoder.hpp>

#include <chrono>
#include <optional>
#include <span>
#include <string_view>

namespace catro::tools {

struct EncodeCheckOptions {
    std::chrono::seconds duration{10};
    platform::windows::HardwareEncoderConfig encoder;
};

inline constexpr std::string_view kEncodeCheckUsage =
    "usage: catro-encode-check [--seconds 1-60] [--width even] [--height even] "
    "[--fps 1-120] [--bitrate 128000-50000000]\n"
    "  default: 10s, 1728x1080, 30 fps, 6000000 bit/s\n"
    "  path: WGC BGRA8 GPU texture -> D3D11 video processor -> NV12 -> same-adapter H.264 hardware MFT\n";

[[nodiscard]] std::optional<EncodeCheckOptions> parse_encode_check_arguments(
    std::span<const std::string_view> arguments);

} // namespace catro::tools

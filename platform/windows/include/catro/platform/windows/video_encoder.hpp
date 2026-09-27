#pragma once

#include <catro/platform/windows/screen_capture.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace catro::platform::windows {

enum class HardwareEncoderErrorCode : std::uint8_t {
    invalid_config,
    media_foundation_startup_failed,
    adapter_mismatch,
    video_processor_unavailable,
    conversion_surface_failed,
    hardware_encoder_not_found,
    hardware_encoder_activation_failed,
    hardware_encoder_not_d3d11,
    media_type_failed,
    stream_start_failed,
    input_failed,
    output_failed,
    output_timeout,
};

struct HardwareEncoderError {
    HardwareEncoderErrorCode code = HardwareEncoderErrorCode::hardware_encoder_activation_failed;
    std::int64_t native_code = 0;

    friend bool operator==(const HardwareEncoderError&, const HardwareEncoderError&) = default;
};

[[nodiscard]] constexpr const char* name(HardwareEncoderErrorCode code) noexcept {
    switch (code) {
    case HardwareEncoderErrorCode::invalid_config:
        return "invalid encoder configuration";
    case HardwareEncoderErrorCode::media_foundation_startup_failed:
        return "Media Foundation startup failed";
    case HardwareEncoderErrorCode::adapter_mismatch:
        return "capture texture adapter does not match requested encoder adapter";
    case HardwareEncoderErrorCode::video_processor_unavailable:
        return "D3D11 video processor cannot convert BGRA to NV12";
    case HardwareEncoderErrorCode::conversion_surface_failed:
        return "GPU NV12 conversion surface creation failed";
    case HardwareEncoderErrorCode::hardware_encoder_not_found:
        return "same-adapter H.264 hardware encoder was not found";
    case HardwareEncoderErrorCode::hardware_encoder_activation_failed:
        return "H.264 hardware encoder activation failed";
    case HardwareEncoderErrorCode::hardware_encoder_not_d3d11:
        return "H.264 hardware encoder is not D3D11-aware";
    case HardwareEncoderErrorCode::media_type_failed:
        return "H.264 encoder media-type negotiation failed";
    case HardwareEncoderErrorCode::stream_start_failed:
        return "H.264 encoder stream start failed";
    case HardwareEncoderErrorCode::input_failed:
        return "H.264 encoder rejected GPU input";
    case HardwareEncoderErrorCode::output_failed:
        return "H.264 encoder output failed";
    case HardwareEncoderErrorCode::output_timeout:
        return "H.264 encoder output timed out";
    }
    return "hardware encoder failure";
}

struct HardwareEncoderConfig {
    std::uint32_t width = 1728;
    std::uint32_t height = 1080;
    std::uint32_t frame_rate_numerator = 30;
    std::uint32_t frame_rate_denominator = 1;
    std::uint32_t bitrate = 6'000'000;
    std::uint32_t gop_frames = 60;
    std::optional<std::uint64_t> adapter_luid;
};

struct EncodedAccessUnit {
    std::vector<std::byte> bytes;
    std::uint64_t sequence = 0;
    std::int64_t pts_100ns = 0;
    std::int64_t duration_100ns = 0;
    bool keyframe = false;
};

struct HardwareEncoderStatistics {
    std::uint64_t adapter_luid = 0;
    std::string encoder_name;
    bool asynchronous = false;
    bool d3d11_aware = false;
    bool low_latency_requested = false;
    bool low_latency_applied = false;
    bool explicit_bt709_conversion = false;

    std::uint64_t frames_submitted = 0;
    std::uint64_t frames_encoded = 0;
    std::uint64_t encoded_bytes = 0;
    std::uint64_t keyframes = 0;
    std::uint64_t conversion_failures = 0;
    std::uint64_t input_failures = 0;
    std::uint64_t output_failures = 0;
    std::uint64_t output_timeouts = 0;

    std::uint64_t conversion_total_us = 0;
    std::uint64_t conversion_max_us = 0;
    std::uint64_t encode_total_us = 0;
    std::uint64_t encode_max_us = 0;
};

// Same-adapter SDR H.264 path:
//
//   WGC BGRA8 texture -> D3D11 VideoProcessor -> NV12 texture -> hardware Media Foundation MFT
//
// No uncompressed frame is mapped or copied through CPU memory. Only the compressed access unit is
// copied out of Media Foundation so the next transport slice can packetize it.
//
// This object is intentionally single-worker-thread. It serializes one frame in flight to bound
// latency and prevent the driver from building an unbounded encode queue.
class WindowsH264HardwareEncoder final {
public:
    WindowsH264HardwareEncoder();
    ~WindowsH264HardwareEncoder();

    WindowsH264HardwareEncoder(const WindowsH264HardwareEncoder&) = delete;
    WindowsH264HardwareEncoder& operator=(const WindowsH264HardwareEncoder&) = delete;

    [[nodiscard]] std::optional<HardwareEncoderError> start(
        const HardwareEncoderConfig& config, ID3D11Texture2D& first_source);

    [[nodiscard]] std::optional<HardwareEncoderError> encode(
        const GpuCaptureFrame& source, EncodedAccessUnit& output);

    void stop() noexcept;

    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] HardwareEncoderStatistics statistics() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace catro::platform::windows

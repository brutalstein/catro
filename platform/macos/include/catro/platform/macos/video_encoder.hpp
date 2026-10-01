#pragma once

#include <catro/platform/macos/pixel_buffer.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace catro::platform::macos {

enum class H264EncoderErrorCode : std::uint8_t {
    invalid_config,
    session_creation_failed,
    // Production requires the hardware encoder; a software-only session is refused, never hidden.
    hardware_unavailable,
    property_failed,
    invalid_source,
    encode_failed,
    output_too_large,
};

struct H264EncoderError {
    H264EncoderErrorCode code = H264EncoderErrorCode::session_creation_failed;
    std::int64_t native_code = 0;

    friend bool operator==(const H264EncoderError&, const H264EncoderError&) = default;
};

[[nodiscard]] constexpr const char* name(H264EncoderErrorCode code) noexcept {
    switch (code) {
    case H264EncoderErrorCode::invalid_config:
        return "invalid encoder configuration";
    case H264EncoderErrorCode::session_creation_failed:
        return "VideoToolbox H.264 session creation failed";
    case H264EncoderErrorCode::hardware_unavailable:
        return "hardware H.264 encoder unavailable";
    case H264EncoderErrorCode::property_failed:
        return "VideoToolbox encoder property rejected";
    case H264EncoderErrorCode::invalid_source:
        return "encoder source frame is missing";
    case H264EncoderErrorCode::encode_failed:
        return "H.264 encode failed";
    case H264EncoderErrorCode::output_too_large:
        return "H.264 access unit exceeded the configured bound";
    }
    return "hardware encoder failure";
}

struct H264EncoderConfig {
    std::uint32_t width = 1920;
    std::uint32_t height = 1080;
    std::uint32_t frame_rate_numerator = 30;
    std::uint32_t frame_rate_denominator = 1;
    std::uint32_t bitrate = 6'000'000;
    std::uint32_t gop_frames = 60;
    std::size_t max_access_unit_bytes = 16U * 1024U * 1024U;
    // Production keeps this true. Tests and diagnostics may allow the software encoder.
    bool require_hardware = true;
};

[[nodiscard]] std::optional<H264EncoderError> validate(const H264EncoderConfig& config) noexcept;

struct EncodedAccessUnit {
    // Annex-B byte stream; keyframes carry SPS and PPS ahead of the IDR slice.
    std::vector<std::byte> bytes;
    std::uint64_t sequence = 0;
    std::int64_t pts_100ns = 0;
    bool keyframe = false;
};

struct H264EncoderStatistics {
    bool hardware_accelerated = false;
    bool low_latency_rate_control = false;
    std::uint64_t frames_submitted = 0;
    std::uint64_t frames_encoded = 0;
    std::uint64_t frames_dropped = 0;
    std::uint64_t encoded_bytes = 0;
    std::uint64_t keyframes = 0;
    std::uint64_t encode_failures = 0;
    std::uint64_t oversized_outputs = 0;
    std::uint64_t encode_total_us = 0;
    std::uint64_t encode_max_us = 0;
};

// VideoToolbox low-latency H.264: IOSurface NV12 in, Annex-B out. One frame in flight; encode()
// returns only after the access unit is emitted, so the codec never builds a queue. Single worker.
class MacH264Encoder final {
public:
    MacH264Encoder();
    ~MacH264Encoder();

    MacH264Encoder(const MacH264Encoder&) = delete;
    MacH264Encoder& operator=(const MacH264Encoder&) = delete;

    [[nodiscard]] std::optional<H264EncoderError> start(const H264EncoderConfig& config);
    // Success with empty output.bytes means the encoder dropped the frame.
    [[nodiscard]] std::optional<H264EncoderError> encode(const PixelBuffer& source,
                                                         std::int64_t pts_100ns,
                                                         bool force_keyframe,
                                                         EncodedAccessUnit& output);
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] H264EncoderStatistics statistics() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace catro::platform::macos

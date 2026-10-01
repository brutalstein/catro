#pragma once

#include <catro/platform/macos/pixel_buffer.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace catro::platform::macos {

enum class H264DecoderErrorCode : std::uint8_t {
    invalid_config,
    input_too_large,
    malformed_input,
    format_failed,
    session_creation_failed,
    // Production requires the hardware decoder; a software-only session is refused, never hidden.
    hardware_unavailable,
    decode_failed,
};

struct H264DecoderError {
    H264DecoderErrorCode code = H264DecoderErrorCode::decode_failed;
    std::int64_t native_code = 0;

    friend bool operator==(const H264DecoderError&, const H264DecoderError&) = default;
};

[[nodiscard]] constexpr const char* name(H264DecoderErrorCode code) noexcept {
    switch (code) {
    case H264DecoderErrorCode::invalid_config:
        return "invalid decoder configuration";
    case H264DecoderErrorCode::input_too_large:
        return "H.264 access unit exceeded the configured bound";
    case H264DecoderErrorCode::malformed_input:
        return "malformed Annex-B access unit";
    case H264DecoderErrorCode::format_failed:
        return "H.264 parameter sets were rejected";
    case H264DecoderErrorCode::session_creation_failed:
        return "VideoToolbox H.264 decoder creation failed";
    case H264DecoderErrorCode::hardware_unavailable:
        return "hardware H.264 decoder unavailable";
    case H264DecoderErrorCode::decode_failed:
        return "H.264 decode failed";
    }
    return "decoder failure";
}

struct H264DecoderConfig {
    std::size_t max_access_unit_bytes = 4U * 1024U * 1024U;
    // Production keeps this true. Tests and diagnostics may allow the software decoder.
    bool require_hardware = true;
};

struct DecodedFrame {
    // IOSurface-backed NV12; never copied into application CPU memory.
    PixelBuffer buffer;
    std::int64_t pts_100ns = 0;
};

struct H264DecoderStatistics {
    bool hardware_accelerated = false;
    std::uint64_t frames_submitted = 0;
    std::uint64_t frames_decoded = 0;
    std::uint64_t compressed_bytes = 0;
    std::uint64_t malformed_inputs = 0;
    std::uint64_t oversized_inputs = 0;
    std::uint64_t decode_failures = 0;
    // Access units that arrived before any SPS/PPS, typically while joining between keyframes.
    std::uint64_t waiting_for_parameter_sets = 0;
    std::uint64_t stream_changes = 0;
    std::uint64_t decode_total_us = 0;
    std::uint64_t decode_max_us = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// VideoToolbox H.264 decoder: Annex-B in, IOSurface NV12 out. New SPS/PPS rebuild the session.
// decode() is synchronous with one frame in flight. Single worker thread.
class MacH264Decoder final {
public:
    MacH264Decoder();
    ~MacH264Decoder();

    MacH264Decoder(const MacH264Decoder&) = delete;
    MacH264Decoder& operator=(const MacH264Decoder&) = delete;

    [[nodiscard]] std::optional<H264DecoderError> start(const H264DecoderConfig& config = {});
    // Success with an empty output.buffer means more input is needed.
    [[nodiscard]] std::optional<H264DecoderError> decode(std::span<const std::byte> annex_b_access_unit,
                                                         std::int64_t pts_100ns,
                                                         DecodedFrame& output);
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] H264DecoderStatistics statistics() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace catro::platform::macos

#pragma once

#include <catro/platform/macos/screen_capture.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace catro::platform::macos {

enum class VideoEncoderErrorCode : std::uint8_t {
    invalid_config,
    session_creation_failed,
    hardware_encoder_unavailable,
    property_failed,
    input_failed,
    output_failed,
    output_too_large,
};

struct VideoEncoderError {
    VideoEncoderErrorCode code = VideoEncoderErrorCode::session_creation_failed;
    std::int64_t native_code = 0;

    friend bool operator==(const VideoEncoderError&, const VideoEncoderError&) = default;
};

[[nodiscard]] constexpr const char* name(VideoEncoderErrorCode code) noexcept {
    switch (code) {
    case VideoEncoderErrorCode::invalid_config:
        return "invalid H.264 encoder configuration";
    case VideoEncoderErrorCode::session_creation_failed:
        return "VideoToolbox encoder session creation failed";
    case VideoEncoderErrorCode::hardware_encoder_unavailable:
        return "hardware H.264 encoder unavailable";
    case VideoEncoderErrorCode::property_failed:
        return "VideoToolbox encoder property failed";
    case VideoEncoderErrorCode::input_failed:
        return "VideoToolbox encoder rejected native input";
    case VideoEncoderErrorCode::output_failed:
        return "VideoToolbox encoder output failed";
    case VideoEncoderErrorCode::output_too_large:
        return "encoded access unit exceeded its configured bound";
    }
    return "H.264 encoder failure";
}

struct VideoEncoderConfig {
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    std::uint32_t frame_rate = 30;
    std::uint32_t bitrate = 4'000'000;
    std::uint32_t gop_frames = 60;
    std::size_t max_access_unit_bytes = 16U * 1024U * 1024U;
    bool require_hardware = true;
};

struct EncodedAccessUnit {
    std::vector<std::byte> bytes;
    std::uint64_t sequence = 0;
    std::int64_t pts_100ns = 0;
    bool keyframe = false;
};

struct VideoEncoderStartResult {
    std::optional<VideoEncoderError> error;
    bool hardware_accelerated = false;
    std::string implementation_name;
};

struct VideoEncoderStatistics {
    bool hardware_accelerated = false;
    bool low_latency_requested = false;
    std::string implementation_name;
    std::uint64_t frames_submitted = 0;
    std::uint64_t frames_encoded = 0;
    std::uint64_t encoded_bytes = 0;
    std::uint64_t keyframes = 0;
    std::uint64_t input_failures = 0;
    std::uint64_t output_failures = 0;
    std::uint64_t oversized_outputs = 0;
};

class VideoEncoderNativeAdapter {
public:
    using OutputHandler = std::function<void(EncodedAccessUnit)>;
    using FailureHandler = std::function<void(VideoEncoderError)>;

    virtual ~VideoEncoderNativeAdapter() = default;
    [[nodiscard]] virtual VideoEncoderStartResult start(
        const VideoEncoderConfig& config,
        OutputHandler on_output,
        FailureHandler on_failure) noexcept = 0;
    [[nodiscard]] virtual std::optional<VideoEncoderError> encode(
        const NativeVideoFrame& frame,
        bool force_keyframe) noexcept = 0;
    virtual void stop() noexcept = 0;
};

[[nodiscard]] std::unique_ptr<VideoEncoderNativeAdapter>
make_video_encoder_native_adapter();

class MacH264HardwareEncoder final {
public:
    using OutputHandler = std::function<void(const EncodedAccessUnit&)>;
    using FailureHandler = std::function<void(const VideoEncoderError&)>;

    MacH264HardwareEncoder();
    explicit MacH264HardwareEncoder(std::unique_ptr<VideoEncoderNativeAdapter> adapter);
    ~MacH264HardwareEncoder();

    MacH264HardwareEncoder(const MacH264HardwareEncoder&) = delete;
    MacH264HardwareEncoder& operator=(const MacH264HardwareEncoder&) = delete;

    [[nodiscard]] std::optional<VideoEncoderError> start(
        const VideoEncoderConfig& config,
        OutputHandler on_output = {},
        FailureHandler on_failure = {}) noexcept;
    [[nodiscard]] std::optional<VideoEncoderError> encode(
        const NativeVideoFrame& frame,
        bool force_keyframe = false) noexcept;
    void stop() noexcept;

    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] VideoEncoderStatistics statistics() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace catro::platform::macos

#pragma once

#include <catro/platform/macos/screen_capture.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace catro::platform::macos {

enum class VideoDecoderErrorCode : std::uint8_t {
    invalid_config,
    session_creation_failed,
    hardware_decoder_unavailable,
    malformed_access_unit,
    input_too_large,
    input_failed,
    output_failed,
};

struct VideoDecoderError {
    VideoDecoderErrorCode code = VideoDecoderErrorCode::session_creation_failed;
    std::int64_t native_code = 0;

    friend bool operator==(const VideoDecoderError&, const VideoDecoderError&) = default;
};

[[nodiscard]] constexpr const char* name(VideoDecoderErrorCode code) noexcept {
    switch (code) {
    case VideoDecoderErrorCode::invalid_config:
        return "invalid H.264 decoder configuration";
    case VideoDecoderErrorCode::session_creation_failed:
        return "VideoToolbox decoder session creation failed";
    case VideoDecoderErrorCode::hardware_decoder_unavailable:
        return "hardware H.264 decoder unavailable";
    case VideoDecoderErrorCode::malformed_access_unit:
        return "malformed Annex-B H.264 access unit";
    case VideoDecoderErrorCode::input_too_large:
        return "H.264 access unit exceeded its configured bound";
    case VideoDecoderErrorCode::input_failed:
        return "VideoToolbox decoder rejected compressed input";
    case VideoDecoderErrorCode::output_failed:
        return "VideoToolbox decoder output failed";
    }
    return "H.264 decoder failure";
}

struct VideoDecoderConfig {
    std::size_t max_access_unit_bytes = 4U * 1024U * 1024U;
    bool require_hardware = true;
};

struct VideoDecoderStartResult {
    std::optional<VideoDecoderError> error;
    bool hardware_accelerated = false;
    std::string implementation_name;
};

struct VideoDecoderStatistics {
    bool hardware_accelerated = false;
    std::string implementation_name;
    std::uint64_t frames_submitted = 0;
    std::uint64_t frames_decoded = 0;
    std::uint64_t compressed_bytes = 0;
    std::uint64_t malformed_inputs = 0;
    std::uint64_t oversized_inputs = 0;
    std::uint64_t input_failures = 0;
    std::uint64_t output_failures = 0;
};

class VideoDecoderNativeAdapter {
public:
    using OutputHandler = std::function<void(NativeVideoFrame)>;
    using FailureHandler = std::function<void(VideoDecoderError)>;

    virtual ~VideoDecoderNativeAdapter() = default;
    [[nodiscard]] virtual VideoDecoderStartResult start(
        const VideoDecoderConfig& config,
        OutputHandler on_output,
        FailureHandler on_failure) noexcept = 0;
    [[nodiscard]] virtual std::optional<VideoDecoderError> decode(
        std::span<const std::byte> access_unit,
        std::int64_t pts_100ns) noexcept = 0;
    virtual void stop() noexcept = 0;
};

[[nodiscard]] std::unique_ptr<VideoDecoderNativeAdapter>
make_video_decoder_native_adapter();

class MacH264HardwareDecoder final {
public:
    using OutputHandler = std::function<void(const NativeVideoFrame&)>;
    using FailureHandler = std::function<void(const VideoDecoderError&)>;

    MacH264HardwareDecoder();
    explicit MacH264HardwareDecoder(std::unique_ptr<VideoDecoderNativeAdapter> adapter);
    ~MacH264HardwareDecoder();

    MacH264HardwareDecoder(const MacH264HardwareDecoder&) = delete;
    MacH264HardwareDecoder& operator=(const MacH264HardwareDecoder&) = delete;

    [[nodiscard]] std::optional<VideoDecoderError> start(
        const VideoDecoderConfig& config,
        OutputHandler on_output = {},
        FailureHandler on_failure = {}) noexcept;
    [[nodiscard]] std::optional<VideoDecoderError> decode(
        std::span<const std::byte> access_unit,
        std::int64_t pts_100ns) noexcept;
    void stop() noexcept;

    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] VideoDecoderStatistics statistics() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace catro::platform::macos

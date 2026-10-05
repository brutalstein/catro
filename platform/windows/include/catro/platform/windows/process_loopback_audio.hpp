#pragma once

#include <cstdint>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace catro::platform::windows {

inline constexpr std::uint32_t kStreamAudioSampleRate = 48'000;
inline constexpr std::uint32_t kStreamAudioChannels = 2;

enum class StreamAudioErrorCode : std::uint8_t {
    unsupported,
    activation_failed,
    initialization_failed,
    device_unavailable,
    worker_start_failed,
    os_failure,
};

struct StreamAudioError {
    StreamAudioErrorCode code = StreamAudioErrorCode::os_failure;
    std::int64_t native_code = 0;
};

[[nodiscard]] constexpr std::string_view name(
    StreamAudioErrorCode code) noexcept {
    switch (code) {
    case StreamAudioErrorCode::unsupported:
        return "process audio capture unsupported";
    case StreamAudioErrorCode::activation_failed:
        return "process audio activation failed";
    case StreamAudioErrorCode::initialization_failed:
        return "stream audio initialization failed";
    case StreamAudioErrorCode::device_unavailable:
        return "stream audio output unavailable";
    case StreamAudioErrorCode::worker_start_failed:
        return "stream audio worker could not start";
    case StreamAudioErrorCode::os_failure:
        break;
    }
    return "stream audio OS failure";
}

struct StreamAudioStatistics {
    bool running = false;
    std::uint64_t callbacks = 0;
    std::uint64_t frames = 0;
    std::uint64_t glitches = 0;
    std::optional<StreamAudioError> error;
};

// Captures only audio rendered by one target process and its child-process tree.
//
// Windows performs device-format conversion at the boundary. The callback always receives
// interleaved 48 kHz float stereo and runs on an MMCSS audio worker. The callback must not allocate,
// block, take a mutex, or call platform APIs.
class ProcessLoopbackAudioCapture final {
public:
    using Sink =
        std::function<void(std::span<const float>)>;

    ProcessLoopbackAudioCapture();
    ~ProcessLoopbackAudioCapture();

    ProcessLoopbackAudioCapture(
        const ProcessLoopbackAudioCapture&) = delete;
    ProcessLoopbackAudioCapture& operator=(
        const ProcessLoopbackAudioCapture&) = delete;

    // Captures the process tree of process_id, or with exclude_target everything the system renders
    // except that tree (computer audio without Catro's own voice playback).
    // The optional cancellation flag must remain alive until start() returns.
    [[nodiscard]] std::optional<StreamAudioError> start(
        std::uint32_t process_id,
        Sink sink,
        bool exclude_target = false,
        const std::atomic_bool* cancelled = nullptr);
    void stop() noexcept;

    [[nodiscard]] StreamAudioStatistics statistics()
        const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Dedicated stereo renderer for incoming screen-share audio. It intentionally stays separate from
// the mono communications/voice renderer, so game/media audio keeps stereo imaging and independent
// buffering/volume policy.
class WasapiStreamAudioRenderer final {
public:
    using Source =
        std::function<void(std::span<float>)>;

    WasapiStreamAudioRenderer();
    ~WasapiStreamAudioRenderer();

    WasapiStreamAudioRenderer(
        const WasapiStreamAudioRenderer&) = delete;
    WasapiStreamAudioRenderer& operator=(
        const WasapiStreamAudioRenderer&) = delete;

    [[nodiscard]] std::optional<StreamAudioError> start(
        Source source);
    void stop() noexcept;

    [[nodiscard]] StreamAudioStatistics statistics()
        const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace catro::platform::windows

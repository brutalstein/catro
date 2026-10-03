#pragma once

#include <catro/capabilities/ids.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

namespace catro::audio {

enum class AudioErrorCode {
    device_not_found,
    device_in_use,
    // The OS or the user denied microphone access.
    permission_denied,
    format_unsupported,
    // The device disappeared or was invalidated while a stream ran.
    device_lost,
    os_failure,
};

struct AudioError {
    AudioErrorCode code = AudioErrorCode::os_failure;
    std::optional<std::int64_t> native_code;

    friend bool operator==(const AudioError&, const AudioError&) = default;
};

// What an opened stream runs on. Frame counts are at the engine rate, kSampleRate.
struct StreamInfo {
    capabilities::AudioEndpointId device;
    std::uint32_t device_sample_rate = 0;
    std::uint32_t device_channels = 0;
    std::uint32_t period_frames = 0;
    // Latency the device reports beyond the period, e.g. converter and hardware delay.
    std::uint32_t device_latency_frames = 0;

    friend bool operator==(const StreamInfo&, const StreamInfo&) = default;
};

// Real-time callbacks: called on a platform audio thread, must not allocate, lock, or block.
class CaptureSink {
public:
    virtual void on_captured(std::span<const float> frames) noexcept = 0;

protected:
    ~CaptureSink() = default;
};

class RenderSource {
public:
    // Must fill every frame.
    virtual void on_render(std::span<float> frames) noexcept = 0;

protected:
    ~RenderSource() = default;
};

// An opened, not yet started stream. Destruction stops it; no callback runs after it returns.
class AudioStream {
public:
    virtual ~AudioStream() = default;

    [[nodiscard]] virtual const StreamInfo& info() const noexcept = 0;
    [[nodiscard]] virtual std::optional<AudioError> start() = 0;
    // Idempotent and non-blocking enough for a platform failure callback. It must only ask the
    // stream to stop; destruction is still the completion barrier and guarantees callbacks ended.
    virtual void request_stop() noexcept = 0;
    // Discontinuities the OS reported, e.g. capture data lost to a late thread.
    [[nodiscard]] virtual std::uint64_t glitches() const noexcept = 0;
};

// Called at most once per stream, on a platform thread but never inside a real-time callback.
// It must not destroy the stream; post the reaction to a control thread instead.
using StreamFailure = std::function<void(AudioError)>;

using OpenResult = std::variant<std::unique_ptr<AudioStream>, AudioError>;

enum class DeviceDirection {
    capture,
    render,
};

// Implemented per platform. An absent device means the system default for communications.
class AudioPlatform {
public:
    virtual ~AudioPlatform() = default;
    // The device open_* would pick for an absent device right now, in StreamInfo::device form, or
    // nullopt when the platform cannot tell. Sessions on the default poll it to follow a new headset.
    [[nodiscard]] virtual std::optional<capabilities::AudioEndpointId> default_device(DeviceDirection) {
        return std::nullopt;
    }

    [[nodiscard]] virtual OpenResult open_capture(const std::optional<capabilities::AudioEndpointId>& device,
                                                  CaptureSink& sink, StreamFailure failure) = 0;
    [[nodiscard]] virtual OpenResult open_render(const std::optional<capabilities::AudioEndpointId>& device,
                                                 RenderSource& source, StreamFailure failure) = 0;
};

enum class SessionMode {
    // Capture only, with an input meter.
    meter,
    // Render a test tone only.
    tone,
    // Capture to render, for a live microphone check.
    monitor,
};

struct SessionConfig {
    SessionMode mode = SessionMode::meter;
    std::optional<capabilities::AudioEndpointId> input;
    std::optional<capabilities::AudioEndpointId> output;
};

enum class EngineState {
    idle,
    running,
    failed,
};

struct AudioStatistics {
    EngineState state = EngineState::idle;
    std::optional<SessionMode> mode;
    std::optional<AudioError> error;
    std::optional<StreamInfo> input;
    std::optional<StreamInfo> output;
    // Linear levels of the latest block, 1.0 is full scale.
    float input_peak = 0.0F;
    float input_rms = 0.0F;
    float output_peak = 0.0F;
    std::uint64_t underruns = 0;
    std::uint64_t overruns = 0;
    std::uint64_t drift_corrections = 0;
    std::uint64_t glitches = 0;
    // Monitor only. Sum of device latencies, periods, and the monitor cushion; never measured.
    std::optional<std::chrono::microseconds> estimated_latency;
};

// Stable lowercase names for tools, logs, and view models.
[[nodiscard]] constexpr std::string_view name(AudioErrorCode code) noexcept {
    switch (code) {
    case AudioErrorCode::device_not_found:
        return "device not found";
    case AudioErrorCode::device_in_use:
        return "device in use";
    case AudioErrorCode::permission_denied:
        return "microphone access denied";
    case AudioErrorCode::format_unsupported:
        return "format unsupported";
    case AudioErrorCode::device_lost:
        return "device lost";
    case AudioErrorCode::os_failure:
        break;
    }
    return "OS failure";
}

[[nodiscard]] constexpr std::string_view name(SessionMode mode) noexcept {
    switch (mode) {
    case SessionMode::tone:
        return "tone";
    case SessionMode::monitor:
        return "monitor";
    case SessionMode::meter:
        break;
    }
    return "meter";
}

// Runs one session at a time. start, stop, and statistics may be called from any non-audio
// thread. start and stop are serialized; the failure handler runs on a platform failure thread
// after every stream in the failed session has been asked to stop.
class AudioEngine {
public:
    using FailureHandler = std::function<void(AudioError)>;

    explicit AudioEngine(AudioPlatform& platform, FailureHandler on_failure = {});
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // Stops any running session first. On error nothing is left running and the state is failed.
    std::optional<AudioError> start(const SessionConfig& config);
    void stop();
    [[nodiscard]] AudioStatistics statistics() const;

private:
    struct Session;

    void fail(std::uint64_t generation, AudioError error);

    AudioPlatform& platform_;
    FailureHandler on_failure_;
    // Serializes lifecycle transitions that can open/start/destroy platform streams. Failure
    // callbacks never take this mutex because a stream destructor may be joining their thread.
    std::mutex lifecycle_mutex_;
    mutable std::mutex mutex_;
    std::unique_ptr<Session> session_;
    std::uint64_t generation_ = 0;
    EngineState state_ = EngineState::idle;
    std::optional<AudioError> error_;
};

} // namespace catro::audio

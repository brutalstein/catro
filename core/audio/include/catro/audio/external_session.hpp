#pragma once

#include <catro/audio/engine.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

namespace catro::audio {

enum class ExternalSessionMode {
    capture_only,
    render_only,
    duplex,
};

struct ExternalSessionConfig {
    ExternalSessionMode mode = ExternalSessionMode::duplex;
    std::optional<capabilities::AudioEndpointId> input;
    std::optional<capabilities::AudioEndpointId> output;
};

struct ExternalAudioStatistics {
    EngineState state = EngineState::idle;
    std::optional<AudioError> error;
    std::optional<StreamInfo> input;
    std::optional<StreamInfo> output;
    std::uint64_t glitches = 0;
};

// Owns native streams while user-supplied real-time callbacks own the media path. The sink/source
// must outlive a running session. start/stop are serialized; native stream destruction never occurs
// while the state mutex is held.
class ExternalAudioSession {
public:
    using FailureHandler = std::function<void(AudioError)>;

    explicit ExternalAudioSession(AudioPlatform& platform, FailureHandler on_failure = {});
    ~ExternalAudioSession();

    ExternalAudioSession(const ExternalAudioSession&) = delete;
    ExternalAudioSession& operator=(const ExternalAudioSession&) = delete;

    [[nodiscard]] std::optional<AudioError> start(const ExternalSessionConfig& config,
                                                  CaptureSink& capture_sink,
                                                  RenderSource& render_source);
    void stop();

    [[nodiscard]] ExternalAudioStatistics statistics() const;

private:
    struct Session;
    void fail(std::uint64_t generation, AudioError error);

    AudioPlatform& platform_;
    FailureHandler on_failure_;
    std::mutex lifecycle_mutex_;
    mutable std::mutex mutex_;
    std::unique_ptr<Session> session_;
    std::uint64_t generation_ = 0;
    EngineState state_ = EngineState::idle;
    std::optional<AudioError> error_;
};

} // namespace catro::audio

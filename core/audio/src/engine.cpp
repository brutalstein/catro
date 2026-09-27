#include <catro/audio/engine.hpp>

#include <catro/audio/monitor_pipe.hpp>
#include <catro/audio/realtime.hpp>

#include <algorithm>
#include <utility>

namespace catro::audio {
namespace {

// The monitor cushion never drops below 10 ms, and capture may run 40 ms ahead of it before
// the oldest frames are dropped.
constexpr std::uint32_t kMinimumCushionFrames = kSampleRate / 100;
constexpr std::uint32_t kDriftAllowanceFrames = kSampleRate / 25;

constexpr double kToneHz = 440.0;
// About -14 dBFS: clearly audible without being harsh.
constexpr float kToneAmplitude = 0.2F;

} // namespace

// Owns everything the audio threads touch. The streams are declared last so they are destroyed
// first, before the state their callbacks use.
struct AudioEngine::Session final : CaptureSink, RenderSource {
    explicit Session(SessionMode session_mode) : mode(session_mode) {}

    void on_captured(std::span<const float> frames) noexcept override {
        input_meter.process(frames);
        if (pipe) {
            pipe->push(frames);
        }
    }

    void on_render(std::span<float> frames) noexcept override {
        if (pipe) {
            pipe->pull(frames);
        } else {
            tone.fill(frames);
        }
        output_meter.process(frames);
    }

    SessionMode mode;
    LevelMeter input_meter;
    LevelMeter output_meter;
    ToneGenerator tone{kToneHz, kToneAmplitude};
    // Created before the streams start, never changed while they run.
    std::optional<MonitorPipe> pipe;
    std::unique_ptr<AudioStream> capture;
    std::unique_ptr<AudioStream> render;
};

AudioEngine::AudioEngine(AudioPlatform& platform, FailureHandler on_failure)
    : platform_(platform), on_failure_(std::move(on_failure)) {}

AudioEngine::~AudioEngine() {
    stop();
}

std::optional<AudioError> AudioEngine::start(const SessionConfig& config) {
    stop();
    std::uint64_t generation = 0;
    {
        const std::scoped_lock lock(mutex_);
        generation = ++generation_;
    }
    auto session = std::make_unique<Session>(config.mode);
    const auto failure = [this, generation](AudioError error) { fail(generation, error); };
    const auto record = [&](AudioError error) {
        const std::scoped_lock lock(mutex_);
        if (generation == generation_) {
            state_ = EngineState::failed;
            error_ = error;
        }
        return std::optional{error};
    };
    const auto open = [&](OpenResult result, std::unique_ptr<AudioStream>& slot) -> std::optional<AudioError> {
        if (auto* error = std::get_if<AudioError>(&result)) {
            return *error;
        }
        slot = std::move(std::get<std::unique_ptr<AudioStream>>(result));
        return std::nullopt;
    };

    if (config.mode != SessionMode::tone) {
        if (const auto error = open(platform_.open_capture(config.input, *session, failure), session->capture)) {
            return record(*error);
        }
    }
    if (config.mode != SessionMode::meter) {
        if (const auto error = open(platform_.open_render(config.output, *session, failure), session->render)) {
            return record(*error);
        }
    }
    if (config.mode == SessionMode::monitor) {
        const auto target = std::max(session->capture->info().period_frames + session->render->info().period_frames,
                                     kMinimumCushionFrames);
        session->pipe.emplace(target, target + kDriftAllowanceFrames);
    }
    for (auto* stream : {session->capture.get(), session->render.get()}) {
        if (stream == nullptr) {
            continue;
        }
        if (const auto error = stream->start()) {
            return record(*error);
        }
    }

    const std::scoped_lock lock(mutex_);
    // A stream may already have failed on its own thread; that failure stands.
    if (generation == generation_ && state_ != EngineState::failed) {
        state_ = EngineState::running;
    }
    session_ = std::move(session);
    return error_;
}

void AudioEngine::stop() {
    std::unique_ptr<Session> finished;
    {
        const std::scoped_lock lock(mutex_);
        ++generation_;
        finished = std::move(session_);
        state_ = EngineState::idle;
        error_.reset();
    }
    // Destroyed outside the lock: stopping a stream joins its thread, which may be waiting in fail().
    finished.reset();
}

void AudioEngine::fail(std::uint64_t generation, AudioError error) {
    {
        const std::scoped_lock lock(mutex_);
        if (generation != generation_ || state_ == EngineState::failed) {
            return;
        }
        state_ = EngineState::failed;
        error_ = error;
    }
    if (on_failure_) {
        on_failure_(error);
    }
}

AudioStatistics AudioEngine::statistics() const {
    const std::scoped_lock lock(mutex_);
    AudioStatistics statistics{.state = state_, .error = error_};
    if (!session_) {
        return statistics;
    }
    const auto& session = *session_;
    statistics.mode = session.mode;
    if (session.capture) {
        statistics.input = session.capture->info();
        statistics.input_peak = session.input_meter.peak();
        statistics.input_rms = session.input_meter.rms();
        statistics.glitches += session.capture->glitches();
    }
    if (session.render) {
        statistics.output = session.render->info();
        statistics.output_peak = session.output_meter.peak();
        statistics.glitches += session.render->glitches();
    }
    if (session.pipe) {
        statistics.underruns = session.pipe->underruns();
        statistics.overruns = session.pipe->overruns();
        statistics.drift_corrections = session.pipe->drift_corrections();
        const auto frames = std::uint64_t{statistics.input->device_latency_frames} + statistics.input->period_frames +
                            session.pipe->target_frames() + statistics.output->period_frames +
                            statistics.output->device_latency_frames;
        statistics.estimated_latency = std::chrono::microseconds(frames * 1'000'000 / kSampleRate);
    }
    return statistics;
}

} // namespace catro::audio

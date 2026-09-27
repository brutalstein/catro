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
    // Opening and starting native streams is a lifecycle transaction. A concurrent stop waits for
    // the transaction to finish instead of returning while a local, unpublished stream can still
    // become active.
    const std::scoped_lock lifecycle_lock(lifecycle_mutex_);

    std::unique_ptr<Session> previous;
    std::uint64_t generation = 0;
    {
        const std::scoped_lock lock(mutex_);
        ++generation_;
        previous = std::move(session_);
        state_ = EngineState::idle;
        error_.reset();
        generation = ++generation_;
    }
    // Platform destructors may join their worker threads; never do that while holding mutex_.
    previous.reset();

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

    // Publish ownership before starting either stream. If a platform reports an asynchronous
    // failure immediately after start(), fail() can now see the complete session and request both
    // sides to stop without destroying a stream from its own callback thread.
    {
        const std::scoped_lock lock(mutex_);
        session_ = std::move(session);
    }

    const auto abort_start = [&](AudioError error) {
        std::unique_ptr<Session> failed;
        {
            const std::scoped_lock lock(mutex_);
            if (generation == generation_) {
                state_ = EngineState::failed;
                error_ = error;
                failed = std::move(session_);
            }
        }
        failed.reset();
        return std::optional{error};
    };

    Session* active = nullptr;
    {
        const std::scoped_lock lock(mutex_);
        active = session_.get();
    }

    // In monitor mode start render first. Bluetooth and other outputs can take noticeably longer
    // to leave their prepared state; starting capture first lets the monitor ring fill and report
    // a burst of artificial startup overruns before playback is even running.
    const auto start_stream = [&](AudioStream* stream) -> std::optional<AudioError> {
        if (stream == nullptr) {
            return std::nullopt;
        }
        {
            const std::scoped_lock lock(mutex_);
            if (generation != generation_ || state_ == EngineState::failed) {
                return error_;
            }
        }
        return stream->start();
    };

    if (config.mode == SessionMode::monitor) {
        if (const auto error = start_stream(active->render.get())) {
            return abort_start(*error);
        }
        if (const auto error = start_stream(active->capture.get())) {
            return abort_start(*error);
        }
    } else {
        if (const auto error = start_stream(active->capture.get())) {
            return abort_start(*error);
        }
        if (const auto error = start_stream(active->render.get())) {
            return abort_start(*error);
        }
    }

    const std::scoped_lock lock(mutex_);
    if (generation == generation_ && state_ != EngineState::failed) {
        state_ = EngineState::running;
    }
    return error_;
}

void AudioEngine::stop() {
    const std::scoped_lock lifecycle_lock(lifecycle_mutex_);
    std::unique_ptr<Session> finished;
    {
        const std::scoped_lock lock(mutex_);
        ++generation_;
        finished = std::move(session_);
        state_ = EngineState::idle;
        error_.reset();
    }
    // Destroyed outside the state lock: stopping a stream may join its platform worker thread.
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

        // Do not destroy streams here. On Windows this callback can be running on the stream
        // thread itself, and destruction would attempt to join that same thread. request_stop()
        // is the non-owning, idempotent escape hatch that also stops the peer stream.
        if (session_) {
            if (session_->capture) {
                session_->capture->request_stop();
            }
            if (session_->render) {
                session_->render->request_stop();
            }
        }
    }
    // User code is invoked without either engine mutex held, so it may safely call start()/stop().
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

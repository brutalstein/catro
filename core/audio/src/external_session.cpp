#include <catro/audio/external_session.hpp>

#include <utility>

namespace catro::audio {

struct ExternalAudioSession::Session {
    std::unique_ptr<AudioStream> capture;
    std::unique_ptr<AudioStream> render;
};

ExternalAudioSession::ExternalAudioSession(AudioPlatform& platform, FailureHandler on_failure)
    : platform_(platform), on_failure_(std::move(on_failure)) {}

ExternalAudioSession::~ExternalAudioSession() {
    stop();
}

std::optional<AudioError> ExternalAudioSession::start(const ExternalSessionConfig& config,
                                                      CaptureSink& capture_sink,
                                                      RenderSource& render_source) {
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
    previous.reset();

    auto session = std::make_unique<Session>();
    const auto failure = [this, generation](AudioError error) { fail(generation, error); };
    const auto open = [](OpenResult result, std::unique_ptr<AudioStream>& slot) -> std::optional<AudioError> {
        if (auto* error = std::get_if<AudioError>(&result)) {
            return *error;
        }
        slot = std::move(std::get<std::unique_ptr<AudioStream>>(result));
        return std::nullopt;
    };
    const auto record_open_failure = [&](AudioError error) {
        const std::scoped_lock lock(mutex_);
        if (generation == generation_) {
            state_ = EngineState::failed;
            error_ = error;
        }
        return std::optional{error};
    };

    if (config.mode != ExternalSessionMode::render_only) {
        if (const auto error =
                open(platform_.open_capture(config.input, capture_sink, failure), session->capture)) {
            return record_open_failure(*error);
        }
    }
    if (config.mode != ExternalSessionMode::capture_only) {
        if (const auto error =
                open(platform_.open_render(config.output, render_source, failure), session->render)) {
            return record_open_failure(*error);
        }
    }

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

    // Playback is armed first so incoming media cannot build an artificial startup backlog while
    // a slow Bluetooth or other output endpoint is still starting.
    if (const auto error = start_stream(active->render.get())) {
        return abort_start(*error);
    }
    if (const auto error = start_stream(active->capture.get())) {
        return abort_start(*error);
    }

    const std::scoped_lock lock(mutex_);
    if (generation == generation_ && state_ != EngineState::failed) {
        state_ = EngineState::running;
    }
    return error_;
}

void ExternalAudioSession::stop() {
    const std::scoped_lock lifecycle_lock(lifecycle_mutex_);
    std::unique_ptr<Session> finished;
    {
        const std::scoped_lock lock(mutex_);
        ++generation_;
        finished = std::move(session_);
        state_ = EngineState::idle;
        error_.reset();
    }
    finished.reset();
}

void ExternalAudioSession::fail(std::uint64_t generation, AudioError error) {
    {
        const std::scoped_lock lock(mutex_);
        if (generation != generation_ || state_ == EngineState::failed) {
            return;
        }
        state_ = EngineState::failed;
        error_ = error;
        if (session_) {
            if (session_->capture) {
                session_->capture->request_stop();
            }
            if (session_->render) {
                session_->render->request_stop();
            }
        }
    }
    if (on_failure_) {
        on_failure_(error);
    }
}

ExternalAudioStatistics ExternalAudioSession::statistics() const {
    const std::scoped_lock lock(mutex_);
    ExternalAudioStatistics result{.state = state_, .error = error_};
    if (!session_) {
        return result;
    }
    if (session_->capture) {
        result.input = session_->capture->info();
        result.glitches += session_->capture->glitches();
    }
    if (session_->render) {
        result.output = session_->render->info();
        result.glitches += session_->render->glitches();
    }
    return result;
}

} // namespace catro::audio

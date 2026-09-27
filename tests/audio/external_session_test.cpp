#include <catro/audio/external_session.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace catro::audio;
using catro::capabilities::AudioEndpointId;
using catro::capabilities::IdentityScope;

namespace {

AudioEndpointId endpoint(std::string value) {
    return {std::move(value), IdentityScope::persistent};
}

class Sink final : public CaptureSink {
public:
    void on_captured(std::span<const float>) noexcept override {}
};

class Source final : public RenderSource {
public:
    void on_render(std::span<float> frames) noexcept override {
        for (auto& frame : frames) {
            frame = 0.0F;
        }
    }
};

class Stream final : public AudioStream {
public:
    Stream(StreamInfo info, std::atomic_bool& started, std::vector<std::string>& order,
           std::string label, std::optional<AudioError> start_error = {})
        : info_(std::move(info)), started_(started), order_(order), label_(std::move(label)),
          start_error_(start_error) {}

    ~Stream() override {
        started_.store(false, std::memory_order_relaxed);
    }

    const StreamInfo& info() const noexcept override {
        return info_;
    }

    std::optional<AudioError> start() override {
        order_.push_back(label_);
        if (start_error_) {
            return start_error_;
        }
        started_.store(true, std::memory_order_relaxed);
        return std::nullopt;
    }

    void request_stop() noexcept override {
        started_.store(false, std::memory_order_relaxed);
    }

    std::uint64_t glitches() const noexcept override {
        return glitches_;
    }

    void set_glitches(std::uint64_t value) noexcept {
        glitches_ = value;
    }

private:
    StreamInfo info_;
    std::atomic_bool& started_;
    std::vector<std::string>& order_;
    std::string label_;
    std::optional<AudioError> start_error_;
    std::uint64_t glitches_ = 0;
};

class Platform final : public AudioPlatform {
public:
    OpenResult open_capture(const std::optional<AudioEndpointId>& device, CaptureSink& sink,
                            StreamFailure failure) override {
        ++capture_opens;
        capture_sink = &sink;
        capture_failure = std::move(failure);
        if (capture_open_error) {
            return *capture_open_error;
        }
        auto stream = std::make_unique<Stream>(
            StreamInfo{device.value_or(endpoint("default-in")), 48000, 1, 480, 0},
            capture_started, start_order, "capture", capture_start_error);
        capture_stream = stream.get();
        return stream;
    }

    OpenResult open_render(const std::optional<AudioEndpointId>& device, RenderSource& source,
                           StreamFailure failure) override {
        ++render_opens;
        render_source = &source;
        render_failure = std::move(failure);
        if (render_open_error) {
            return *render_open_error;
        }
        auto stream = std::make_unique<Stream>(
            StreamInfo{device.value_or(endpoint("default-out")), 48000, 2, 480, 0},
            render_started, start_order, "render", render_start_error);
        render_stream = stream.get();
        return stream;
    }

    int capture_opens = 0;
    int render_opens = 0;
    std::optional<AudioError> capture_open_error;
    std::optional<AudioError> render_open_error;
    std::optional<AudioError> capture_start_error;
    std::optional<AudioError> render_start_error;
    CaptureSink* capture_sink = nullptr;
    RenderSource* render_source = nullptr;
    StreamFailure capture_failure;
    StreamFailure render_failure;
    Stream* capture_stream = nullptr;
    Stream* render_stream = nullptr;
    std::atomic_bool capture_started = false;
    std::atomic_bool render_started = false;
    std::vector<std::string> start_order;
};

} // namespace

TEST_CASE("external duplex session starts render before capture and exposes native stream info") {
    Platform platform;
    Sink sink;
    Source source;
    ExternalAudioSession session(platform);

    const ExternalSessionConfig config{
        .mode = ExternalSessionMode::duplex,
        .input = endpoint("mic"),
        .output = endpoint("phones"),
    };
    REQUIRE_FALSE(session.start(config, sink, source));
    CHECK(platform.start_order == std::vector<std::string>{"render", "capture"});
    CHECK(platform.capture_started.load(std::memory_order_relaxed));
    CHECK(platform.render_started.load(std::memory_order_relaxed));

    const auto stats = session.statistics();
    CHECK(stats.state == EngineState::running);
    REQUIRE(stats.input);
    REQUIRE(stats.output);
    CHECK(stats.input->device == endpoint("mic"));
    CHECK(stats.output->device == endpoint("phones"));

    session.stop();
    CHECK_FALSE(platform.capture_started.load(std::memory_order_relaxed));
    CHECK_FALSE(platform.render_started.load(std::memory_order_relaxed));
    CHECK(session.statistics().state == EngineState::idle);
}

TEST_CASE("external session opens only the requested direction") {
    Sink sink;
    Source source;

    SECTION("capture only") {
        Platform platform;
        ExternalAudioSession session(platform);
        REQUIRE_FALSE(session.start({.mode = ExternalSessionMode::capture_only}, sink, source));
        CHECK(platform.capture_opens == 1);
        CHECK(platform.render_opens == 0);
        CHECK(platform.capture_started.load(std::memory_order_relaxed));
    }

    SECTION("render only") {
        Platform platform;
        ExternalAudioSession session(platform);
        REQUIRE_FALSE(session.start({.mode = ExternalSessionMode::render_only}, sink, source));
        CHECK(platform.capture_opens == 0);
        CHECK(platform.render_opens == 1);
        CHECK(platform.render_started.load(std::memory_order_relaxed));
    }
}

TEST_CASE("external session leaves no running stream when opening or starting fails") {
    Sink sink;
    Source source;

    SECTION("render open fails after capture opened") {
        Platform platform;
        platform.render_open_error = AudioError{AudioErrorCode::device_not_found};
        ExternalAudioSession session(platform);
        const auto error = session.start({.mode = ExternalSessionMode::duplex}, sink, source);
        REQUIRE(error);
        CHECK(error->code == AudioErrorCode::device_not_found);
        CHECK_FALSE(platform.capture_started.load(std::memory_order_relaxed));
        CHECK(session.statistics().state == EngineState::failed);
    }

    SECTION("capture start fails after render started") {
        Platform platform;
        platform.capture_start_error = AudioError{AudioErrorCode::device_lost};
        ExternalAudioSession session(platform);
        const auto error = session.start({.mode = ExternalSessionMode::duplex}, sink, source);
        REQUIRE(error);
        CHECK(error->code == AudioErrorCode::device_lost);
        CHECK_FALSE(platform.capture_started.load(std::memory_order_relaxed));
        CHECK_FALSE(platform.render_started.load(std::memory_order_relaxed));
        CHECK(session.statistics().state == EngineState::failed);
    }
}

TEST_CASE("external session failure requests both streams to stop and ignores stale failures") {
    Platform platform;
    Sink sink;
    Source source;
    std::vector<AudioError> failures;
    ExternalAudioSession session(platform, [&](AudioError error) { failures.push_back(error); });

    REQUIRE_FALSE(session.start({.mode = ExternalSessionMode::duplex}, sink, source));
    const auto stale = platform.capture_failure;
    REQUIRE(platform.render_failure);

    platform.render_failure(AudioError{AudioErrorCode::device_lost});
    CHECK_FALSE(platform.capture_started.load(std::memory_order_relaxed));
    CHECK_FALSE(platform.render_started.load(std::memory_order_relaxed));
    CHECK(session.statistics().state == EngineState::failed);
    REQUIRE(failures.size() == 1);
    CHECK(failures.front().code == AudioErrorCode::device_lost);

    REQUIRE_FALSE(session.start({.mode = ExternalSessionMode::duplex}, sink, source));
    REQUIRE(stale);
    stale(AudioError{AudioErrorCode::os_failure});
    CHECK(session.statistics().state == EngineState::running);
    CHECK(failures.size() == 1);
}

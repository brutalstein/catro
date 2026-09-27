#include <catro/audio/engine.hpp>
#include <catro/audio/monitor_pipe.hpp>
#include <catro/audio/realtime.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numeric>
#include <thread>
#include <vector>

using namespace catro::audio;
using catro::capabilities::AudioEndpointId;
using catro::capabilities::IdentityScope;
using Catch::Approx;

TEST_CASE("the ring rounds its capacity and keeps order across wrap-around") {
    SpscRing<int> ring(5);
    CHECK(ring.capacity() == 8);

    const std::array input{1, 2, 3, 4, 5, 6};
    CHECK(ring.write(input) == 6);
    std::array<int, 4> out{};
    CHECK(ring.read(out) == 4);
    CHECK(out == std::array{1, 2, 3, 4});

    // Six more fit only partly: two remain buffered, so six of eight slots are free.
    const std::array more{7, 8, 9, 10, 11, 12, 13};
    CHECK(ring.write(more) == 6);
    CHECK(ring.size() == 8);
    CHECK(ring.discard(3) == 3);
    std::array<int, 8> rest{};
    CHECK(ring.read(rest) == 5);
    CHECK(std::vector(rest.begin(), rest.begin() + 5) == std::vector{8, 9, 10, 11, 12});
    CHECK(ring.discard(1) == 0);
}

TEST_CASE("the ring delivers every item in order between two threads") {
    constexpr std::uint32_t kItems = 1'000'000;
    SpscRing<std::uint32_t> ring(1024);

    std::thread producer([&] {
        std::array<std::uint32_t, 97> chunk{};
        std::uint32_t next = 0;
        while (next < kItems) {
            const auto count = std::min<std::uint32_t>(static_cast<std::uint32_t>(chunk.size()), kItems - next);
            std::iota(chunk.begin(), chunk.begin() + count, next);
            std::span<const std::uint32_t> pending(chunk.data(), count);
            while (!pending.empty()) {
                pending = pending.subspan(ring.write(pending));
            }
            next += count;
        }
    });

    std::array<std::uint32_t, 61> chunk{};
    std::uint32_t expected = 0;
    bool ordered = true;
    while (expected < kItems) {
        const auto count = ring.read(chunk);
        for (std::size_t index = 0; index < count; ++index) {
            ordered = ordered && chunk[index] == expected;
            ++expected;
        }
    }
    producer.join();
    CHECK(ordered);
    CHECK(ring.size() == 0);
}

TEST_CASE("the level meter reports block peak and RMS in dBFS") {
    std::vector<float> block(4800);
    ToneGenerator(1000.0, 0.5F).fill(block);
    LevelMeter meter;
    meter.process(block);
    CHECK(meter.peak() == Approx(0.5F).epsilon(0.01));
    CHECK(meter.rms() == Approx(0.5F / std::sqrt(2.0F)).epsilon(0.01));

    CHECK(LevelMeter::to_dbfs(1.0F) == Approx(0.0F));
    CHECK(LevelMeter::to_dbfs(0.5F) == Approx(-6.0206F).epsilon(0.001));
    CHECK(LevelMeter::to_dbfs(0.0F) == -120.0F);
    CHECK(LevelMeter::to_dbfs(1e-9F) == -120.0F);

    meter.reset();
    CHECK(meter.peak() == 0.0F);
}

TEST_CASE("the tone is phase-continuous across blocks and at its frequency") {
    std::vector<float> whole(kSampleRate);
    ToneGenerator(440.0, 0.2F).fill(whole);

    std::vector<float> pieces(kSampleRate);
    ToneGenerator split(440.0, 0.2F);
    for (std::size_t offset = 0; offset < pieces.size(); offset += 480) {
        split.fill(std::span(pieces).subspan(offset, 480));
    }
    CHECK(std::ranges::equal(whole, pieces));

    std::size_t rising = 0;
    for (std::size_t index = 1; index < whole.size(); ++index) {
        rising += whole[index - 1] < 0.0F && whole[index] >= 0.0F ? 1 : 0;
    }
    CHECK(rising >= 439);
    CHECK(rising <= 441);
}

TEST_CASE("the monitor pipe primes, plays in order, and recovers from starvation") {
    MonitorPipe pipe(4, 8);
    std::array<float, 4> out{};

    const std::array<float, 3> first{1, 2, 3};
    pipe.push(first);
    pipe.pull(out);
    CHECK(out == std::array<float, 4>{0, 0, 0, 0});
    CHECK(pipe.underruns() == 0);

    const std::array<float, 3> second{4, 5, 6};
    pipe.push(second);
    pipe.pull(out);
    CHECK(out == std::array<float, 4>{1, 2, 3, 4});

    // Two frames left: the block is padded with silence and the pipe re-primes.
    pipe.pull(out);
    CHECK(out == std::array<float, 4>{5, 6, 0, 0});
    CHECK(pipe.underruns() == 1);
    const std::array<float, 2> third{7, 8};
    pipe.push(third);
    pipe.pull(out);
    CHECK(out == std::array<float, 4>{0, 0, 0, 0});
}

TEST_CASE("the monitor pipe corrects drift and counts overruns") {
    MonitorPipe pipe(4, 8);
    std::vector<float> frames(12);
    std::iota(frames.begin(), frames.end(), 1.0F);
    pipe.push(frames);
    CHECK(pipe.overruns() == 0);

    // Twelve buffered is past the high-water mark: the oldest are dropped down to the target.
    std::array<float, 4> out{};
    pipe.pull(out);
    CHECK(pipe.drift_corrections() == 1);
    CHECK(out == std::array<float, 4>{9, 10, 11, 12});

    std::vector<float> flood(40, 1.0F);
    pipe.push(flood);
    CHECK(pipe.overruns() == 1);
}

namespace {

AudioEndpointId endpoint(const char* value) {
    return {value, IdentityScope::persistent};
}

class FakeStream final : public AudioStream {
public:
    FakeStream(StreamInfo info, std::optional<AudioError> start_error, bool& started)
        : info_(std::move(info)), start_error_(start_error), started_(started) {}

    ~FakeStream() override { started_ = false; }

    const StreamInfo& info() const noexcept override { return info_; }
    std::optional<AudioError> start() override {
        started_ = !start_error_;
        return start_error_;
    }
    std::uint64_t glitches() const noexcept override { return 2; }

private:
    StreamInfo info_;
    std::optional<AudioError> start_error_;
    bool& started_;
};

class FakePlatform final : public AudioPlatform {
public:
    OpenResult open_capture(const std::optional<AudioEndpointId>& device, CaptureSink& capture_sink,
                            StreamFailure failure) override {
        ++capture_opens;
        if (capture_open_error) {
            return *capture_open_error;
        }
        sink = &capture_sink;
        capture_failure = std::move(failure);
        return std::make_unique<FakeStream>(
            StreamInfo{device.value_or(endpoint("default-in")), 44100, 1, 480, 96}, capture_start_error,
            capture_started);
    }

    OpenResult open_render(const std::optional<AudioEndpointId>& device, RenderSource& render_source,
                           StreamFailure failure) override {
        ++render_opens;
        if (render_open_error) {
            return *render_open_error;
        }
        source = &render_source;
        render_failure = std::move(failure);
        return std::make_unique<FakeStream>(StreamInfo{device.value_or(endpoint("default-out")), 48000, 2, 480, 144},
                                            std::nullopt, render_started);
    }

    int capture_opens = 0;
    int render_opens = 0;
    std::optional<AudioError> capture_open_error;
    std::optional<AudioError> capture_start_error;
    std::optional<AudioError> render_open_error;
    CaptureSink* sink = nullptr;
    RenderSource* source = nullptr;
    StreamFailure capture_failure;
    StreamFailure render_failure;
    bool capture_started = false;
    bool render_started = false;
};

} // namespace

TEST_CASE("a meter session opens only the capture stream") {
    FakePlatform platform;
    AudioEngine engine(platform);
    REQUIRE_FALSE(engine.start({.mode = SessionMode::meter, .input = endpoint("mic")}));
    CHECK(platform.capture_opens == 1);
    CHECK(platform.render_opens == 0);
    CHECK(platform.capture_started);

    const std::array<float, 4> frames{0.1F, -0.5F, 0.25F, 0.0F};
    platform.sink->on_captured(frames);
    const auto statistics = engine.statistics();
    CHECK(statistics.state == EngineState::running);
    CHECK(statistics.mode == SessionMode::meter);
    CHECK(statistics.input->device == endpoint("mic"));
    CHECK(statistics.input_peak == 0.5F);
    CHECK_FALSE(statistics.output);
    CHECK_FALSE(statistics.estimated_latency);
    CHECK(statistics.glitches == 2);

    engine.stop();
    CHECK_FALSE(platform.capture_started);
    CHECK(engine.statistics().state == EngineState::idle);
}

TEST_CASE("a tone session renders the test tone on the default output") {
    FakePlatform platform;
    AudioEngine engine(platform);
    REQUIRE_FALSE(engine.start({.mode = SessionMode::tone}));
    CHECK(platform.capture_opens == 0);
    std::vector<float> out(4800);
    platform.source->on_render(out);
    const auto statistics = engine.statistics();
    CHECK(statistics.output->device == endpoint("default-out"));
    CHECK(statistics.output_peak == Approx(0.2F).epsilon(0.01));
}

TEST_CASE("a monitor session carries capture to render and estimates latency") {
    FakePlatform platform;
    AudioEngine engine(platform);
    REQUIRE_FALSE(engine.start({.mode = SessionMode::monitor}));

    // The cushion is both periods: 960 frames.
    std::vector<float> captured(960, 0.5F);
    std::vector<float> out(480, 1.0F);
    platform.source->on_render(out);
    CHECK(std::ranges::all_of(out, [](float sample) { return sample == 0.0F; }));
    platform.sink->on_captured(captured);
    platform.source->on_render(out);
    CHECK(std::ranges::all_of(out, [](float sample) { return sample == 0.5F; }));

    const auto statistics = engine.statistics();
    // 96 + 480 + 960 + 480 + 144 frames at 48 kHz.
    CHECK(statistics.estimated_latency == std::chrono::microseconds(45'000));
    CHECK(statistics.underruns == 0);
    CHECK(statistics.glitches == 4);
}

TEST_CASE("open and start errors leave nothing running") {
    FakePlatform platform;
    AudioEngine engine(platform);

    SECTION("render open fails after capture opened") {
        platform.render_open_error = AudioError{AudioErrorCode::device_not_found};
        CHECK(engine.start({.mode = SessionMode::monitor}) == AudioError{AudioErrorCode::device_not_found});
        CHECK_FALSE(platform.capture_started);
    }

    SECTION("capture start is denied") {
        platform.capture_start_error = AudioError{AudioErrorCode::permission_denied, 5};
        CHECK(engine.start({.mode = SessionMode::meter}) == AudioError{AudioErrorCode::permission_denied, 5});
    }

    const auto statistics = engine.statistics();
    CHECK(statistics.state == EngineState::failed);
    CHECK(statistics.error);
    CHECK_FALSE(statistics.input);
}

TEST_CASE("a stream failure fails the session once and stale failures are ignored") {
    FakePlatform platform;
    std::vector<AudioError> reported;
    AudioEngine engine(platform, [&](AudioError error) { reported.push_back(error); });
    REQUIRE_FALSE(engine.start({.mode = SessionMode::monitor}));

    platform.render_failure(AudioError{AudioErrorCode::device_lost, 7});
    platform.capture_failure(AudioError{AudioErrorCode::os_failure});
    CHECK(reported == std::vector{AudioError{AudioErrorCode::device_lost, 7}});
    CHECK(engine.statistics().state == EngineState::failed);

    auto stale = platform.render_failure;
    REQUIRE_FALSE(engine.start({.mode = SessionMode::tone}));
    stale(AudioError{AudioErrorCode::device_lost});
    CHECK(engine.statistics().state == EngineState::running);
    CHECK(reported.size() == 1);
}

#include <catro/audio/engine.hpp>
#include <catro/platform/windows/audio_platform.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

using namespace std::chrono_literals;
using namespace catro::audio;
using catro::capabilities::AudioEndpointId;
using catro::capabilities::IdentityScope;
using catro::platform::windows::WasapiAudioPlatform;

// Real endpoints: machines without one (CI runners) report SKIP, never a pass.
namespace {

void require_or_skip(const std::optional<AudioError>& error) {
    if (error && (error->code == AudioErrorCode::device_not_found || error->code == AudioErrorCode::permission_denied)) {
        SKIP("no usable default endpoint: " << name(error->code));
    }
    REQUIRE_FALSE(error);
}

} // namespace

TEST_CASE("the default render endpoint plays the test tone") {
    WasapiAudioPlatform platform;
    AudioEngine engine(platform);
    require_or_skip(engine.start({.mode = SessionMode::tone}));
    std::this_thread::sleep_for(500ms);
    const auto statistics = engine.statistics();
    CHECK(statistics.state == EngineState::running);
    REQUIRE(statistics.output);
    CHECK(statistics.output->device.value.starts_with("mmdevice:"));
    CHECK(statistics.output->device_sample_rate > 0);
    CHECK(statistics.output->period_frames > 0);
    // The render callback ran: the meter saw the tone.
    CHECK(statistics.output_peak == Catch::Approx(0.2F).epsilon(0.02));
    engine.stop();
    CHECK(engine.statistics().state == EngineState::idle);
}

TEST_CASE("the default capture endpoint delivers frames") {
    WasapiAudioPlatform platform;
    AudioEngine engine(platform);
    require_or_skip(engine.start({.mode = SessionMode::meter}));
    std::this_thread::sleep_for(500ms);
    const auto statistics = engine.statistics();
    CHECK(statistics.state == EngineState::running);
    REQUIRE(statistics.input);
    CHECK(statistics.input->device.value.starts_with("mmdevice:"));
    CHECK(statistics.input->period_frames > 0);
}

TEST_CASE("a monitor session runs capture into render") {
    WasapiAudioPlatform platform;
    AudioEngine engine(platform);
    require_or_skip(engine.start({.mode = SessionMode::monitor}));
    std::this_thread::sleep_for(1s);
    const auto statistics = engine.statistics();
    CHECK(statistics.state == EngineState::running);
    REQUIRE(statistics.estimated_latency);
    CHECK(*statistics.estimated_latency > 10ms);
    CHECK(*statistics.estimated_latency < 500ms);
}

TEST_CASE("unknown and wrong-direction endpoints are not found") {
    WasapiAudioPlatform platform;
    AudioEngine engine(platform);
    CHECK(engine.start({.mode = SessionMode::meter, .input = AudioEndpointId{"mmdevice:{nope}", IdentityScope::persistent}}) ==
          AudioError{AudioErrorCode::device_not_found});
    CHECK(engine.start({.mode = SessionMode::tone, .output = AudioEndpointId{"coreaudio:x:output", IdentityScope::persistent}}) ==
          AudioError{AudioErrorCode::device_not_found});

    // The default capture endpoint cannot render.
    AudioEngine probe(platform);
    const auto error = probe.start({.mode = SessionMode::meter});
    if (error) {
        SKIP("no default capture endpoint");
    }
    const auto input = probe.statistics().input->device;
    probe.stop();
    CHECK(engine.start({.mode = SessionMode::tone, .output = input}) == AudioError{AudioErrorCode::device_not_found});
}

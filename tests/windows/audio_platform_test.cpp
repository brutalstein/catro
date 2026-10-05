#include <catro/audio/engine.hpp>
#include <catro/platform/windows/audio_platform.hpp>
#include <catro/platform/windows/process_loopback_audio.hpp>
#include "../../platform/windows/src/process_loopback_activation.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <atomic>
#include <cmath>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;
using namespace catro::audio;
using catro::capabilities::AudioEndpointId;
using catro::capabilities::IdentityScope;
using catro::platform::windows::ProcessLoopbackAudioCapture;
using catro::platform::windows::StreamAudioErrorCode;
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

TEST_CASE("process loopback stream audio rejects a zero process id") {
    ProcessLoopbackAudioCapture capture;
    const auto failure =
        capture.start(
            0,
            [](std::span<const float>) noexcept {});
    REQUIRE(failure);
    CHECK(failure->code ==
          StreamAudioErrorCode::initialization_failed);
    CHECK_FALSE(capture.statistics().running);
}

TEST_CASE("late process audio activation owns its event and parameters after the caller leaves") {
    Microsoft::WRL::ComPtr<IActivateAudioInterfaceCompletionHandler> pending;
    HANDLE completed = nullptr;
    {
        auto handler = Microsoft::WRL::Make<catro::platform::windows::detail::ActivationHandler>(42, false);
        REQUIRE(handler);
        completed = handler->completed();
        REQUIRE(completed);
        const auto* parameters = reinterpret_cast<const AUDIOCLIENT_ACTIVATION_PARAMS*>(handler->parameters()->blob.pBlobData);
        CHECK(parameters->ProcessLoopbackParams.TargetProcessId == 42);
        pending = handler;
    }
    CHECK(WaitForSingleObject(completed, 0) == WAIT_TIMEOUT);
    REQUIRE(pending->ActivateCompleted(nullptr) == S_OK);
    CHECK(WaitForSingleObject(completed, 0) == WAIT_OBJECT_0);
}

TEST_CASE("process audio activation can be cancelled before it starts without blocking") {
    ProcessLoopbackAudioCapture capture;
    std::atomic_bool cancelled{true};
    const auto started = std::chrono::steady_clock::now();
    const auto failure = capture.start(GetCurrentProcessId(), [](std::span<const float>) {}, false, &cancelled);
    REQUIRE(failure);
    CHECK(failure->native_code == HRESULT_FROM_WIN32(ERROR_CANCELLED));
    CHECK(std::chrono::steady_clock::now() - started < 250ms);
    CHECK_FALSE(capture.statistics().running);
}

TEST_CASE("process loopback captures playback while the original render session continues") {
    WasapiAudioPlatform platform;
    AudioEngine engine(platform);
    require_or_skip(engine.start({.mode = SessionMode::tone}));
    ProcessLoopbackAudioCapture capture;
    std::atomic_bool heard{false};
    const auto failure = capture.start(GetCurrentProcessId(), [&](std::span<const float> samples) noexcept {
        for (const auto sample : samples) {
            if (std::abs(sample) > 0.01F) {
                heard.store(true, std::memory_order_relaxed);
                break;
            }
        }
    });
    if (failure && failure->code == StreamAudioErrorCode::unsupported) {
        SKIP("process loopback is not supported on this Windows version");
    }
    REQUIRE_FALSE(failure);
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!heard.load(std::memory_order_relaxed) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(10ms);
    }
    CHECK(heard.load(std::memory_order_relaxed));
    CHECK(capture.statistics().running);
    CHECK(engine.statistics().state == EngineState::running);
    CHECK(engine.statistics().output_peak == Catch::Approx(0.2F).epsilon(0.02));
    capture.stop();
    CHECK(engine.statistics().state == EngineState::running);
}

TEST_CASE("stream audio callback failure is reported without terminating the application") {
    catro::platform::windows::WasapiStreamAudioRenderer renderer;
    const auto failure = renderer.start([](std::span<float>) { throw std::runtime_error("callback failed"); });
    if (failure && failure->code == StreamAudioErrorCode::device_unavailable) {
        SKIP("no default render endpoint");
    }
    REQUIRE_FALSE(failure);
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (renderer.statistics().running && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(10ms);
    }
    CHECK_FALSE(renderer.statistics().running);
    REQUIRE(renderer.statistics().error);
    CHECK(renderer.statistics().error->native_code == E_FAIL);
    renderer.stop();
}

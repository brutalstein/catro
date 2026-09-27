#include "fixtures/capability_fixtures.hpp"

#include <AudioViewModel.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <string>

using namespace catro::capabilities;
using namespace catro::app;
using namespace std::chrono_literals;
namespace audio = catro::audio;
namespace fx = catro::fixtures;

namespace {

bool has_row(const AudioSessionView& view, const std::string& label, const std::string& value) {
    return std::ranges::any_of(view.rows, [&](const DiagnosticsRow& row) {
        return row.label == label && row.value.find(value) != std::string::npos;
    });
}

audio::AudioStatistics monitor_statistics() {
    audio::AudioStatistics statistics;
    statistics.state = audio::EngineState::running;
    statistics.mode = audio::SessionMode::monitor;
    statistics.input = audio::StreamInfo{fx::microphone(), 16000, 1, 480, 0};
    statistics.output = audio::StreamInfo{fx::speakers(), 48000, 2, 480, 96};
    statistics.input_peak = 1.0F;
    statistics.estimated_latency = 40ms;
    return statistics;
}

} // namespace

TEST_CASE("device choices start with the default and follow the snapshot") {
    const auto snapshot = fx::valid_snapshot();
    const auto inputs = audio_choices(snapshot, AudioDirection::input);
    REQUIRE(inputs.size() == 2);
    CHECK_FALSE(inputs[0].id);
    CHECK(inputs[0].label == "System default (communications)");
    CHECK(inputs[1] == AudioDeviceChoice{fx::microphone(), "Microphone — 48 kHz (default communications)"});
    const auto outputs = audio_choices(snapshot, AudioDirection::output);
    REQUIRE(outputs.size() == 2);
    CHECK(outputs[1].id == fx::speakers());
}

TEST_CASE("inactive endpoints are left out and unnamed ones show their id") {
    auto snapshot = fx::valid_snapshot();
    for (auto& state : snapshot.runtime.audio_endpoints) {
        if (state.endpoint == fx::speakers()) {
            state.active = Observed<bool>::known(false, fx::measured(fx::kAudioProbe));
        }
        state.default_roles = Observed<std::vector<AudioRole>>::unknown(fx::measured(fx::kAudioProbe));
    }
    for (auto& endpoint : snapshot.devices.audio_endpoints) {
        endpoint.name = Observed<std::string>::unknown(fx::measured(fx::kAudioProbe));
    }
    CHECK(audio_choices(snapshot, AudioDirection::output).size() == 1);
    const auto inputs = audio_choices(snapshot, AudioDirection::input);
    REQUIRE(inputs.size() == 2);
    CHECK(inputs[1].label == fx::microphone().value + " — 48 kHz");
}


TEST_CASE("narrowband input devices are labelled explicitly") {
    auto snapshot = fx::valid_snapshot();
    for (auto& endpoint : snapshot.devices.audio_endpoints) {
        if (endpoint.id == fx::microphone()) {
            endpoint.sample_rate_hz = fx::known<std::uint32_t>(16'000U, fx::advertised(fx::kAudioProbe));
        }
    }
    const auto inputs = audio_choices(snapshot, AudioDirection::input);
    REQUIRE(inputs.size() == 2);
    CHECK(inputs[1].label.find("16 kHz (narrowband)") != std::string::npos);
}

TEST_CASE("levels map -60 dBFS to empty and full scale to full") {
    CHECK(audio_level(1.0F) == AudioLevel{1.0, "0.0 dBFS"});
    CHECK(audio_level(0.0F) == AudioLevel{0.0, "-120.0 dBFS"});
    CHECK(audio_level(0.001F).fraction == 0.0);
}

TEST_CASE("a running monitor session shows streams and an estimated latency") {
    auto statistics = monitor_statistics();
    auto view = describe_audio(statistics);
    CHECK(view.status == "Running monitor");
    CHECK(view.tone == Tone::positive);
    CHECK(view.running);
    REQUIRE(view.input);
    CHECK(view.input->fraction == 1.0);
    REQUIRE(view.output);
    CHECK(has_row(view, "Input device format", "16000 Hz, 1 ch"));
    CHECK(has_row(view, "Output device latency", "2.0 ms"));
    CHECK(has_row(view, "Estimated latency", "40.0 ms (from device and buffer sizes, not measured)"));

    statistics.glitches = 1;
    CHECK(describe_audio(statistics).tone == Tone::positive);
    statistics.glitches = 4;
    CHECK(describe_audio(statistics).tone == Tone::caution);
    statistics.glitches = 0;
    statistics.underruns = 1;
    CHECK(describe_audio(statistics).tone == Tone::caution);
}

TEST_CASE("idle and failed sessions say they are stopped") {
    const auto idle = describe_audio({});
    CHECK(idle.status == "Stopped");
    CHECK_FALSE(idle.running);
    CHECK(idle.rows.empty());

    auto statistics = monitor_statistics();
    statistics.state = audio::EngineState::failed;
    statistics.error = audio::AudioError{audio::AudioErrorCode::device_lost};
    const auto failed = describe_audio(statistics);
    CHECK(failed.status == "Stopped: device lost");
    CHECK(failed.tone == Tone::critical);
    CHECK_FALSE(failed.running);
}

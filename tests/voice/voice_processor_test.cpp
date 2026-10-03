#include <catro/voice/pipeline.hpp>
#include <catro/voice/voice_processor.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <variant>

using namespace catro::voice;

namespace {

// Deterministic white noise in [-amplitude, amplitude].
class Noise {
public:
    Noise(float amplitude, std::uint32_t seed) : amplitude_(amplitude), state_(seed) {}

    void fill(PcmFrame& frame) {
        for (auto& sample : frame) {
            state_ = state_ * 1664525U + 1013904223U;
            sample = amplitude_ * (static_cast<float>(state_ >> 8) / 8388608.0F - 1.0F);
        }
    }

private:
    float amplitude_;
    std::uint32_t state_;
};

double energy(const PcmFrame& frame) {
    double total = 0.0;
    for (const auto sample : frame) {
        total += static_cast<double>(sample) * sample;
    }
    return total;
}

} // namespace

TEST_CASE("echo cancellation removes the far end from the microphone") {
    auto processor = VoiceProcessor::create(
        {.echo_cancellation = true, .noise_suppression = false, .automatic_gain = false});
    REQUIRE(processor);

    // The microphone hears the speakers 60 ms later at half amplitude, and nothing else.
    constexpr std::size_t kDelayFrames = 3;
    Noise far_end(0.3F, 7);
    std::deque<PcmFrame> in_flight;
    double input = 0.0;
    double output = 0.0;
    for (int tick = 0; tick < 300; ++tick) { // 6 s
        PcmFrame render{};
        far_end.fill(render);
        processor->analyze_render(render);
        in_flight.push_back(render);

        PcmFrame capture{};
        if (in_flight.size() > kDelayFrames) {
            for (std::size_t sample = 0; sample < capture.size(); ++sample) {
                capture[sample] = 0.5F * in_flight.front()[sample];
            }
            in_flight.pop_front();
        }
        const auto before = energy(capture);
        processor->process_capture(capture);
        if (tick >= 250) { // the last second, after convergence
            input += before;
            output += energy(capture);
        }
    }
    REQUIRE(input > 0.0);
    CHECK(output < input * 0.01); // at least 20 dB of echo removed
}

TEST_CASE("echo cancellation keeps the local voice when the speakers are silent") {
    auto processor = VoiceProcessor::create(
        {.echo_cancellation = true, .noise_suppression = false, .automatic_gain = false});
    REQUIRE(processor);

    Noise voice(0.2F, 3);
    double input = 0.0;
    double output = 0.0;
    for (int tick = 0; tick < 150; ++tick) {
        PcmFrame render{};
        processor->analyze_render(render);
        PcmFrame capture{};
        voice.fill(capture);
        const auto before = energy(capture);
        processor->process_capture(capture);
        if (tick >= 100) {
            input += before;
            output += energy(capture);
        }
    }
    CHECK(output > input * 0.5); // within 3 dB
}

TEST_CASE("noise suppression attenuates steady background noise") {
    auto processor = VoiceProcessor::create(
        {.echo_cancellation = false, .noise_suppression = true, .automatic_gain = false});
    REQUIRE(processor);

    Noise fan(0.03F, 11);
    double input = 0.0;
    double output = 0.0;
    for (int tick = 0; tick < 200; ++tick) { // 4 s
        PcmFrame capture{};
        fan.fill(capture);
        const auto before = energy(capture);
        processor->process_capture(capture);
        if (tick >= 150) {
            input += before;
            output += energy(capture);
        }
    }
    CHECK(output < input * 0.1); // at least 10 dB of steady noise removed
}

TEST_CASE("voice processing stays off unless the pipeline asks for it") {
    VoicePipelineConfig config;
    auto plain = VoicePipeline::create(config);
    REQUIRE(std::holds_alternative<std::unique_ptr<VoicePipeline>>(plain));
    CHECK_FALSE(std::get<std::unique_ptr<VoicePipeline>>(plain)->processing_available());

    config.processing = VoiceProcessingConfig{};
    auto processed = VoicePipeline::create(config);
    REQUIRE(std::holds_alternative<std::unique_ptr<VoicePipeline>>(processed));
    auto& pipeline = *std::get<std::unique_ptr<VoicePipeline>>(processed);
    CHECK(pipeline.processing_available());
    pipeline.set_processing({.echo_cancellation = false, .noise_suppression = true, .automatic_gain = true});
}

#include <catro/voice/pipeline.hpp>
#include <catro/voice/stream_controls.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <vector>
#include <cstdint>
#include <memory>
#include <optional>
#include <variant>

using namespace catro::voice;

namespace {

std::unique_ptr<VoicePipeline> make_pipeline(std::uint32_t stream_id, StreamControls* controls) {
    VoicePipelineConfig config{.local_stream_id = stream_id, .jitter_target_packets = 1};
    config.controls = controls;
    auto result = VoicePipeline::create(config);
    REQUIRE(std::holds_alternative<std::unique_ptr<VoicePipeline>>(result));
    return std::move(std::get<std::unique_ptr<VoicePipeline>>(result));
}

PcmFrame tone(double& phase, float amplitude) {
    PcmFrame frame{};
    for (auto& sample : frame) {
        sample = amplitude * static_cast<float>(std::sin(phase));
        phase += 2.0 * 3.14159265358979323846 * 300.0 / static_cast<double>(kSampleRate);
    }
    return frame;
}

OutboundDatagram encode(VoicePipeline& pipeline, const PcmFrame& frame) {
    pipeline.capture().on_captured(frame);
    OutboundDatagram datagram;
    const auto result = pipeline.encode_next(datagram);
    REQUIRE(std::holds_alternative<EncodeStep>(result));
    REQUIRE(std::get<EncodeStep>(result) == EncodeStep::packet_ready);
    return datagram;
}

double rendered_energy(VoicePipeline& pipeline) {
    PcmFrame rendered{};
    pipeline.render().on_render(rendered);
    double energy = 0.0;
    for (const auto sample : rendered) {
        energy += static_cast<double>(sample) * sample;
    }
    return energy;
}

} // namespace

TEST_CASE("user stream ids match the identity-byte derivation on every platform") {
    // Same fold the shells used over the first four identity bytes.
    std::uint32_t expected = 0x4354524fU;
    for (const std::uint32_t byte : {0xABU, 0x01U, 0xFFU, 0x10U}) {
        expected = (expected << 5U) ^ (expected >> 27U) ^ byte;
    }
    CHECK(user_stream_id("ab01ff10deadbeef") == expected);
    CHECK(user_stream_id("AB01FF10") == expected);
    CHECK(user_stream_id("ab01ff10") != user_stream_id("ab01ff11"));
}

TEST_CASE("stream volumes are clamped, stored, and released at unity") {
    StreamControls controls;
    CHECK(controls.volume(7) == 1.0F);
    controls.set_volume(7, 0.25F);
    controls.set_volume(9, 5.0F);
    controls.set_volume(11, -1.0F);
    CHECK(controls.volume(7) == 0.25F);
    CHECK(controls.volume(9) == kMaxStreamVolume);
    CHECK(controls.volume(11) == 0.0F);
    controls.set_volume(7, 1.0F);
    CHECK(controls.volume(7) == 1.0F);
    controls.set_volume(0, 0.5F);
    CHECK(controls.volume(0) == 1.0F);
}

TEST_CASE("speaking set tracks streams without duplicates") {
    StreamControls controls;
    controls.set_speaking(5, true);
    controls.set_speaking(5, true);
    controls.set_speaking(6, true);
    std::array<std::uint32_t, kMaxControlledStreams> out{};
    CHECK(controls.speaking(out) == 2);
    controls.set_speaking(5, false);
    REQUIRE(controls.speaking(out) == 1);
    CHECK(out[0] == 6);
}

TEST_CASE("pipeline reports who speaks and applies per-user volume") {
    StreamControls sender_controls;
    StreamControls receiver_controls;
    auto sender = make_pipeline(0x91000001U, &sender_controls);
    auto receiver = make_pipeline(0x91000002U, &receiver_controls);
    double phase = 0.0;

    // The sender's own microphone lights up its local indicator.
    const auto loud = encode(*sender, tone(phase, 0.2F));
    CHECK(sender_controls.local_speaking());

    REQUIRE(std::holds_alternative<JitterPushResult>(receiver->receive(loud.view())));
    REQUIRE(std::holds_alternative<DecodeStep>(receiver->decode_next()));
    std::array<std::uint32_t, kMaxControlledStreams> speaking{};
    REQUIRE(receiver_controls.speaking(speaking) == 1);
    CHECK(speaking[0] == 0x91000001U);
    CHECK(rendered_energy(*receiver) > 1e-6);

    // Muting a user locally silences them but still shows them speaking.
    receiver_controls.set_volume(0x91000001U, 0.0F);
    const auto next = encode(*sender, tone(phase, 0.2F));
    REQUIRE(std::holds_alternative<JitterPushResult>(receiver->receive(next.view())));
    REQUIRE(std::holds_alternative<DecodeStep>(receiver->decode_next()));
    CHECK(rendered_energy(*receiver) == 0.0);
    CHECK(receiver_controls.speaking(speaking) == 1);

    // Mute clears the local indicator once the hangover ends.
    sender->set_muted(true);
    for (int frame = 0; frame < 20; ++frame) {
        (void)encode(*sender, tone(phase, 0.2F));
    }
    CHECK_FALSE(sender_controls.local_speaking());

    receiver.reset();
    CHECK(receiver_controls.speaking(speaking) == 0);
}

TEST_CASE("voice activity gate sends quiet input as silence and reports the input level") {
    StreamControls controls;
    auto pipeline = make_pipeline(0x91000003U, &controls);
    double phase = 0.0;

    // Without a threshold every frame passes.
    (void)encode(*pipeline, tone(phase, 0.001F));
    CHECK(pipeline->statistics().gated_frames == 0);

    // Automatic: a -63 dBFS hum stays closed, speech opens the gate.
    pipeline->set_input_threshold(std::nullopt);
    for (int frame = 0; frame < 20; ++frame) {
        (void)encode(*pipeline, tone(phase, 0.001F));
    }
    CHECK(pipeline->statistics().gated_frames >= 5);
    CHECK_FALSE(controls.local_speaking());
    CHECK(controls.local_level() < -55.0F);
    const auto gated = pipeline->statistics().gated_frames;
    (void)encode(*pipeline, tone(phase, 0.2F));
    CHECK(pipeline->statistics().gated_frames == gated);
    CHECK(controls.local_speaking());
    CHECK(controls.local_level() > -20.0F);

    // A manual -10 dBFS threshold closes on a -17 dBFS voice once the hangover ends.
    pipeline->set_input_threshold(-10.0F);
    for (int frame = 0; frame < 20; ++frame) {
        (void)encode(*pipeline, tone(phase, 0.2F));
    }
    CHECK_FALSE(controls.local_speaking());
}

TEST_CASE("stream audio joins the echo reference as mono without growing latency") {
    StreamControls controls;
    std::vector<float> stereo(2 * kFrameSamples);
    for (std::size_t frame = 0; frame < kFrameSamples; ++frame) {
        stereo[2 * frame] = 0.5F;
        stereo[2 * frame + 1] = 0.3F;
    }
    PcmFrame mix{};
    mix.fill(0.25F);
    controls.add_echo_reference(stereo);
    controls.mix_echo_reference(mix);
    CHECK(mix[0] == 0.25F + 0.4F);
    CHECK(mix[kFrameSamples - 1] == 0.25F + 0.4F);

    // Nothing pending: the voice mix stays as it is.
    mix.fill(0.25F);
    controls.mix_echo_reference(mix);
    CHECK(mix[0] == 0.25F);

    // A backlog from an idle mixer is skipped to the newest frame, and a full ring drops input
    // instead of blocking the stream thread.
    for (int frame = 0; frame < 20; ++frame) {
        for (std::size_t sample = 0; sample < stereo.size(); ++sample) {
            stereo[sample] = static_cast<float>(frame) / 100.0F;
        }
        controls.add_echo_reference(stereo);
    }
    mix.fill(0.0F);
    controls.mix_echo_reference(mix);
    CHECK(mix[0] == 0.07F);
    mix.fill(0.0F);
    controls.mix_echo_reference(mix);
    CHECK(mix[0] == 0.0F);
}

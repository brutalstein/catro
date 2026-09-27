#include <catro/voice/pipeline.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <limits>
#include <span>
#include <variant>
#include <vector>

using namespace catro::voice;

namespace {

std::unique_ptr<VoicePipeline> make_pipeline(std::uint32_t stream_id, std::uint16_t jitter_target = 1,
                                             std::size_t render_queue_frames = 8,
                                             std::uint16_t initial_sequence = 0,
                                             std::uint32_t initial_timestamp = 0) {
    VoicePipelineConfig config{
        .local_stream_id = stream_id,
        .initial_sequence = initial_sequence,
        .initial_timestamp = initial_timestamp,
        .jitter_target_packets = jitter_target,
        .capture_queue_frames = 8,
        .render_queue_frames = render_queue_frames,
    };
    auto result = VoicePipeline::create(config);
    REQUIRE(std::holds_alternative<std::unique_ptr<VoicePipeline>>(result));
    return std::move(std::get<std::unique_ptr<VoicePipeline>>(result));
}

PcmFrame tone_frame(double& phase, float amplitude) {
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
    REQUIRE(datagram.size > kVoiceHeaderBytes);
    return datagram;
}

bool finite_nonzero(std::span<const float> pcm) {
    double energy = 0.0;
    for (const auto sample : pcm) {
        if (!std::isfinite(sample)) {
            return false;
        }
        energy += static_cast<double>(sample) * sample;
    }
    return energy > 1e-8;
}

} // namespace

TEST_CASE("pipeline rejects an invalid local stream identity") {
    VoicePipelineConfig config;
    config.local_stream_id = 0;
    const auto result = VoicePipeline::create(config);
    REQUIRE(std::holds_alternative<CodecError>(result));
    CHECK(std::get<CodecError>(result).code == CodecErrorCode::invalid_argument);
}

TEST_CASE("capture to Opus packet path increments sequence and timestamp across wrap") {
    constexpr auto kTimestampBeforeWrap =
        std::numeric_limits<std::uint32_t>::max() - kFrameSamples + 1U;
    auto pipeline = make_pipeline(0x12345678U, 1, 8, 65535U, kTimestampBeforeWrap);
    double phase = 0.0;

    auto first = encode(*pipeline, tone_frame(phase, 0.15F));
    auto parsed = parse_packet(first.view());
    REQUIRE(std::holds_alternative<VoicePacketView>(parsed));
    auto packet = std::get<VoicePacketView>(parsed);
    CHECK(packet.stream_id == 0x12345678U);
    CHECK(packet.sequence == 65535U);
    CHECK(packet.timestamp == kTimestampBeforeWrap);

    auto second = encode(*pipeline, tone_frame(phase, 0.15F));
    parsed = parse_packet(second.view());
    REQUIRE(std::holds_alternative<VoicePacketView>(parsed));
    packet = std::get<VoicePacketView>(parsed);
    CHECK(packet.sequence == 0U);
    CHECK(packet.timestamp == 0U);

    OutboundDatagram none;
    const auto empty = pipeline->encode_next(none);
    REQUIRE(std::holds_alternative<EncodeStep>(empty));
    CHECK(std::get<EncodeStep>(empty) == EncodeStep::no_frame);
    CHECK(none.size == 0);

    const auto stats = pipeline->statistics();
    CHECK(stats.encoded_frames == 2);
    CHECK(stats.encode_errors == 0);
    CHECK(stats.outbound_bytes == first.size + second.size);
    CHECK(stats.capture.frames_dequeued == 2);
}

TEST_CASE("sender packet clock advances across intentionally discarded capture backlog") {
    auto pipeline = make_pipeline(0x12121212U, 1, 8, 100U, 10'000U);
    double phase = 0.0;

    const auto one = tone_frame(phase, 0.10F);
    const auto two = tone_frame(phase, 0.11F);
    const auto three = tone_frame(phase, 0.12F);
    const auto four = tone_frame(phase, 0.13F);
    pipeline->capture().on_captured(one);
    pipeline->capture().on_captured(two);
    pipeline->capture().on_captured(three);
    pipeline->capture().on_captured(four);

    OutboundDatagram datagram;
    const auto encoded = pipeline->encode_next(datagram);
    REQUIRE(std::holds_alternative<EncodeStep>(encoded));
    REQUIRE(std::get<EncodeStep>(encoded) == EncodeStep::packet_ready);

    const auto parsed = parse_packet(datagram.view());
    REQUIRE(std::holds_alternative<VoicePacketView>(parsed));
    const auto packet = std::get<VoicePacketView>(parsed);
    CHECK(packet.sequence == 102U);
    CHECK(packet.timestamp == 10'000U + 2U * kFrameSamples);
    CHECK(pipeline->statistics().capture.stale_frames_discarded == 2);
    CHECK(pipeline->statistics().capture.timeline_frames_skipped == 2);
}

TEST_CASE("capture overflow creates a packet-clock gap instead of time-compressing speech") {
    auto pipeline = make_pipeline(0x13131313U, 1, 4, 500U, 20'000U);
    double phase = 0.0;

    for (int index = 0; index < 5; ++index) {
        pipeline->capture().on_captured(tone_frame(phase, 0.1F));
    }

    OutboundDatagram datagram;
    auto result = pipeline->encode_next(datagram);
    REQUIRE(std::holds_alternative<EncodeStep>(result));
    CHECK(std::get<EncodeStep>(result) == EncodeStep::no_frame);

    pipeline->capture().on_captured(tone_frame(phase, 0.1F));
    result = pipeline->encode_next(datagram);
    REQUIRE(std::holds_alternative<EncodeStep>(result));
    REQUIRE(std::get<EncodeStep>(result) == EncodeStep::packet_ready);

    const auto parsed = parse_packet(datagram.view());
    REQUIRE(std::holds_alternative<VoicePacketView>(parsed));
    const auto packet = std::get<VoicePacketView>(parsed);
    CHECK(packet.sequence == 505U);
    CHECK(packet.timestamp == 20'000U + 5U * kFrameSamples);

    const auto stats = pipeline->statistics().capture;
    CHECK(stats.dropped_callbacks == 1);
    CHECK(stats.resync_events == 1);
    CHECK(stats.timeline_frames_skipped == 5);
}

TEST_CASE("two pipelines restore reordered datagrams and queue decoded audio") {
    auto sender = make_pipeline(0x11111111U, 1);
    auto receiver = make_pipeline(0x22222222U, 3);
    double phase = 0.0;

    std::array<OutboundDatagram, 3> packets{
        encode(*sender, tone_frame(phase, 0.10F)),
        encode(*sender, tone_frame(phase, 0.15F)),
        encode(*sender, tone_frame(phase, 0.20F)),
    };

    CHECK(std::get<JitterPushResult>(receiver->receive(packets[1].view())) == JitterPushResult::accepted);
    CHECK(std::get<JitterPushResult>(receiver->receive(packets[0].view())) == JitterPushResult::accepted);
    CHECK(std::get<JitterPushResult>(receiver->receive(packets[2].view())) == JitterPushResult::accepted);

    for (int index = 0; index < 3; ++index) {
        const auto step = receiver->decode_next();
        REQUIRE(std::holds_alternative<DecodeStep>(step));
        CHECK(std::get<DecodeStep>(step) == DecodeStep::queued_packet);
    }

    PcmFrame rendered{};
    for (int index = 0; index < 3; ++index) {
        receiver->render().on_render(rendered);
        CHECK(finite_nonzero(rendered));
    }

    const auto stats = receiver->statistics();
    CHECK(stats.received_datagrams == 3);
    CHECK(stats.malformed_datagrams == 0);
    CHECK(stats.decoded_frames == 3);
    CHECK(stats.decode_errors == 0);
    CHECK(stats.render_queue_full == 0);
    CHECK(stats.jitter.reordered == 1);
    CHECK(stats.jitter.played == 3);
    CHECK(stats.render.underrun_callbacks == 0);
}

TEST_CASE("receiver resynchronization drops stale jitter state and accepts a restarted remote stream") {
    auto first_sender = make_pipeline(0x10101010U, 1);
    auto receiver = make_pipeline(0x20202020U, 2);
    double phase = 0.0;

    const auto first = encode(*first_sender, tone_frame(phase, 0.12F));
    const auto second = encode(*first_sender, tone_frame(phase, 0.13F));
    REQUIRE(std::get<JitterPushResult>(receiver->receive(first.view())) == JitterPushResult::accepted);
    REQUIRE(std::get<JitterPushResult>(receiver->receive(second.view())) == JitterPushResult::accepted);
    REQUIRE(std::get<DecodeStep>(receiver->decode_next()) == DecodeStep::queued_packet);
    CHECK(receiver->statistics().jitter.buffered == 1);

    REQUIRE_FALSE(receiver->resynchronize_receiver());
    auto stats = receiver->statistics();
    CHECK(stats.jitter.resyncs == 1);
    CHECK(stats.jitter.resync_discarded_packets == 1);
    CHECK(stats.jitter.buffered == 0);
    CHECK(receiver->next_playout_kind() == PlayoutKind::waiting);

    auto restarted_sender = make_pipeline(0x30303030U, 1);
    const auto restarted_first = encode(*restarted_sender, tone_frame(phase, 0.14F));
    const auto restarted_second = encode(*restarted_sender, tone_frame(phase, 0.15F));
    REQUIRE(std::get<JitterPushResult>(receiver->receive(restarted_first.view())) == JitterPushResult::accepted);
    REQUIRE(std::get<JitterPushResult>(receiver->receive(restarted_second.view())) == JitterPushResult::accepted);
    CHECK(std::get<DecodeStep>(receiver->decode_next()) == DecodeStep::queued_packet);
    CHECK(receiver->statistics().jitter.wrong_stream == 0);
}

TEST_CASE("pipeline uses FEC for one missing packet and keeps the following packet") {
    auto sender = make_pipeline(0x33333333U, 1);
    auto receiver = make_pipeline(0x44444444U, 1);
    double phase = 0.0;

    const auto first = encode(*sender, tone_frame(phase, 0.12F));
    (void)encode(*sender, tone_frame(phase, 0.18F));
    const auto third = encode(*sender, tone_frame(phase, 0.14F));

    REQUIRE(std::get<JitterPushResult>(receiver->receive(first.view())) == JitterPushResult::accepted);
    auto step = receiver->decode_next();
    REQUIRE(std::holds_alternative<DecodeStep>(step));
    CHECK(std::get<DecodeStep>(step) == DecodeStep::queued_packet);

    REQUIRE(std::get<JitterPushResult>(receiver->receive(third.view())) == JitterPushResult::accepted);
    step = receiver->decode_next();
    REQUIRE(std::holds_alternative<DecodeStep>(step));
    CHECK(std::get<DecodeStep>(step) == DecodeStep::queued_fec);

    step = receiver->decode_next();
    REQUIRE(std::holds_alternative<DecodeStep>(step));
    CHECK(std::get<DecodeStep>(step) == DecodeStep::queued_packet);

    const auto stats = receiver->statistics();
    CHECK(stats.decoded_frames == 3);
    CHECK(stats.jitter.fec == 1);
    CHECK(stats.jitter.buffered == 0);
}

TEST_CASE("pipeline PLC never waits on a missing network packet") {
    auto sender = make_pipeline(0x55555555U, 1);
    auto receiver = make_pipeline(0x66666666U, 1);
    double phase = 0.0;

    const auto first = encode(*sender, tone_frame(phase, 0.1F));
    REQUIRE(std::get<JitterPushResult>(receiver->receive(first.view())) == JitterPushResult::accepted);
    REQUIRE(std::get<DecodeStep>(receiver->decode_next()) == DecodeStep::queued_packet);
    REQUIRE(std::get<DecodeStep>(receiver->decode_next()) == DecodeStep::queued_plc);

    PcmFrame rendered{};
    receiver->render().on_render(rendered);
    CHECK(finite_nonzero(rendered));
    receiver->render().on_render(rendered);
    CHECK(std::ranges::all_of(rendered, [](float sample) { return std::isfinite(sample); }));

    CHECK(receiver->statistics().jitter.plc == 1);
}

TEST_CASE("malformed Opus payload is concealed without terminating the voice pipeline") {
    auto receiver = make_pipeline(0x70000001U, 1);
    const std::array<std::byte, 2> invalid_opus{std::byte{0x03}, std::byte{0x00}};
    std::array<std::byte, kVoiceHeaderBytes + invalid_opus.size()> datagram{};
    const auto serialized = serialize_packet(
        VoicePacketView{.stream_id = 0x70000002U,
                        .sequence = 10,
                        .timestamp = 9'600U,
                        .payload = invalid_opus},
        datagram);
    REQUIRE(std::holds_alternative<std::size_t>(serialized));

    const auto received = receiver->receive(datagram);
    REQUIRE(std::holds_alternative<JitterPushResult>(received));
    REQUIRE(std::get<JitterPushResult>(received) == JitterPushResult::accepted);

    const auto decoded = receiver->decode_next();
    REQUIRE(std::holds_alternative<DecodeStep>(decoded));
    CHECK(std::get<DecodeStep>(decoded) == DecodeStep::queued_plc);

    const auto stats = receiver->statistics();
    CHECK(stats.decode_errors == 1);
    CHECK(stats.decoded_frames == 1);
    CHECK(stats.render.frames_enqueued == 1);
}

TEST_CASE("malformed datagrams are rejected before jitter state changes") {
    auto receiver = make_pipeline(0x77777777U, 1);
    const std::array<std::byte, 4> garbage{
        std::byte{0x00}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03},
    };
    const auto result = receiver->receive(garbage);
    REQUIRE(std::holds_alternative<PacketError>(result));
    CHECK(std::get<PacketError>(result) == PacketError::too_short);

    const auto stats = receiver->statistics();
    CHECK(stats.received_datagrams == 1);
    CHECK(stats.malformed_datagrams == 1);
    CHECK(stats.jitter.accepted == 0);
    CHECK(stats.jitter.buffered == 0);
}

TEST_CASE("decoded audio never grows memory and reports bounded render backpressure") {
    auto sender = make_pipeline(0x88888888U, 1);
    auto receiver = make_pipeline(0x99999999U, 1, 2);
    double phase = 0.0;

    for (int index = 0; index < 3; ++index) {
        const auto packet = encode(*sender, tone_frame(phase, 0.11F));
        REQUIRE(std::get<JitterPushResult>(receiver->receive(packet.view())) == JitterPushResult::accepted);
        const auto step = receiver->decode_next();
        REQUIRE(std::holds_alternative<DecodeStep>(step));
        if (index < 2) {
            CHECK(std::get<DecodeStep>(step) == DecodeStep::queued_packet);
        } else {
            CHECK(std::get<DecodeStep>(step) == DecodeStep::render_queue_full);
        }
    }

    const auto stats = receiver->statistics();
    CHECK(stats.decoded_frames == 3);
    CHECK(stats.render_queue_full == 1);
    CHECK(stats.render.push_rejections == 1);
    CHECK(stats.render.buffered_samples == 2U * kFrameSamples);
}

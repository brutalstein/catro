#include <catro/voice/codec.hpp>
#include <catro/voice/jitter.hpp>
#include <catro/voice/sequence.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <limits>
#include <variant>
#include <vector>

using namespace catro::voice;

namespace {

constexpr std::uint32_t kStream = 0x10203040U;
constexpr std::uint32_t kTimestamp = 4'000'000U;

JitterPushResult push(JitterBuffer& buffer, std::uint16_t sequence, std::uint32_t timestamp,
                      std::byte marker, std::uint32_t stream = kStream) {
    const std::array payload{marker, std::byte{0x55}};
    return buffer.push(VoicePacketView{
        .stream_id = stream,
        .sequence = sequence,
        .timestamp = timestamp,
        .payload = payload,
    });
}

std::vector<std::byte> encode_frame(Encoder& encoder, float amplitude, double& phase) {
    std::array<float, kFrameSamples> pcm{};
    for (auto& sample : pcm) {
        sample = static_cast<float>(amplitude * std::sin(phase));
        phase += 2.0 * 3.14159265358979323846 * 220.0 / static_cast<double>(kSampleRate);
    }
    std::array<std::byte, kMaxOpusPacketBytes> encoded{};
    const auto result = encoder.encode(pcm, encoded);
    REQUIRE(std::holds_alternative<std::size_t>(result));
    const auto size = std::get<std::size_t>(result);
    return {encoded.begin(), encoded.begin() + static_cast<std::ptrdiff_t>(size)};
}

} // namespace

TEST_CASE("16-bit sequence arithmetic is wrap safe") {
    CHECK(sequence_distance(101, 100) == 1);
    CHECK(sequence_distance(100, 101) == -1);
    CHECK(sequence_distance(0, 65535) == 1);
    CHECK(sequence_distance(65535, 0) == -1);
    CHECK(sequence_ahead(0, 65535));
    CHECK_FALSE(sequence_ahead(65535, 0));
}

TEST_CASE("jitter buffer waits for target depth then restores packet order") {
    JitterBuffer buffer(3);
    CHECK(push(buffer, 101, kTimestamp + kFrameSamples, std::byte{0x11}) == JitterPushResult::accepted);
    CHECK(push(buffer, 100, kTimestamp, std::byte{0x10}) == JitterPushResult::accepted);

    PlayoutFrame frame;
    CHECK(buffer.pull(frame) == PlayoutKind::waiting);
    CHECK_FALSE(buffer.started());

    CHECK(push(buffer, 102, kTimestamp + 2U * kFrameSamples, std::byte{0x12}) == JitterPushResult::accepted);
    CHECK(buffer.pull(frame) == PlayoutKind::packet);
    CHECK(frame.sequence == 100);
    CHECK(frame.payload_sequence == 100);
    REQUIRE(frame.payload_size == 2);
    CHECK(frame.payload[0] == std::byte{0x10});

    CHECK(buffer.pull(frame) == PlayoutKind::packet);
    CHECK(frame.sequence == 101);
    CHECK(frame.payload[0] == std::byte{0x11});
    CHECK(buffer.pull(frame) == PlayoutKind::packet);
    CHECK(frame.sequence == 102);
    CHECK(frame.payload[0] == std::byte{0x12});

    const auto stats = buffer.statistics();
    CHECK(stats.accepted == 3);
    CHECK(stats.reordered == 1);
    CHECK(stats.played == 3);
    CHECK(stats.peak_buffered == 3);
    CHECK(stats.buffered == 0);
}

TEST_CASE("duplicates late packets wrong streams and far-ahead packets are bounded") {
    JitterBuffer buffer(1);
    CHECK(push(buffer, 10, kTimestamp, std::byte{1}) == JitterPushResult::accepted);
    CHECK(push(buffer, 10, kTimestamp, std::byte{1}) == JitterPushResult::duplicate);
    CHECK(push(buffer, 11, kTimestamp + kFrameSamples, std::byte{2}, kStream + 1U) ==
          JitterPushResult::wrong_stream);

    PlayoutFrame frame;
    REQUIRE(buffer.pull(frame) == PlayoutKind::packet);
    CHECK(push(buffer, 10, kTimestamp, std::byte{1}) == JitterPushResult::late);
    CHECK(push(buffer, 11U + static_cast<std::uint16_t>(kJitterCapacityPackets),
               kTimestamp + (1U + static_cast<std::uint32_t>(kJitterCapacityPackets)) * kFrameSamples,
               std::byte{3}) == JitterPushResult::outside_window);

    const auto stats = buffer.statistics();
    CHECK(stats.duplicates == 1);
    CHECK(stats.late == 1);
    CHECK(stats.wrong_stream == 1);
    CHECK(stats.outside_window == 1);
}

TEST_CASE("jitter preview never consumes media and distinguishes FEC from PLC") {
    JitterBuffer buffer(1);
    REQUIRE(push(buffer, 100, kTimestamp, std::byte{0x10}) == JitterPushResult::accepted);
    CHECK(buffer.peek() == PlayoutKind::packet);
    CHECK(buffer.statistics().buffered == 1);

    PlayoutFrame frame;
    REQUIRE(buffer.pull(frame) == PlayoutKind::packet);
    CHECK(buffer.statistics().buffered == 0);
    CHECK(buffer.peek() == PlayoutKind::plc);

    REQUIRE(push(buffer, 102, kTimestamp + 2U * kFrameSamples, std::byte{0x12}) ==
            JitterPushResult::accepted);
    CHECK(buffer.peek() == PlayoutKind::fec);
    CHECK(buffer.statistics().buffered == 1);
    CHECK(buffer.peek() == PlayoutKind::fec);
    CHECK(buffer.statistics().buffered == 1);
}

TEST_CASE("a missing packet uses the following packet for FEC without consuming it") {
    JitterBuffer buffer(1);
    REQUIRE(push(buffer, 100, kTimestamp, std::byte{0x10}) == JitterPushResult::accepted);

    PlayoutFrame frame;
    REQUIRE(buffer.pull(frame) == PlayoutKind::packet);
    REQUIRE(push(buffer, 102, kTimestamp + 2U * kFrameSamples, std::byte{0x12}) == JitterPushResult::accepted);

    CHECK(buffer.pull(frame) == PlayoutKind::fec);
    CHECK(frame.sequence == 101);
    CHECK(frame.payload_sequence == 102);
    CHECK(frame.timestamp == kTimestamp + kFrameSamples);
    CHECK(frame.payload[0] == std::byte{0x12});
    CHECK(buffer.statistics().buffered == 1);

    CHECK(buffer.pull(frame) == PlayoutKind::packet);
    CHECK(frame.sequence == 102);
    CHECK(frame.payload_sequence == 102);
    CHECK(buffer.statistics().buffered == 0);
    CHECK(buffer.statistics().fec == 1);
}

TEST_CASE("missing data falls back to PLC and late recovery is rejected") {
    JitterBuffer buffer(1);
    REQUIRE(push(buffer, 7, kTimestamp, std::byte{7}) == JitterPushResult::accepted);
    PlayoutFrame frame;
    REQUIRE(buffer.pull(frame) == PlayoutKind::packet);

    CHECK(buffer.pull(frame) == PlayoutKind::plc);
    CHECK(frame.sequence == 8);
    CHECK(frame.payload_size == 0);
    CHECK(push(buffer, 8, kTimestamp + kFrameSamples, std::byte{8}) == JitterPushResult::late);
    CHECK(buffer.statistics().plc == 1);
}

TEST_CASE("timestamp discontinuities are concealed rather than played") {
    JitterBuffer buffer(1);
    REQUIRE(push(buffer, 20, kTimestamp, std::byte{1}) == JitterPushResult::accepted);
    PlayoutFrame frame;
    REQUIRE(buffer.pull(frame) == PlayoutKind::packet);

    REQUIRE(push(buffer, 21, kTimestamp + kFrameSamples + 1U, std::byte{2}) == JitterPushResult::accepted);
    CHECK(buffer.pull(frame) == PlayoutKind::plc);
    CHECK(buffer.statistics().timestamp_mismatches == 1);
    CHECK(buffer.statistics().buffered == 0);
}

TEST_CASE("reordering across sequence wrap plays 65535 then zero then one") {
    JitterBuffer buffer(3);
    REQUIRE(push(buffer, 0, kTimestamp + kFrameSamples, std::byte{0}) == JitterPushResult::accepted);
    REQUIRE(push(buffer, 65535, kTimestamp, std::byte{0xff}) == JitterPushResult::accepted);
    REQUIRE(push(buffer, 1, kTimestamp + 2U * kFrameSamples, std::byte{1}) == JitterPushResult::accepted);

    PlayoutFrame frame;
    REQUIRE(buffer.pull(frame) == PlayoutKind::packet);
    CHECK(frame.sequence == 65535);
    REQUIRE(buffer.pull(frame) == PlayoutKind::packet);
    CHECK(frame.sequence == 0);
    REQUIRE(buffer.pull(frame) == PlayoutKind::packet);
    CHECK(frame.sequence == 1);
}

TEST_CASE("jitter playout accepts timestamp wrap at the 32-bit boundary") {
    constexpr auto before_wrap = std::numeric_limits<std::uint32_t>::max() - kFrameSamples + 1U;
    JitterBuffer buffer(3);
    REQUIRE(push(buffer, 65535, before_wrap, std::byte{1}) == JitterPushResult::accepted);
    REQUIRE(push(buffer, 0, 0U, std::byte{2}) == JitterPushResult::accepted);
    REQUIRE(push(buffer, 1, kFrameSamples, std::byte{3}) == JitterPushResult::accepted);

    PlayoutFrame frame;
    REQUIRE(buffer.pull(frame) == PlayoutKind::packet);
    CHECK(frame.sequence == 65535);
    CHECK(frame.timestamp == before_wrap);
    REQUIRE(buffer.pull(frame) == PlayoutKind::packet);
    CHECK(frame.sequence == 0);
    CHECK(frame.timestamp == 0U);
    REQUIRE(buffer.pull(frame) == PlayoutKind::packet);
    CHECK(frame.sequence == 1);
    CHECK(frame.timestamp == kFrameSamples);
    CHECK(buffer.statistics().timestamp_mismatches == 0);
}

TEST_CASE("reset clears stream identity playout state and counters") {
    JitterBuffer buffer(1);
    REQUIRE(push(buffer, 1, kTimestamp, std::byte{1}) == JitterPushResult::accepted);
    PlayoutFrame frame;
    REQUIRE(buffer.pull(frame) == PlayoutKind::packet);
    buffer.reset();

    CHECK_FALSE(buffer.started());
    CHECK(buffer.statistics() == JitterStatistics{});
    CHECK(push(buffer, 50, 77, std::byte{2}, kStream + 99U) == JitterPushResult::accepted);
}

TEST_CASE("jitter FEC and PLC decisions drive the Opus decoder without errors") {
    auto encoder_result = Encoder::create();
    REQUIRE(std::holds_alternative<std::unique_ptr<Encoder>>(encoder_result));
    auto encoder = std::move(std::get<std::unique_ptr<Encoder>>(encoder_result));

    auto decoder_result = Decoder::create();
    REQUIRE(std::holds_alternative<std::unique_ptr<Decoder>>(decoder_result));
    auto decoder = std::move(std::get<std::unique_ptr<Decoder>>(decoder_result));

    double phase = 0.0;
    const auto first = encode_frame(*encoder, 0.12F, phase);
    (void)encode_frame(*encoder, 0.18F, phase); // deliberately lost packet 101
    const auto third = encode_frame(*encoder, 0.15F, phase);

    JitterBuffer buffer(1);
    REQUIRE(buffer.push(VoicePacketView{.stream_id = kStream, .sequence = 100, .timestamp = kTimestamp,
                                        .payload = first}) == JitterPushResult::accepted);

    PlayoutFrame frame;
    REQUIRE(buffer.pull(frame) == PlayoutKind::packet);
    std::array<float, kFrameSamples> pcm{};
    auto decoded = decoder->decode(frame.payload_view(), pcm);
    REQUIRE(std::holds_alternative<std::size_t>(decoded));
    CHECK(std::get<std::size_t>(decoded) == kFrameSamples);

    REQUIRE(buffer.push(VoicePacketView{.stream_id = kStream, .sequence = 102,
                                        .timestamp = kTimestamp + 2U * kFrameSamples,
                                        .payload = third}) == JitterPushResult::accepted);

    REQUIRE(buffer.pull(frame) == PlayoutKind::fec);
    decoded = decoder->decode(frame.payload_view(), pcm, true);
    REQUIRE(std::holds_alternative<std::size_t>(decoded));
    CHECK(std::get<std::size_t>(decoded) == kFrameSamples);
    CHECK(std::ranges::all_of(pcm, [](float sample) { return std::isfinite(sample); }));

    REQUIRE(buffer.pull(frame) == PlayoutKind::packet);
    decoded = decoder->decode(frame.payload_view(), pcm);
    REQUIRE(std::holds_alternative<std::size_t>(decoded));
    CHECK(std::get<std::size_t>(decoded) == kFrameSamples);

    REQUIRE(buffer.pull(frame) == PlayoutKind::plc);
    decoded = decoder->conceal(pcm);
    REQUIRE(std::holds_alternative<std::size_t>(decoded));
    CHECK(std::get<std::size_t>(decoded) == kFrameSamples);
    CHECK(std::ranges::all_of(pcm, [](float sample) { return std::isfinite(sample); }));
}

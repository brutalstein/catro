#include <catro/voice/codec.hpp>
#include <catro/voice/packet.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

using namespace catro::voice;

TEST_CASE("voice packet v1 round trips in network byte order") {
    const std::array payload{std::byte{0x11}, std::byte{0x22}, std::byte{0x33}};
    const VoicePacketView packet{.stream_id = 0x01020304U, .sequence = 0x1122U,
                                 .timestamp = 0xa1b2c3d4U, .payload = payload};
    std::array<std::byte, kVoiceHeaderBytes + payload.size()> bytes{};
    const auto written = serialize_packet(packet, bytes);
    REQUIRE(std::holds_alternative<std::size_t>(written));
    CHECK(std::get<std::size_t>(written) == bytes.size());

    CHECK(bytes[0] == std::byte{static_cast<unsigned char>('C')});
    CHECK(bytes[1] == std::byte{static_cast<unsigned char>('V')});
    CHECK(bytes[2] == std::byte{1});
    CHECK(bytes[4] == std::byte{0x01});
    CHECK(bytes[5] == std::byte{0x02});
    CHECK(bytes[8] == std::byte{0x11});
    CHECK(bytes[9] == std::byte{0x22});
    CHECK(bytes[12] == std::byte{0xa1});
    CHECK(bytes[15] == std::byte{0xd4});

    const auto parsed = parse_packet(bytes);
    REQUIRE(std::holds_alternative<VoicePacketView>(parsed));
    const auto view = std::get<VoicePacketView>(parsed);
    CHECK(view.stream_id == packet.stream_id);
    CHECK(view.sequence == packet.sequence);
    CHECK(view.timestamp == packet.timestamp);
    CHECK(std::ranges::equal(view.payload, payload));
}

TEST_CASE("voice packet header can be written in place without copying encoded payload") {
    std::array<std::byte, kVoiceHeaderBytes + 4> bytes{};
    bytes[kVoiceHeaderBytes + 0] = std::byte{0xaa};
    bytes[kVoiceHeaderBytes + 1] = std::byte{0xbb};
    bytes[kVoiceHeaderBytes + 2] = std::byte{0xcc};
    bytes[kVoiceHeaderBytes + 3] = std::byte{0xdd};

    const auto written = write_packet_header(0x01020304U, 0xfffeU, 0x10203040U, 4, bytes);
    REQUIRE(std::holds_alternative<std::size_t>(written));
    CHECK(std::get<std::size_t>(written) == bytes.size());

    const auto parsed = parse_packet(bytes);
    REQUIRE(std::holds_alternative<VoicePacketView>(parsed));
    const auto packet = std::get<VoicePacketView>(parsed);
    CHECK(packet.stream_id == 0x01020304U);
    CHECK(packet.sequence == 0xfffeU);
    CHECK(packet.timestamp == 0x10203040U);
    REQUIRE(packet.payload.size() == 4);
    CHECK(packet.payload[0] == std::byte{0xaa});
    CHECK(packet.payload[3] == std::byte{0xdd});
}

TEST_CASE("voice packet parser rejects malformed datagrams") {
    std::array<std::byte, kVoiceHeaderBytes + 1> bytes{};
    const std::array payload{std::byte{1}};
    REQUIRE(std::holds_alternative<std::size_t>(
        serialize_packet(VoicePacketView{.stream_id = 1, .sequence = 2, .timestamp = 3, .payload = payload}, bytes)));

    auto broken = bytes;
    broken[0] = std::byte{static_cast<unsigned char>('X')};
    CHECK(std::get<PacketError>(parse_packet(broken)) == PacketError::bad_magic);

    broken = bytes;
    broken[2] = std::byte{2};
    CHECK(std::get<PacketError>(parse_packet(broken)) == PacketError::unsupported_version);

    broken = bytes;
    broken[3] = std::byte{1};
    CHECK(std::get<PacketError>(parse_packet(broken)) == PacketError::unsupported_flags);

    broken = bytes;
    broken[10] = std::byte{1};
    CHECK(std::get<PacketError>(parse_packet(broken)) == PacketError::nonzero_reserved);

    CHECK(std::get<PacketError>(parse_packet(std::span<const std::byte>(bytes).first(kVoiceHeaderBytes))) ==
          PacketError::empty_payload);
    CHECK(std::get<PacketError>(parse_packet(std::span<const std::byte>(bytes).first(5))) == PacketError::too_short);

    std::vector<std::byte> oversized(kVoiceHeaderBytes + kVoiceMaxPayloadBytes + 1, std::byte{0});
    oversized[0] = std::byte{'C'};
    oversized[1] = std::byte{'V'};
    oversized[2] = std::byte{1};
    CHECK(std::get<PacketError>(parse_packet(oversized)) == PacketError::payload_too_large);
}

TEST_CASE("opus voice codec encodes decodes and conceals fixed 20 ms frames") {
    auto encoder_result = Encoder::create();
    REQUIRE(std::holds_alternative<std::unique_ptr<Encoder>>(encoder_result));
    auto encoder = std::move(std::get<std::unique_ptr<Encoder>>(encoder_result));

    auto decoder_result = Decoder::create();
    REQUIRE(std::holds_alternative<std::unique_ptr<Decoder>>(decoder_result));
    auto decoder = std::move(std::get<std::unique_ptr<Decoder>>(decoder_result));

    std::array<float, kFrameSamples> input{};
    std::array<std::byte, kMaxOpusPacketBytes> encoded{};
    std::array<float, kFrameSamples> decoded{};

    double phase = 0.0;
    double decoded_energy = 0.0;
    for (int frame = 0; frame < 20; ++frame) {
        for (auto& sample : input) {
            sample = static_cast<float>(0.2 * std::sin(phase));
            phase += 2.0 * 3.14159265358979323846 * 440.0 / static_cast<double>(kSampleRate);
        }
        const auto encoded_result = encoder->encode(input, encoded);
        REQUIRE(std::holds_alternative<std::size_t>(encoded_result));
        const auto size = std::get<std::size_t>(encoded_result);
        REQUIRE(size > 0);
        REQUIRE(size <= encoded.size());

        const auto decoded_result = decoder->decode(std::span<const std::byte>(encoded).first(size), decoded);
        REQUIRE(std::holds_alternative<std::size_t>(decoded_result));
        CHECK(std::get<std::size_t>(decoded_result) == kFrameSamples);
        for (const auto sample : decoded) {
            REQUIRE(std::isfinite(sample));
            decoded_energy += static_cast<double>(sample) * sample;
        }
    }
    CHECK(decoded_energy > 0.01);

    const auto concealed = decoder->conceal(decoded);
    REQUIRE(std::holds_alternative<std::size_t>(concealed));
    CHECK(std::get<std::size_t>(concealed) == kFrameSamples);
    CHECK(std::ranges::all_of(decoded, [](float sample) { return std::isfinite(sample); }));
}

TEST_CASE("opus wrapper validates frame and output sizes") {
    auto encoder_result = Encoder::create();
    REQUIRE(std::holds_alternative<std::unique_ptr<Encoder>>(encoder_result));
    auto encoder = std::move(std::get<std::unique_ptr<Encoder>>(encoder_result));

    std::array<float, kFrameSamples - 1> short_frame{};
    std::array<std::byte, kMaxOpusPacketBytes> encoded{};
    const auto bad_frame = encoder->encode(short_frame, encoded);
    REQUIRE(std::holds_alternative<CodecError>(bad_frame));
    CHECK(std::get<CodecError>(bad_frame).code == CodecErrorCode::invalid_argument);

    std::array<float, kFrameSamples> frame{};
    const auto no_output = encoder->encode(frame, std::span<std::byte>{});
    REQUIRE(std::holds_alternative<CodecError>(no_output));
    CHECK(std::get<CodecError>(no_output).code == CodecErrorCode::output_too_small);
}

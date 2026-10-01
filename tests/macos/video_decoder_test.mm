#include <catro/platform/macos/video_decoder.hpp>
#include <catro/platform/macos/video_encoder.hpp>

#include "pixel_buffer_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <vector>

using namespace catro;
using namespace catro::platform::macos;

namespace {

H264DecoderConfig software_allowed() {
    H264DecoderConfig config;
    config.require_hardware = false;
    return config;
}

std::vector<EncodedAccessUnit> encode_frames(std::uint32_t width, std::uint32_t height, int count) {
    H264EncoderConfig config;
    config.width = width;
    config.height = height;
    config.bitrate = 1'000'000;
    config.require_hardware = false;
    MacH264Encoder encoder;
    REQUIRE_FALSE(encoder.start(config).has_value());
    const auto source = test::make_nv12_surface(width, height);
    std::vector<EncodedAccessUnit> units;
    for (int index = 0; index < count; ++index) {
        EncodedAccessUnit unit;
        REQUIRE_FALSE(encoder.encode(source, index * 333'333, index == 0, unit).has_value());
        if (!unit.bytes.empty()) {
            units.push_back(std::move(unit));
        }
    }
    return units;
}

} // namespace

TEST_CASE("macOS H.264 decoder rejects malformed and oversized input without a session") {
    MacH264Decoder decoder;
    DecodedFrame frame;
    const std::vector<std::byte> payload{std::byte{0x65}, std::byte{0x88}};
    CHECK(decoder.decode(payload, 0, frame)->code == H264DecoderErrorCode::invalid_config);

    REQUIRE_FALSE(decoder.start(software_allowed()).has_value());
    CHECK(decoder.decode(payload, 0, frame)->code == H264DecoderErrorCode::malformed_input);
    const std::vector<std::byte> empty_nal{std::byte{0}, std::byte{0}, std::byte{1}, std::byte{0}, std::byte{0},
                                           std::byte{1}, std::byte{0x65}};
    CHECK(decoder.decode(empty_nal, 0, frame)->code == H264DecoderErrorCode::malformed_input);

    H264DecoderConfig tiny = software_allowed();
    tiny.max_access_unit_bytes = 1024;
    REQUIRE_FALSE(decoder.start(tiny).has_value());
    const std::vector<std::byte> oversized(2048, std::byte{1});
    CHECK(decoder.decode(oversized, 0, frame)->code == H264DecoderErrorCode::input_too_large);
    CHECK(decoder.statistics().oversized_inputs == 1);
    CHECK_FALSE(frame.buffer);
}

TEST_CASE("macOS H.264 decoder waits for parameter sets before decoding delta frames") {
    const auto units = encode_frames(320, 180, 3);
    REQUIRE(units.size() >= 2);
    MacH264Decoder decoder;
    REQUIRE_FALSE(decoder.start(software_allowed()).has_value());
    DecodedFrame frame;
    REQUIRE_FALSE(decoder.decode(units[1].bytes, units[1].pts_100ns, frame).has_value());
    CHECK_FALSE(frame.buffer);
    CHECK(decoder.statistics().waiting_for_parameter_sets == 1);
}

TEST_CASE("macOS H.264 decoder round-trips encoder output into GPU surfaces") {
    const auto units = encode_frames(320, 180, 4);
    REQUIRE_FALSE(units.empty());
    MacH264Decoder decoder;
    REQUIRE_FALSE(decoder.start(software_allowed()).has_value());
    std::uint64_t decoded = 0;
    for (const auto& unit : units) {
        DecodedFrame frame;
        REQUIRE_FALSE(decoder.decode(unit.bytes, unit.pts_100ns, frame).has_value());
        if (frame.buffer) {
            ++decoded;
            CHECK(frame.buffer.width() == 320);
            CHECK(frame.buffer.height() == 180);
            CHECK(CVPixelBufferGetIOSurface(frame.buffer.get()) != nullptr);
        }
    }
    CHECK(decoded > 0);
    const auto stats = decoder.statistics();
    CHECK(stats.width == 320);
    CHECK(stats.height == 180);
    std::printf("diagnostic: macOS H.264 decoder hardware=%d\n", stats.hardware_accelerated ? 1 : 0);
}

TEST_CASE("macOS H.264 decoder rebuilds its session when the stream resolution changes") {
    const auto small = encode_frames(320, 180, 1);
    const auto large = encode_frames(640, 360, 1);
    REQUIRE(small.size() == 1);
    REQUIRE(large.size() == 1);
    MacH264Decoder decoder;
    REQUIRE_FALSE(decoder.start(software_allowed()).has_value());
    DecodedFrame frame;
    REQUIRE_FALSE(decoder.decode(small[0].bytes, 0, frame).has_value());
    REQUIRE_FALSE(decoder.decode(large[0].bytes, 1, frame).has_value());
    const auto stats = decoder.statistics();
    CHECK(stats.stream_changes == 1);
    CHECK(stats.width == 640);
    CHECK(stats.height == 360);
}

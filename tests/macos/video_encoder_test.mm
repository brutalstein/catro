#include <catro/platform/macos/video_encoder.hpp>

#include "pixel_buffer_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>

using namespace catro;
using namespace catro::platform::macos;

namespace {

H264EncoderConfig software_allowed(std::uint32_t width = 640, std::uint32_t height = 360) {
    H264EncoderConfig config;
    config.width = width;
    config.height = height;
    config.bitrate = 1'000'000;
    config.gop_frames = 30;
    config.require_hardware = false;
    return config;
}

bool starts_with_sps(const EncodedAccessUnit& unit) {
    return unit.bytes.size() > 5 && unit.bytes[0] == std::byte{0} && unit.bytes[1] == std::byte{0} &&
           unit.bytes[2] == std::byte{0} && unit.bytes[3] == std::byte{1} &&
           (std::to_integer<int>(unit.bytes[4]) & 0x1F) == 7;
}

} // namespace

TEST_CASE("macOS H.264 encoder rejects invalid configuration") {
    CHECK_FALSE(validate(H264EncoderConfig{}).has_value());
    auto config = software_allowed();
    config.width = 641;
    CHECK(validate(config)->code == H264EncoderErrorCode::invalid_config);
    config = software_allowed();
    config.frame_rate_denominator = 0;
    CHECK(validate(config)->code == H264EncoderErrorCode::invalid_config);
    config = software_allowed();
    config.bitrate = 0;
    CHECK(validate(config)->code == H264EncoderErrorCode::invalid_config);
    config = software_allowed();
    config.gop_frames = 0;
    CHECK(validate(config)->code == H264EncoderErrorCode::invalid_config);

    MacH264Encoder encoder;
    config = software_allowed();
    config.height = 7;
    CHECK(encoder.start(config)->code == H264EncoderErrorCode::invalid_config);
    CHECK_FALSE(encoder.running());

    EncodedAccessUnit unit;
    CHECK(encoder.encode(test::make_nv12_surface(640, 360), 0, false, unit)->code ==
          H264EncoderErrorCode::invalid_source);
}

TEST_CASE("macOS H.264 encoder emits Annex-B keyframes with parameter sets") {
    MacH264Encoder encoder;
    REQUIRE_FALSE(encoder.start(software_allowed()).has_value());
    const auto source = test::make_nv12_surface(640, 360);

    EncodedAccessUnit first;
    REQUIRE_FALSE(encoder.encode(source, 0, false, first).has_value());
    REQUIRE_FALSE(first.bytes.empty());
    CHECK(first.keyframe);
    CHECK(starts_with_sps(first));

    EncodedAccessUnit delta;
    REQUIRE_FALSE(encoder.encode(source, 333'333, false, delta).has_value());
    CHECK_FALSE(delta.keyframe);

    EncodedAccessUnit forced;
    REQUIRE_FALSE(encoder.encode(source, 666'666, true, forced).has_value());
    CHECK(forced.keyframe);
    CHECK(starts_with_sps(forced));

    EncodedAccessUnit missing;
    CHECK(encoder.encode(PixelBuffer{}, 0, false, missing)->code == H264EncoderErrorCode::invalid_source);

    const auto stats = encoder.statistics();
    CHECK(stats.frames_encoded == 3);
    CHECK(stats.keyframes == 2);
    std::printf("diagnostic: macOS H.264 encoder hardware=%d low_latency=%d\n", stats.hardware_accelerated ? 1 : 0,
                stats.low_latency_rate_control ? 1 : 0);
}

TEST_CASE("macOS H.264 encoder bounds access-unit size") {
    auto config = software_allowed();
    config.max_access_unit_bytes = 1024;
    MacH264Encoder encoder;
    REQUIRE_FALSE(encoder.start(config).has_value());
    EncodedAccessUnit unit;
    const auto error = encoder.encode(test::make_nv12_surface(640, 360, 200), 0, true, unit);
    if (error) {
        CHECK(error->code == H264EncoderErrorCode::output_too_large);
        CHECK(unit.bytes.empty());
        CHECK(encoder.statistics().oversized_outputs == 1);
    } else {
        CHECK(unit.bytes.size() <= config.max_access_unit_bytes);
    }
}

TEST_CASE("macOS H.264 production encoder never hides a software fallback") {
    MacH264Encoder encoder;
    H264EncoderConfig config;
    config.width = 640;
    config.height = 360;
    const auto error = encoder.start(config);
    if (error) {
        CHECK(error->code == H264EncoderErrorCode::hardware_unavailable);
        CHECK_FALSE(encoder.running());
    } else {
        CHECK(encoder.statistics().hardware_accelerated);
    }
}

#include <catro/platform/windows/video_encoder.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace catro::platform::windows;

TEST_CASE("Windows H264 encoder is inert until started") {
    WindowsH264HardwareEncoder encoder;
    CHECK_FALSE(encoder.running());

    const auto stats = encoder.statistics();
    CHECK(stats.frames_submitted == 0);
    CHECK(stats.frames_encoded == 0);
    CHECK(stats.encoded_bytes == 0);
    CHECK(stats.conversion_failures == 0);
    CHECK(stats.input_failures == 0);
    CHECK(stats.output_failures == 0);
    CHECK(stats.output_timeouts == 0);
    CHECK(stats.input_sample_allocations == 0);
    CHECK(stats.output_sample_allocations == 0);

    encoder.stop();
    encoder.stop();
    CHECK_FALSE(encoder.running());
}

TEST_CASE("Windows H264 encoder rejects an empty source before touching Media Foundation") {
    WindowsH264HardwareEncoder encoder;
    GpuCaptureFrame empty;
    EncodedAccessUnit output;
    const auto failure = encoder.encode(empty, output);
    REQUIRE(failure);
    CHECK(failure->code == HardwareEncoderErrorCode::input_failed);
}

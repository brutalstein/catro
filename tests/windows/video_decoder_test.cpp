#include <catro/platform/windows/video_decoder.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <span>

using namespace catro::platform::windows;

TEST_CASE("Windows H264 decoder is inert until started") {
    WindowsH264D3D11Decoder decoder;
    CHECK_FALSE(decoder.running());

    const auto stats = decoder.statistics();
    CHECK(stats.frames_submitted == 0);
    CHECK(stats.frames_decoded == 0);
    CHECK(stats.compressed_bytes == 0);
    CHECK(stats.input_failures == 0);
    CHECK(stats.output_failures == 0);
    CHECK(stats.gpu_output_failures == 0);
    CHECK(stats.oversized_inputs == 0);
    CHECK(stats.stream_changes == 0);
    CHECK(stats.input_sample_allocations == 0);

    decoder.stop();
    decoder.stop();
    CHECK_FALSE(decoder.running());
}

TEST_CASE("Windows H264 decoder rejects invalid configuration before device creation") {
    WindowsH264D3D11Decoder decoder;
    H264DecoderConfig config;
    config.max_access_unit_bytes = 1;

    const auto failure = decoder.start(config);
    REQUIRE(failure);
    CHECK(failure->code == H264DecoderErrorCode::invalid_config);
    CHECK_FALSE(decoder.running());
}

TEST_CASE("Windows H264 decoder rejects input before startup") {
    WindowsH264D3D11Decoder decoder;
    DecodedGpuFrame output;
    const std::byte byte{0};

    const auto failure = decoder.decode(
        std::span<const std::byte>(&byte, 1),
        0,
        output);
    REQUIRE(failure);
    CHECK(failure->code == H264DecoderErrorCode::input_failed);
    CHECK_FALSE(output.texture);
}

#include <encode_check.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string_view>
#include <vector>

using namespace catro::tools;

namespace {

std::optional<EncodeCheckOptions> parse(std::vector<std::string_view> arguments) {
    return parse_encode_check_arguments(arguments);
}

} // namespace

TEST_CASE("encode-check defaults to conservative same-adapter H264 validation") {
    const auto defaults = parse({});
    REQUIRE(defaults);
    CHECK(defaults->duration == std::chrono::seconds(10));
    CHECK(defaults->encoder.width == 1728);
    CHECK(defaults->encoder.height == 1080);
    CHECK(defaults->encoder.frame_rate_numerator == 30);
    CHECK(defaults->encoder.frame_rate_denominator == 1);
    CHECK(defaults->encoder.bitrate == 6'000'000);
    CHECK(defaults->encoder.gop_frames == 60);
}

TEST_CASE("encode-check accepts bounded explicit video settings") {
    const auto explicit_options = parse({
        "--seconds", "20",
        "--width", "1920",
        "--height", "1080",
        "--fps", "60",
        "--bitrate", "8000000",
    });
    REQUIRE(explicit_options);
    CHECK(explicit_options->duration == std::chrono::seconds(20));
    CHECK(explicit_options->encoder.width == 1920);
    CHECK(explicit_options->encoder.height == 1080);
    CHECK(explicit_options->encoder.frame_rate_numerator == 60);
    CHECK(explicit_options->encoder.gop_frames == 120);
    CHECK(explicit_options->encoder.bitrate == 8'000'000);
}

TEST_CASE("encode-check rejects malformed duplicate or unsafe ranges") {
    CHECK_FALSE(parse({"--seconds"}));
    CHECK_FALSE(parse({"--seconds", "0"}));
    CHECK_FALSE(parse({"--seconds", "61"}));
    CHECK_FALSE(parse({"--width", "1919"}));
    CHECK_FALSE(parse({"--height", "1079"}));
    CHECK_FALSE(parse({"--fps", "0"}));
    CHECK_FALSE(parse({"--fps", "121"}));
    CHECK_FALSE(parse({"--bitrate", "127999"}));
    CHECK_FALSE(parse({"--bitrate", "50000001"}));
    CHECK_FALSE(parse({"--fps", "30", "--fps", "60"}));
    CHECK_FALSE(parse({"--codec", "av1"}));
}

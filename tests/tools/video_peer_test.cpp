#include "video_peer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string_view>

using namespace catro::tools;

TEST_CASE("video peer parser accepts a bounded sender configuration") {
    const std::array<std::string_view, 24> arguments{
        "--mode", "send",
        "--bind", "127.0.0.1:54000",
        "--peer", "127.0.0.1:54001",
        "--seconds", "45",
        "--max-width", "1920",
        "--max-height", "1080",
        "--fps", "60",
        "--bitrate", "12000000",
        "--ssrc", "42",
        "--payload-type", "102",
        "--mtu", "1200",
        "--frame-buffer", "8388608",
    };

    const auto parsed = parse_video_peer_arguments(arguments);
    REQUIRE(parsed);
    CHECK(parsed->mode == VideoPeerMode::send);
    CHECK(parsed->bind == UdpEndpoint{"127.0.0.1", 54000});
    CHECK(parsed->peer == UdpEndpoint{"127.0.0.1", 54001});
    CHECK(parsed->duration.count() == 45);
    CHECK(parsed->max_width == 1920);
    CHECK(parsed->max_height == 1080);
    CHECK(parsed->fps == 60);
    CHECK(parsed->bitrate == 12'000'000);
    CHECK(parsed->ssrc == 42);
    CHECK(parsed->payload_type == 102);
    CHECK(parsed->mtu_bytes == 1200);
    CHECK(parsed->max_access_unit_bytes == 8U * 1024U * 1024U);
}

TEST_CASE("video peer parser keeps conservative media defaults") {
    const std::array<std::string_view, 6> arguments{
        "--mode", "receive",
        "--bind", "127.0.0.1:54100",
        "--peer", "127.0.0.1:54101",
    };
    const auto parsed = parse_video_peer_arguments(arguments);
    REQUIRE(parsed);
    CHECK(parsed->mode == VideoPeerMode::receive);
    CHECK(parsed->duration.count() == 30);
    CHECK(parsed->max_width == 2560);
    CHECK(parsed->max_height == 1080);
    CHECK(parsed->fps == 30);
    CHECK(parsed->bitrate == 6'000'000);
    CHECK(parsed->ssrc == 1);
    CHECK(parsed->payload_type == 96);
    CHECK(parsed->mtu_bytes == 1200);
    CHECK(parsed->max_access_unit_bytes == 4U * 1024U * 1024U);
}

TEST_CASE("video peer parser rejects unsafe or incomplete settings") {
    CHECK_FALSE(parse_video_peer_arguments(
        std::array<std::string_view, 4>{
            "--bind", "127.0.0.1:54200",
            "--peer", "127.0.0.1:54201"}));

    CHECK_FALSE(parse_video_peer_arguments(
        std::array<std::string_view, 8>{
            "--mode", "send",
            "--bind", "127.0.0.1:54200",
            "--peer", "127.0.0.1:54201",
            "--mtu", "1500"}));

    CHECK_FALSE(parse_video_peer_arguments(
        std::array<std::string_view, 8>{
            "--mode", "send",
            "--bind", "127.0.0.1:54200",
            "--peer", "127.0.0.1:54201",
            "--fps", "0"}));

    CHECK_FALSE(parse_video_peer_arguments(
        std::array<std::string_view, 10>{
            "--mode", "receive",
            "--bind", "127.0.0.1:54200",
            "--bind", "127.0.0.1:54202",
            "--peer", "127.0.0.1:54201",
            "--seconds", "10"}));
}

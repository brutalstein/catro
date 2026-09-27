#include "voice_peer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string_view>

using namespace catro::tools;

TEST_CASE("voice peer parser accepts explicit localhost duplex settings") {
    const std::array<std::string_view, 18> arguments{
        "--bind", "127.0.0.1:50000",
        "--peer", "127.0.0.1:50001",
        "--mode", "duplex",
        "--seconds", "45",
        "--stream-id", "42",
        "--jitter", "4",
        "--bitrate", "64000",
        "--input", "mmdevice:mic",
        "--output", "mmdevice:phones",
    };
    const auto parsed = parse_voice_peer_arguments(arguments);
    REQUIRE(parsed);
    CHECK(parsed->bind == UdpEndpoint{"127.0.0.1", 50000});
    CHECK(parsed->peer == UdpEndpoint{"127.0.0.1", 50001});
    CHECK(parsed->mode == VoicePeerMode::duplex);
    CHECK(parsed->duration.count() == 45);
    CHECK(parsed->stream_id == 42);
    CHECK(parsed->jitter_packets == 4);
    CHECK(parsed->bitrate == 64000);
    REQUIRE(parsed->input);
    REQUIRE(parsed->output);
    CHECK(parsed->input->value == "mmdevice:mic");
    CHECK(parsed->output->value == "mmdevice:phones");
}

TEST_CASE("voice peer parser keeps conservative defaults") {
    const std::array<std::string_view, 4> arguments{
        "--bind", "127.0.0.1:51000",
        "--peer", "127.0.0.1:51001",
    };
    const auto parsed = parse_voice_peer_arguments(arguments);
    REQUIRE(parsed);
    CHECK(parsed->mode == VoicePeerMode::duplex);
    CHECK(parsed->duration.count() == 30);
    CHECK(parsed->stream_id == 1);
    CHECK(parsed->jitter_packets == 3);
    CHECK(parsed->bitrate == 48000);
    CHECK_FALSE(parsed->input);
    CHECK_FALSE(parsed->output);
}

TEST_CASE("voice peer parser rejects missing repeated and unsafe ranges") {
    CHECK_FALSE(parse_voice_peer_arguments(std::array<std::string_view, 2>{"--bind", "127.0.0.1:50000"}));
    CHECK_FALSE(parse_voice_peer_arguments(std::array<std::string_view, 4>{
        "--bind", "127.0.0.1:0", "--peer", "127.0.0.1:50001"}));
    CHECK_FALSE(parse_voice_peer_arguments(std::array<std::string_view, 4>{
        "--bind", "127.0.0.1:70000", "--peer", "127.0.0.1:50001"}));
    CHECK_FALSE(parse_voice_peer_arguments(std::array<std::string_view, 6>{
        "--bind", "127.0.0.1:50000", "--peer", "127.0.0.1:50001", "--jitter", "0"}));
    CHECK_FALSE(parse_voice_peer_arguments(std::array<std::string_view, 6>{
        "--bind", "127.0.0.1:50000", "--peer", "127.0.0.1:50001", "--bitrate", "1000"}));
    CHECK_FALSE(parse_voice_peer_arguments(std::array<std::string_view, 8>{
        "--bind", "127.0.0.1:50000", "--bind", "127.0.0.1:50002",
        "--peer", "127.0.0.1:50001", "--mode", "send"}));
}

TEST_CASE("voice peer parser supports one-way engineering modes") {
    const std::array<std::string_view, 6> send{
        "--bind", "127.0.0.1:52000", "--peer", "127.0.0.1:52001", "--mode", "send"};
    const std::array<std::string_view, 6> receive{
        "--bind", "127.0.0.1:52001", "--peer", "127.0.0.1:52000", "--mode", "receive"};
    REQUIRE(parse_voice_peer_arguments(send));
    REQUIRE(parse_voice_peer_arguments(receive));
    CHECK(parse_voice_peer_arguments(send)->mode == VoicePeerMode::send);
    CHECK(parse_voice_peer_arguments(receive)->mode == VoicePeerMode::receive);
}

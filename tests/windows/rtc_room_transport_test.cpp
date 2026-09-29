#include <catro/rtc/room_media_policy.hpp>
#include <catro/rtc/room_mesh_transport.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace catro::rtc;

TEST_CASE("RTC room transport rejects insecure production configuration") {
    RoomMeshTransport transport;

    RoomMeshConfig config;
    config.signaling_url = "ws://127.0.0.1:8443/v1/rtc";
    config.access_token = "token";
    config.server_id = "server-1";
    config.channel_id = "voice-1";
    config.user_id = "user-1";
    config.ice_server_urls = {"stun:stun.example.test:3478"};

    const auto failure = transport.start(config, {});
    REQUIRE(failure);
    CHECK(failure->code == RoomTransportErrorCode::invalid_config);
    CHECK(transport.state() == RoomTransportState::idle);
    CHECK(transport.peer_count() == 0);
}

TEST_CASE("RTC room transport requires TURN unless explicitly in engineering mode") {
    RoomMeshTransport transport;

    RoomMeshConfig config;
    config.signaling_url = "wss://rtc.example.test/v1/rtc";
    config.access_token = "token";
    config.server_id = "server-1";
    config.channel_id = "voice-1";
    config.user_id = "user-1";
    config.ice_server_urls = {"stun:stun.example.test:3478"};

    const auto failure = transport.start(config, {});
    REQUIRE(failure);
    CHECK(failure->code == RoomTransportErrorCode::invalid_config);

    config.allow_no_turn = true;
    // The relaxed configuration is syntactically valid; do not actually connect in a unit test.
    // Validation coverage above is the invariant this test owns.
}

TEST_CASE("RTC room transport bounds remote peers to four") {
    RoomMeshTransport transport;

    RoomMeshConfig config;
    config.signaling_url = "wss://rtc.example.test/v1/rtc";
    config.access_token = "token";
    config.server_id = "server-1";
    config.channel_id = "voice-1";
    config.user_id = "user-1";
    config.ice_server_urls = {
        "turn:turn.example.test:3478?transport=udp"};

    config.max_peers = 0;
    auto failure = transport.start(config, {});
    REQUIRE(failure);
    CHECK(failure->code == RoomTransportErrorCode::invalid_config);

    config.max_peers = 5;
    failure = transport.start(config, {});
    REQUIRE(failure);
    CHECK(failure->code == RoomTransportErrorCode::invalid_config);
}

TEST_CASE("RTC room screen media accepts only the current owner") {
    CHECK(screen_media_allowed("peer-a", "peer-a"));
    CHECK_FALSE(screen_media_allowed("peer-a", "peer-b"));
    CHECK_FALSE(screen_media_allowed("", "peer-a"));
}

TEST_CASE("RTC room screen claim requires a joined signaling socket") {
    RoomMeshTransport transport;

    CHECK_FALSE(transport.claim_screen());
    CHECK(transport.screen_owner().empty());
    transport.release_screen();
}

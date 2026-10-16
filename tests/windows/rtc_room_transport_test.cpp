#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>

#include <catro/rtc/room_media_policy.hpp>
#include <catro/rtc/room_mesh_transport.hpp>

#include <catch2/catch_test_macros.hpp>
#include <rtc/rtc.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

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
    CHECK(failure->message == "invalid RTC room configuration: signaling URL is not wss://");
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
    CHECK(failure->message == "invalid RTC room configuration: no TURN server");

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
    CHECK(failure->message == "invalid RTC room configuration: peer limit 5 is outside 1-4");
}

TEST_CASE("RTC room screen media accepts only the current owner") {
    CHECK(screen_media_allowed("peer-a", "peer-a"));
    CHECK_FALSE(screen_media_allowed("peer-a", "peer-b"));
    CHECK_FALSE(screen_media_allowed("", "peer-a"));
}

TEST_CASE("RTC room transport rejoins after the signaling connection drops") {
    using namespace std::chrono_literals;
    std::mutex mutex;
    std::vector<std::shared_ptr<::rtc::WebSocket>> clients;
    std::atomic<int> joins{0};

    ::rtc::WebSocketServer::Configuration server_config;
    server_config.port = 0;
    server_config.bindAddress = "127.0.0.1";
    ::rtc::WebSocketServer server(server_config);
    server.onClient([&](std::shared_ptr<::rtc::WebSocket> client) {
        std::weak_ptr<::rtc::WebSocket> weak = client;
        client->onMessage([&joins, weak](::rtc::message_variant message) {
            const auto* text = std::get_if<std::string>(&message);
            const auto socket = weak.lock();
            if (text != nullptr && socket && text->find(R"("type":"join")") != std::string::npos) {
                joins.fetch_add(1);
                socket->send(std::string{R"({"type":"joined","peers":[],"protocol":1})"});
            }
        });
        std::scoped_lock lock(mutex);
        clients.push_back(std::move(client));
    });

    std::atomic<bool> failed{false};
    RoomTransportCallbacks callbacks;
    callbacks.on_state = [&failed](RoomTransportState state) {
        if (state == RoomTransportState::failed) {
            failed.store(true);
        }
    };

    RoomMeshTransport transport;
    RoomMeshConfig config;
    config.signaling_url = "ws://127.0.0.1:" + std::to_string(server.port()) + "/v1/rtc";
    config.access_token = "token";
    config.server_id = "server-1";
    config.channel_id = "voice-1";
    config.user_id = "user-1";
    config.ice_server_urls = {"stun:127.0.0.1:3478"};
    config.allow_insecure_signaling = true;
    config.allow_no_turn = true;
    REQUIRE_FALSE(transport.start(config, std::move(callbacks)));

    const auto eventually = [](auto predicate) {
        for (int i = 0; i < 300 && !predicate(); ++i) {
            std::this_thread::sleep_for(20ms);
        }
        return predicate();
    };
    REQUIRE(eventually([&] { return transport.state() == RoomTransportState::joined; }));

    {
        std::scoped_lock lock(mutex);
        REQUIRE(clients.size() == 1);
        clients.front()->close();
    }
    REQUIRE(eventually([&] { return joins.load() == 2 && transport.state() == RoomTransportState::joined; }));
    CHECK_FALSE(failed.load());

    transport.stop();
    CHECK(transport.state() == RoomTransportState::idle);
}

TEST_CASE("RTC room screen claim requires a joined signaling socket") {
    RoomMeshTransport transport;

    CHECK_FALSE(transport.claim_screen());
    CHECK(transport.screen_owner().empty());
    transport.release_screen();
}

TEST_CASE("RTC room transport names the TLS reason when the signaling handshake fails") {
    using namespace std::chrono_literals;
    WSADATA winsock{};
    REQUIRE(WSAStartup(MAKEWORD(2, 2), &winsock) == 0);
    const SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    REQUIRE(listener != INVALID_SOCKET);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int length = static_cast<int>(sizeof(address));
    REQUIRE(bind(listener, reinterpret_cast<sockaddr*>(&address), length) == 0);
    REQUIRE(listen(listener, 1) == 0);
    REQUIRE(getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) == 0);

    // Answers the TLS ClientHello with plain HTTP, so the client's handshake fails.
    std::thread server([listener] {
        const SOCKET client = accept(listener, nullptr, nullptr);
        if (client != INVALID_SOCKET) {
            constexpr char kReply[] = "HTTP/1.1 400 Bad Request\r\n\r\n";
            send(client, kReply, static_cast<int>(sizeof(kReply) - 1), 0);
            std::this_thread::sleep_for(1s);
            closesocket(client);
        }
    });

    std::mutex mutex;
    std::string error;
    RoomTransportCallbacks callbacks;
    callbacks.on_error = [&](std::string_view message) {
        std::scoped_lock lock(mutex);
        error = message;
    };
    RoomMeshTransport transport;
    RoomMeshConfig config;
    config.signaling_url = "wss://127.0.0.1:" + std::to_string(ntohs(address.sin_port)) + "/v1/rtc";
    config.access_token = "token";
    config.server_id = "server-1";
    config.channel_id = "voice-1";
    config.user_id = "user-1";
    config.ice_server_urls = {"stun:127.0.0.1:3478"};
    config.allow_no_turn = true;
    REQUIRE_FALSE(transport.start(config, std::move(callbacks)));

    const auto reported = [&] {
        std::scoped_lock lock(mutex);
        return error;
    };
    for (int i = 0; i < 300 && reported().empty(); ++i) {
        std::this_thread::sleep_for(20ms);
    }
    transport.stop();
    server.join();
    closesocket(listener);
    WSACleanup();

    const auto message = reported();
    INFO(message);
    CHECK(message.starts_with("TLS connection failed: Handshake failed: "));
}

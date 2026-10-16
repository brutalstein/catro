#include "macos_trust.hpp"

#include <catro/rtc/room_mesh_transport.hpp>

#include <catch2/catch_test_macros.hpp>
#include <rtc/rtc.hpp>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace {

// The production signaling endpoint the release build ships with (deploy/oracle-free).
constexpr const char* kProductionSignaling =
    "wss://51-170-186-61.sslip.io/v1/rtc";

// Errors before TLS starts mean the runner cannot reach production; they skip instead of failing.
[[nodiscard]] bool unreachable(const std::string& error) {
    return error == "TCP connection failed" ||
           error == "Connection timed out" ||
           error == "WebSocket connection failed";
}

class FirstEvent {
public:
    void set(std::string_view value) {
        {
            std::scoped_lock lock(mutex_);
            if (!value_.empty()) {
                return;
            }
            value_ = value;
        }
        changed_.notify_all();
    }

    [[nodiscard]] std::string wait() {
        std::unique_lock lock(mutex_);
        changed_.wait_for(lock, std::chrono::seconds{30}, [this] {
            return !value_.empty();
        });
        return value_;
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::string value_;
};

} // namespace

TEST_CASE("macOS RTC trust PEM holds the system anchors and the Let's Encrypt roots") {
    const std::string& pem = catro::rtc::macos_trust_pem();
    constexpr std::string_view kBegin = "-----BEGIN CERTIFICATE-----";
    std::size_t blocks = 0;
    for (auto at = pem.find(kBegin); at != std::string::npos;
         at = pem.find(kBegin, at + kBegin.size())) {
        ++blocks;
    }
    CHECK(blocks > 4); // the four embedded roots plus the exported system anchors
    CHECK(pem.ends_with(catro::rtc::letsencrypt_root_pem()));
}

TEST_CASE("embedded Let's Encrypt roots alone verify the production signaling server") {
    ::rtc::WebSocket::Configuration config;
    config.connectionTimeout = std::chrono::seconds{10};
    config.caCertificatePemFile = catro::rtc::letsencrypt_root_pem();
    ::rtc::WebSocket socket(config);
    FirstEvent outcome;
    socket.onOpen([&] { outcome.set("open"); });
    socket.onError([&](std::string error) { outcome.set(error); });
    socket.open(kProductionSignaling);

    const auto result = outcome.wait();
    socket.resetCallbacks();
    socket.close();
    INFO("WebSocket outcome: " << result);
    if (unreachable(result)) {
        SKIP("production signaling unreachable: " << result);
    }
    CHECK(result == "open");
}

TEST_CASE("macOS room transport passes TLS verification against production signaling") {
    catro::rtc::RoomMeshConfig config;
    config.signaling_url = kProductionSignaling;
    config.access_token = "ci-tls-check-invalid-token";
    config.server_id = "ci-tls-check";
    config.channel_id = "ci-tls-check";
    config.user_id = "ci-tls-check";
    config.ice_server_urls = {"stun:51-170-186-61.sslip.io:3478"};
    config.allow_no_turn = true;

    FirstEvent outcome;
    catro::rtc::RoomTransportCallbacks callbacks;
    callbacks.on_error = [&](std::string_view error) { outcome.set(error); };
    catro::rtc::RoomMeshTransport transport;
    const auto failure = transport.start(config, std::move(callbacks));
    INFO((failure ? failure->message : std::string{}));
    REQUIRE_FALSE(failure);

    // The server answers a verified TLS + WebSocket join with "unauthorized" for this fake token.
    const auto result = outcome.wait();
    transport.stop();
    INFO("room error: " << result);
    if (unreachable(result)) {
        SKIP("production signaling unreachable: " << result);
    }
    CHECK(result == "unauthorized");
}

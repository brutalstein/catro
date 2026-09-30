#include <catro/room_runtime.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <future>

namespace {

class Runtime final {
public:
    Runtime() : handle_(catro_room_runtime_create()) {
        REQUIRE(handle_ != nullptr);
    }

    ~Runtime() {
        catro_room_runtime_destroy(handle_);
    }

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    [[nodiscard]] CatroRoomRuntimeHandle get() const noexcept {
        return handle_;
    }

private:
    CatroRoomRuntimeHandle handle_ = nullptr;
};

CatroRoomRuntimeConfig valid_config() {
    static constexpr const char* ice_servers[] = {
        "stun:127.0.0.1:9",
    };

    CatroRoomRuntimeConfig config{};
    config.signaling_url = "ws://127.0.0.1:9/v1/rtc";
    config.access_token = "test-token";
    config.server_id = "server-1";
    config.channel_id = "voice-1";
    config.user_id = "user-1";
    config.ice_server_urls = ice_servers;
    config.ice_server_count = 1;
    config.max_remote_peers = 4;
    config.allow_insecure_signaling = 1;
    config.allow_no_turn = 1;
    return config;
}

} // namespace

TEST_CASE("room runtime C ABI rejects invalid arguments") {
    std::array<std::byte, 8> buffer{};

    CHECK(catro_room_runtime_start(nullptr, nullptr) == -1);
    CHECK(catro_room_runtime_claim_screen(nullptr) == -1);
    CHECK(catro_room_runtime_send_voice(nullptr, buffer.data(), buffer.size()) == 0);
    CHECK(catro_room_runtime_send_video(nullptr, buffer.data(), buffer.size()) == 0);
    CHECK(catro_room_runtime_send_stream_audio(nullptr, buffer.data(), buffer.size()) == 0);
    CHECK(catro_room_runtime_receive_voice(nullptr, buffer.data(), buffer.size(), 0) == -1);
    CHECK(catro_room_runtime_receive_video(nullptr, buffer.data(), buffer.size(), 0) == -1);
    CHECK(catro_room_runtime_receive_stream_audio(nullptr, buffer.data(), buffer.size(), 0) == -1);

    const auto snapshot = catro_room_runtime_snapshot(nullptr);
    CHECK(snapshot.state == CATRO_ROOM_FAILED);
    CHECK(std::strcmp(snapshot.error, "room runtime unavailable") == 0);

    catro_room_runtime_stop(nullptr);
    catro_room_runtime_release_screen(nullptr);
    catro_room_runtime_destroy(nullptr);
}

TEST_CASE("room runtime validates required configuration and peer bounds") {
    Runtime runtime;
    auto config = valid_config();

    CHECK(catro_room_runtime_start(runtime.get(), nullptr) == -1);

    config.signaling_url = nullptr;
    CHECK(catro_room_runtime_start(runtime.get(), &config) == -1);

    config = valid_config();
    config.max_remote_peers = 0;
    CHECK(catro_room_runtime_start(runtime.get(), &config) == -1);

    config.max_remote_peers = 5;
    CHECK(catro_room_runtime_start(runtime.get(), &config) == -1);

    config = valid_config();
    static constexpr const char* invalid_ice_servers[] = {""};
    config.ice_server_urls = invalid_ice_servers;
    CHECK(catro_room_runtime_start(runtime.get(), &config) == -1);
}

TEST_CASE("room runtime keeps insecure signaling and no-TURN opt-ins strict") {
    Runtime runtime;
    auto config = valid_config();

    config.allow_insecure_signaling = 0;
    CHECK(catro_room_runtime_start(runtime.get(), &config) == -1);

    config = valid_config();
    config.signaling_url = "wss://rtc.example.test/v1/rtc";
    config.allow_insecure_signaling = 0;
    config.allow_no_turn = 0;
    CHECK(catro_room_runtime_start(runtime.get(), &config) == -1);
}

TEST_CASE("room runtime stop wakes blocked receivers and is idempotent") {
    using namespace std::chrono_literals;

    Runtime runtime;
    auto config = valid_config();
    REQUIRE(catro_room_runtime_start(runtime.get(), &config) == 0);

    auto receive = std::async(std::launch::async, [&runtime] {
        std::array<std::byte, 8> buffer{};
        return catro_room_runtime_receive_voice(
            runtime.get(),
            buffer.data(),
            buffer.size(),
            5'000);
    });

    REQUIRE(receive.wait_for(100ms) == std::future_status::timeout);
    catro_room_runtime_stop(runtime.get());
    CHECK(receive.wait_for(1s) == std::future_status::ready);
    CHECK(receive.get() == 0);

    catro_room_runtime_stop(runtime.get());
    catro_room_runtime_stop(runtime.get());

    const auto snapshot = catro_room_runtime_snapshot(runtime.get());
    CHECK(snapshot.state == CATRO_ROOM_IDLE);
    CHECK(snapshot.peer_count == 0);
    CHECK(snapshot.screen_owner[0] == '\0');
    CHECK(catro_room_runtime_claim_screen(runtime.get()) == -1);
}

TEST_CASE("room runtime can restart after stop") {
    Runtime runtime;
    auto config = valid_config();

    REQUIRE(catro_room_runtime_start(runtime.get(), &config) == 0);
    catro_room_runtime_stop(runtime.get());
    REQUIRE(catro_room_runtime_start(runtime.get(), &config) == 0);
    catro_room_runtime_stop(runtime.get());

    CHECK(catro_room_runtime_snapshot(runtime.get()).state == CATRO_ROOM_IDLE);
}

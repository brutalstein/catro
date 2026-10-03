#include <catro/voice_runtime.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("macOS voice runtime C ABI has deterministic idle control semantics") {
    const auto handle = catro_voice_runtime_create();
    REQUIRE(handle != nullptr);

    auto snapshot = catro_voice_runtime_snapshot(handle);
    CHECK(snapshot.state == CATRO_VOICE_IDLE);
    CHECK(snapshot.exit_code == -1);

    catro_voice_runtime_set_muted(handle, 1);
    catro_voice_runtime_set_deafened(handle, 1);
    snapshot = catro_voice_runtime_snapshot(handle);
    CHECK(snapshot.muted == 1);
    CHECK(snapshot.deafened == 1);
    CHECK(snapshot.speaking == 0);

    // Volume and speaking state are safe before any media starts, and null ids are ignored.
    catro_voice_runtime_set_user_volume(handle, "ab01ff10", 0.5F);
    catro_voice_runtime_set_user_volume(handle, nullptr, 0.5F);
    CHECK(catro_voice_runtime_user_speaking(handle, "ab01ff10") == 0);
    CHECK(catro_voice_runtime_user_speaking(handle, nullptr) == 0);
    CHECK(catro_voice_runtime_user_speaking(nullptr, "ab01ff10") == 0);
    CHECK(snapshot.input_level == -100.0F);
    catro_voice_runtime_set_processing(handle, 0, 1, 0);
    catro_voice_runtime_set_input_threshold(handle, -40.0F);
    catro_voice_runtime_set_transmit(handle, 0);
    catro_voice_runtime_set_transmit(nullptr, 1);

    catro_voice_runtime_stop(handle);
    CHECK(catro_voice_runtime_snapshot(handle).state == CATRO_VOICE_IDLE);
    catro_voice_runtime_destroy(handle);
}

TEST_CASE("macOS voice runtime accepts only the production room path") {
    const auto handle = catro_voice_runtime_create();
    REQUIRE(handle != nullptr);

    CatroVoiceRuntimeConfig config{};
    config.bind_address = "127.0.0.1";
    config.bind_port = 50000;
    config.peer_address = "127.0.0.1";
    config.peer_port = 50001;
    config.stream_id = 1001;
    config.jitter_packets = 3;
    config.bitrate = 48'000;

    CHECK(catro_voice_runtime_start(handle, &config) == 2);
    const auto snapshot = catro_voice_runtime_snapshot(handle);
    CHECK(snapshot.state == CATRO_VOICE_FAILED);
    CHECK(snapshot.error[0] != '\0');

    catro_voice_runtime_destroy(handle);
}

#include <catro/screen_runtime.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace catro::screen;

TEST_CASE("Windows screen media runtime is inert until explicitly started") {
    WindowsScreenShareRuntime runtime;
    const auto snapshot = runtime.snapshot();

    CHECK(snapshot.state == ScreenShareState::idle);
    CHECK(snapshot.frames_encoded == 0);
    CHECK(snapshot.frames_sent == 0);
    CHECK(snapshot.packets_sent == 0);
    CHECK_FALSE(snapshot.remote_available);
    CHECK_FALSE(snapshot.remote_viewing);
    CHECK_FALSE(snapshot.remote_active);
    CHECK(snapshot.remote_packets == 0);
    CHECK(snapshot.remote_decoded == 0);
    CHECK_FALSE(runtime.preview_swap_chain());
    CHECK_FALSE(runtime.remote_swap_chain());

    runtime.set_local_preview_enabled(false);
    CHECK_FALSE(runtime.preview_swap_chain());
    runtime.set_local_preview_enabled(true);

    runtime.set_remote_viewing_enabled(true);
    CHECK(runtime.snapshot().remote_viewing);
    runtime.set_remote_viewing_enabled(false);
    CHECK_FALSE(runtime.snapshot().remote_viewing);
    CHECK_FALSE(runtime.remote_swap_chain());

    runtime.stop_sharing();
    runtime.stop();
    runtime.stop();
    CHECK(runtime.snapshot().state == ScreenShareState::idle);
}

TEST_CASE("Windows screen media runtime rejects an empty listener configuration") {
    WindowsScreenShareRuntime runtime;
    const ScreenTransportConfig config;

    const auto failure = runtime.start_listening(config);
    REQUIRE(failure);
    CHECK(failure->code == ScreenShareErrorCode::invalid_config);
    CHECK(runtime.snapshot().state == ScreenShareState::idle);
}

TEST_CASE("Windows screen media runtime rejects an empty share configuration") {
    WindowsScreenShareRuntime runtime;
    const ScreenShareConfig config;

    const auto failure = runtime.start(config);
    REQUIRE(failure);
    CHECK(failure->code == ScreenShareErrorCode::invalid_config);
    CHECK(runtime.snapshot().state == ScreenShareState::idle);
}

TEST_CASE("screen transport equality includes bounded media parameters") {
    const ScreenTransportConfig left{
        .bind = {"127.0.0.1", 55000},
        .peer = {"127.0.0.1", 55001},
        .payload_type = 96,
        .mtu_bytes = 1200,
        .max_access_unit_bytes = 4U * 1024U * 1024U,
    };
    auto right = left;
    CHECK(left == right);

    right.mtu_bytes = 1000;
    CHECK_FALSE(left == right);
}

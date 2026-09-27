#include <catro/screen_runtime.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace catro::screen;

TEST_CASE("Windows screen-share runtime is inert until explicitly started") {
    WindowsScreenShareRuntime runtime;
    const auto snapshot = runtime.snapshot();

    CHECK(snapshot.state == ScreenShareState::idle);
    CHECK(snapshot.frames_encoded == 0);
    CHECK(snapshot.frames_sent == 0);
    CHECK(snapshot.packets_sent == 0);
    CHECK_FALSE(runtime.preview_swap_chain());

    runtime.stop();
    runtime.stop();
    CHECK(runtime.snapshot().state == ScreenShareState::idle);
}

TEST_CASE("Windows screen-share runtime rejects an empty configuration synchronously") {
    WindowsScreenShareRuntime runtime;
    const ScreenShareConfig config;

    const auto failure = runtime.start(config);
    REQUIRE(failure);
    CHECK(failure->code == ScreenShareErrorCode::invalid_config);
    CHECK(runtime.snapshot().state == ScreenShareState::idle);
}

#include "../helpers/fake_screen_room.hpp"

#include <catro/macos_screen_runtime.hpp>

#import <QuartzCore/QuartzCore.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <thread>

using catro::screen::MacScreenShareConfig;
using catro::screen::MacScreenShareRuntime;
using catro::screen::ScreenShareErrorCode;
using catro::screen::ScreenShareState;
using catro::screen::ScreenTransportConfig;
using catro::test::FakeRoom;
using catro::test::fake_api;

namespace {

ScreenTransportConfig transport_for(FakeRoom& room) {
    ScreenTransportConfig config;
    config.room_runtime = &room;
    return config;
}

MacScreenShareConfig share_for(FakeRoom& room) {
    MacScreenShareConfig config;
    config.room_runtime = &room;
    config.source.native_id = 7;
    config.source.title = "Display";
    return config;
}

} // namespace

TEST_CASE("macOS screen share validation rejects unselected sources and unsafe bounds", "[macos][screen]") {
    FakeRoom room;
    REQUIRE(catro::screen::valid_share(share_for(room)));

    auto config = share_for(room);
    config.source.native_id = 0;
    CHECK_FALSE(catro::screen::valid_share(config));

    config = share_for(room);
    config.room_runtime = nullptr;
    CHECK_FALSE(catro::screen::valid_share(config));

    config = share_for(room);
    config.fps = 0;
    CHECK_FALSE(catro::screen::valid_share(config));

    config = share_for(room);
    config.bitrate = 1'000;
    CHECK_FALSE(catro::screen::valid_share(config));

    config = share_for(room);
    config.ssrc = 0;
    CHECK_FALSE(catro::screen::valid_share(config));
}

TEST_CASE("macOS screen runtime rejects invalid shares without leaving idle", "[macos][screen]") {
    FakeRoom room;
    MacScreenShareRuntime runtime(fake_api());
    auto config = share_for(room);
    config.source.native_id = 0;

    const auto failure = runtime.start(config);
    REQUIRE(failure);
    CHECK(failure->code == ScreenShareErrorCode::invalid_config);
    CHECK(runtime.snapshot().state == ScreenShareState::idle);
}

TEST_CASE("macOS screen runtime listens, restarts, and stops idempotently", "[macos][screen]") {
    FakeRoom room;
    MacScreenShareRuntime runtime(fake_api());

    REQUIRE_FALSE(runtime.start_listening(transport_for(room)));
    CHECK(runtime.snapshot().state == ScreenShareState::listening);
    // Re-requesting the same membership keeps the running receiver.
    REQUIRE_FALSE(runtime.start_listening(transport_for(room)));
    CHECK(runtime.snapshot().state == ScreenShareState::listening);

    runtime.stop_sharing();
    CHECK(runtime.snapshot().state == ScreenShareState::listening);

    runtime.stop();
    runtime.stop();
    CHECK(runtime.snapshot().state == ScreenShareState::idle);

    REQUIRE_FALSE(runtime.start_listening(transport_for(room)));
    CHECK(runtime.snapshot().state == ScreenShareState::listening);
}

TEST_CASE("macOS screen runtime reports a disconnected room before listening", "[macos][screen]") {
    FakeRoom room;
    room.state = CATRO_ROOM_FAILED;
    MacScreenShareRuntime runtime(fake_api());

    const auto failure = runtime.start_listening(transport_for(room));
    REQUIRE(failure);
    CHECK(failure->code == ScreenShareErrorCode::network_failed);
    CHECK(failure->message == "fake room failed");
    CHECK(runtime.snapshot().state == ScreenShareState::failed);

    room.state = CATRO_ROOM_JOINED;
    REQUIRE_FALSE(runtime.start_listening(transport_for(room)));
    CHECK(runtime.snapshot().state == ScreenShareState::listening);
}

TEST_CASE("macOS screen runtime survives malformed video while watched and stops promptly", "[macos][screen]") {
    FakeRoom room;
    MacScreenShareRuntime runtime(fake_api());
    REQUIRE_FALSE(runtime.start_listening(transport_for(room)));
    runtime.set_remote_viewing_enabled(true);

    constexpr std::array<std::byte, 6> kGarbage{std::byte{0xff}, std::byte{0x00}, std::byte{0x13},
                                                std::byte{0x37}, std::byte{0x80}, std::byte{0x01}};
    for (int index = 0; index < 32; ++index) {
        room.video.push(kGarbage.data(), kGarbage.size());
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const auto snapshot = runtime.snapshot();
    CHECK(snapshot.state == ScreenShareState::listening);
    CHECK(snapshot.remote_decoded == 0);

    const auto started = std::chrono::steady_clock::now();
    runtime.stop();
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(2));
    CHECK(runtime.snapshot().state == ScreenShareState::idle);
}

TEST_CASE("macOS screen runtime attaches and detaches caller-owned surfaces on the main thread",
          "[macos][screen]") {
    REQUIRE([NSThread isMainThread]);
    FakeRoom room;
    MacScreenShareRuntime runtime(fake_api());
    runtime.set_output_device("coreaudio:dev.catro.saved-headset:output");
    runtime.set_output_device("");
    REQUIRE_FALSE(runtime.start_listening(transport_for(room)));

    CALayer* preview = [CALayer layer];
    CALayer* remote = [CALayer layer];
    CHECK_FALSE(runtime.attach_preview_surface((__bridge void*)preview));
    CHECK_FALSE(runtime.attach_remote_surface((__bridge void*)remote));
    CHECK(remote.sublayers.count == 1);

    // Replacing the host moves presentation without restarting the room.
    CALayer* replacement = [CALayer layer];
    CHECK_FALSE(runtime.attach_remote_surface((__bridge void*)replacement));
    CHECK(remote.sublayers.count == 0);
    CHECK(replacement.sublayers.count == 1);
    CHECK(runtime.snapshot().state == ScreenShareState::listening);

    CHECK_FALSE(runtime.attach_preview_surface(nullptr));
    CHECK_FALSE(runtime.attach_remote_surface(nullptr));
    CHECK(replacement.sublayers.count == 0);
    runtime.stop();
}

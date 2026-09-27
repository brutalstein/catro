#include <catro/platform/windows/screen_capture.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace catro::platform::windows;
using namespace std::chrono_literals;

TEST_CASE("Windows screen capture starts with no hidden GPU or capture work") {
    WindowsGraphicsCapture capture;
    const auto stats = capture.statistics();
    CHECK(stats.state == ScreenCaptureState::idle);
    CHECK_FALSE(stats.error);
    CHECK(stats.frames_received == 0);
    CHECK(stats.frames_published == 0);
    CHECK(stats.mailbox_overwrites == 0);
    CHECK(stats.contention_drops == 0);

    GpuCaptureFrame frame;
    CHECK_FALSE(capture.wait_for_latest(frame, 0ms));
}

TEST_CASE("stopping an idle Windows screen capture is idempotent") {
    WindowsGraphicsCapture capture;
    capture.stop();
    capture.stop();
    CHECK(capture.statistics().state == ScreenCaptureState::idle);
}

TEST_CASE("capture source descriptors are inert value objects") {
    const CaptureSource source{
        .kind = CaptureSourceKind::window,
        .native_handle = 42,
        .title = "Example",
        .width = 1280,
        .height = 720,
    };
    CHECK(source.kind == CaptureSourceKind::window);
    CHECK(source.native_handle == 42);
    CHECK(source.width == 1280);
    CHECK(source.height == 720);
}

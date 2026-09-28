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

TEST_CASE("CS2 and fullscreen window sources use the game-compatible capture backend") {
    CaptureSource windowed{
        .kind = CaptureSourceKind::window,
        .native_handle = 1,
        .monitor_handle = 2,
        .title = "Counter-Strike 2",
        .process_name = "cs2.exe",
        .width = 1920,
        .height = 1080,
        .fullscreen_like = false,
    };
    CHECK(recommended_capture_backend(windowed) ==
          ScreenCaptureBackend::desktop_duplication);

    windowed.process_name = "notepad.exe";
    CHECK(recommended_capture_backend(windowed) ==
          ScreenCaptureBackend::windows_graphics_capture);

    windowed.fullscreen_like = true;
    CHECK(recommended_capture_backend(windowed) ==
          ScreenCaptureBackend::desktop_duplication);

    CaptureSource display{
        .kind = CaptureSourceKind::display,
        .native_handle = 2,
        .monitor_handle = 2,
        .title = "Primary display",
        .width = 1920,
        .height = 1080,
        .primary = true,
    };
    CHECK(recommended_capture_backend(display) ==
          ScreenCaptureBackend::windows_graphics_capture);
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

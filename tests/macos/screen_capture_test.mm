#include <catro/platform/macos/screen_capture.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

using namespace catro::platform::macos;
using namespace std::chrono_literals;

namespace {

class FakeCaptureAdapter final : public ScreenCaptureNativeAdapter {
public:
    CaptureEnumerationResult enumerate_sources() noexcept override {
        ++enumeration_calls;
        return enumeration;
    }

    std::optional<ScreenCaptureError> start(
        const CaptureSource& source,
        const ScreenCaptureConfig& config,
        FrameHandler on_frame,
        StopHandler on_stop) noexcept override {
        ++start_calls;
        started_source = source;
        started_config = config;
        frame_handler = std::move(on_frame);
        stop_handler = std::move(on_stop);
        return start_error;
    }

    void stop() noexcept override {
        ++stop_calls;
        if (frame_during_stop && frame_handler) {
            frame_handler(*frame_during_stop);
        }
    }

    CaptureEnumerationResult enumeration;
    std::optional<ScreenCaptureError> start_error;
    std::optional<NativeVideoFrame> frame_during_stop;
    FrameHandler frame_handler;
    StopHandler stop_handler;
    CaptureSource started_source;
    ScreenCaptureConfig started_config;
    int enumeration_calls = 0;
    int start_calls = 0;
    int stop_calls = 0;
};

NativeVideoFrame frame(
    std::uint64_t sequence,
    std::uint32_t width,
    std::uint32_t height) {
    return NativeVideoFrame{
        .lease = std::make_shared<int>(static_cast<int>(sequence)),
        .pixel_buffer = reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(sequence + 1)),
        .sequence = sequence,
        .width = width,
        .height = height,
        .pts_100ns = static_cast<std::int64_t>(sequence * 100),
    };
}

CaptureSource display() {
    return CaptureSource{
        .kind = CaptureSourceKind::display,
        .native_id = 42,
        .title = "Built-in Display",
        .width = 2560,
        .height = 1600,
        .primary = true,
    };
}

} // namespace

TEST_CASE("macOS source enumeration is lazy and opens no capture stream") {
    auto adapter = std::make_unique<FakeCaptureAdapter>();
    auto* fake = adapter.get();
    fake->enumeration.sources = {display()};
    MacScreenCapture capture(std::move(adapter));

    const auto result = capture.enumerate_sources();

    CHECK(result.sources == std::vector<CaptureSource>{display()});
    CHECK_FALSE(result.error);
    CHECK(fake->enumeration_calls == 1);
    CHECK(fake->start_calls == 0);
    CHECK(capture.statistics().state == ScreenCaptureState::idle);
}

TEST_CASE("macOS capture reports permission denial before opening a stream") {
    auto adapter = std::make_unique<FakeCaptureAdapter>();
    adapter->enumeration.error = ScreenCaptureError{
        ScreenCaptureErrorCode::permission_denied, -1};
    MacScreenCapture capture(std::move(adapter));

    const auto result = capture.enumerate_sources();

    REQUIRE(result.error);
    CHECK(result.error->code == ScreenCaptureErrorCode::permission_denied);
    CHECK(result.sources.empty());
}

TEST_CASE("macOS capture publishes only the newest frame and records resize") {
    auto adapter = std::make_unique<FakeCaptureAdapter>();
    auto* fake = adapter.get();
    MacScreenCapture capture(std::move(adapter));
    REQUIRE_FALSE(capture.start_source(display()));

    fake->frame_handler(frame(1, 1920, 1080));
    fake->frame_handler(frame(2, 1280, 720));

    NativeVideoFrame latest;
    REQUIRE(capture.wait_for_latest(latest, 0ms));
    CHECK(latest.sequence == 2);
    CHECK(latest.width == 1280);
    CHECK(latest.height == 720);

    const auto stats = capture.statistics();
    CHECK(stats.frames_received == 2);
    CHECK(stats.frames_published == 2);
    CHECK(stats.mailbox_overwrites == 1);
    CHECK(stats.resize_events == 1);
    CHECK(stats.width == 1280);
    CHECK(stats.height == 720);
}

TEST_CASE("macOS capture recovers after permission revocation and restart") {
    auto adapter = std::make_unique<FakeCaptureAdapter>();
    auto* fake = adapter.get();
    MacScreenCapture capture(std::move(adapter));
    REQUIRE_FALSE(capture.start_source(display()));

    fake->stop_handler(ScreenCaptureError{
        ScreenCaptureErrorCode::permission_denied, -2});
    auto stats = capture.statistics();
    REQUIRE(stats.error);
    CHECK(stats.state == ScreenCaptureState::failed);
    CHECK(stats.error->code == ScreenCaptureErrorCode::permission_denied);

    capture.stop();
    REQUIRE_FALSE(capture.start_source(display()));
    stats = capture.statistics();
    CHECK(stats.state == ScreenCaptureState::running);
    CHECK_FALSE(stats.error);
}

TEST_CASE("macOS capture ignores callbacks delivered while stop is draining") {
    auto adapter = std::make_unique<FakeCaptureAdapter>();
    auto* fake = adapter.get();
    MacScreenCapture capture(std::move(adapter));
    REQUIRE_FALSE(capture.start_source(display()));
    fake->frame_during_stop = frame(9, 800, 600);

    capture.stop();

    NativeVideoFrame latest;
    CHECK_FALSE(capture.wait_for_latest(latest, 0ms));
    CHECK(capture.statistics().state == ScreenCaptureState::idle);
    CHECK(fake->stop_calls == 1);
}

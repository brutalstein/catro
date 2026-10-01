#include <catro/platform/macos/screen_capture.hpp>

#include "pixel_buffer_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>
#include <variant>

using namespace catro;
using namespace catro::platform::macos;
using namespace std::chrono_literals;

namespace {

CaptureFrame frame(std::uint64_t sequence) {
    CaptureFrame result;
    result.buffer = test::make_nv12_surface(64, 64);
    result.sequence = sequence;
    return result;
}

} // namespace

TEST_CASE("macOS screen capture rejects invalid configuration before touching ScreenCaptureKit") {
    CHECK_FALSE(validate(ScreenCaptureConfig{}).has_value());
    for (const auto& config : {ScreenCaptureConfig{8, 1080, 30, true}, ScreenCaptureConfig{1920, 0, 30, true},
                               ScreenCaptureConfig{1920, 1080, 0, true}, ScreenCaptureConfig{1920, 1080, 241, true},
                               ScreenCaptureConfig{9000, 1080, 30, true}}) {
        const auto error = validate(config);
        REQUIRE(error.has_value());
        CHECK(error->code == ScreenCaptureErrorCode::invalid_config);
    }

    MacScreenCapture capture;
    const auto error = capture.start(CaptureSource{}, ScreenCaptureConfig{1920, 1080, 0, true});
    REQUIRE(error.has_value());
    CHECK(error->code == ScreenCaptureErrorCode::invalid_config);
}

TEST_CASE("macOS source enumeration is passive and reports missing permission as a structured error") {
    const auto result = enumerate_capture_sources(2s);
    if (!screen_capture_access_granted()) {
        REQUIRE(std::holds_alternative<ScreenCaptureError>(result));
        CHECK(std::get<ScreenCaptureError>(result).code == ScreenCaptureErrorCode::permission_denied);

        MacScreenCapture capture;
        const auto error = capture.start(CaptureSource{});
        REQUIRE(error.has_value());
        CHECK(error->code == ScreenCaptureErrorCode::permission_denied);
        CHECK(capture.statistics().state == ScreenCaptureState::failed);
        return;
    }
    // Machines with Screen Recording access: every listed source must be startable geometry.
    if (const auto* sources = std::get_if<std::vector<CaptureSource>>(&result)) {
        for (const auto& source : *sources) {
            CHECK(source.width > 0);
            CHECK(source.height > 0);
        }
    }
}

TEST_CASE("macOS capture mailbox keeps only the newest frame") {
    LatestFrameMailbox mailbox;
    mailbox.publish(frame(1));
    mailbox.publish(frame(2));
    mailbox.publish(frame(3));

    CaptureFrame latest;
    REQUIRE(mailbox.wait_for_latest(latest, 10ms));
    CHECK(latest.sequence == 3);
    CHECK(latest.buffer);
    CHECK(mailbox.published() == 3);
    CHECK(mailbox.overwrites() == 2);

    const auto started = std::chrono::steady_clock::now();
    CHECK_FALSE(mailbox.wait_for_latest(latest, 20ms));
    CHECK(std::chrono::steady_clock::now() - started >= 15ms);
}

TEST_CASE("macOS capture mailbox close wakes the consumer and ignores later frames") {
    LatestFrameMailbox mailbox;
    std::atomic_bool woke{false};
    std::thread consumer([&] {
        CaptureFrame ignored;
        // Catch2 assertions stay on the test thread.
        woke.store(!mailbox.wait_for_latest(ignored, 5s));
    });
    std::this_thread::sleep_for(20ms);
    mailbox.close();
    consumer.join();
    CHECK(woke.load());

    mailbox.publish(frame(9));
    CaptureFrame ignored;
    CHECK_FALSE(mailbox.wait_for_latest(ignored, 5ms));

    mailbox.reopen();
    mailbox.publish(frame(10));
    REQUIRE(mailbox.wait_for_latest(ignored, 5ms));
    CHECK(ignored.sequence == 10);
}

TEST_CASE("macOS capture mailbox survives close while a producer is publishing") {
    LatestFrameMailbox mailbox;
    std::atomic_bool running{true};
    std::thread producer([&] {
        std::uint64_t sequence = 0;
        while (running.load()) {
            mailbox.publish(frame(++sequence));
        }
    });
    CaptureFrame latest;
    for (int index = 0; index < 50; ++index) {
        (void)mailbox.wait_for_latest(latest, 5ms);
    }
    mailbox.close();
    running.store(false);
    producer.join();
    CHECK(mailbox.published() > 0);
    CHECK_FALSE(mailbox.wait_for_latest(latest, 1ms));
}

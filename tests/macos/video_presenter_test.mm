#include <catro/platform/macos/video_presenter.hpp>

#include "pixel_buffer_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>

using namespace catro;
using namespace catro::platform::macos;

TEST_CASE("macOS presenter does no presentation work while hidden") {
    MacVideoPresenter presenter;
    CHECK(presenter.layer() == nullptr);
    REQUIRE_FALSE(presenter.present(test::make_nv12_surface(320, 180)).has_value());
    const auto stats = presenter.statistics();
    CHECK(stats.frames_dropped == 1);
    CHECK(stats.frames_presented == 0);
    CHECK_FALSE(stats.visible);
}

TEST_CASE("macOS presenter rejects missing frames") {
    MacVideoPresenter presenter;
    presenter.set_visible(true);
    const auto error = presenter.present(PixelBuffer{});
    REQUIRE(error.has_value());
    CHECK(error->code == VideoPresenterErrorCode::invalid_source);
}

TEST_CASE("macOS presenter shows surfaces, tracks resizes, and detaches its layer when hidden") {
    MacVideoPresenter presenter;
    presenter.set_visible(true);
    REQUIRE(presenter.layer() != nullptr);

    REQUIRE_FALSE(presenter.present(test::make_nv12_surface(320, 180)).has_value());
    REQUIRE_FALSE(presenter.present(test::make_nv12_surface(320, 180)).has_value());
    REQUIRE_FALSE(presenter.present(test::make_nv12_surface(640, 360)).has_value());
    auto stats = presenter.statistics();
    CHECK(stats.frames_presented == 3);
    CHECK(stats.reconfigurations == 1);
    CHECK(stats.source_width == 640);
    CHECK(stats.source_height == 360);

    presenter.set_visible(false);
    CHECK(presenter.layer() == nullptr);
    REQUIRE_FALSE(presenter.present(test::make_nv12_surface(640, 360)).has_value());
    stats = presenter.statistics();
    CHECK(stats.frames_presented == 3);
    CHECK(stats.frames_dropped == 1);
}

TEST_CASE("macOS presenter tolerates visibility changes while a worker presents") {
    MacVideoPresenter presenter;
    std::atomic_bool running{true};
    std::thread worker([&] {
        const auto frame = test::make_nv12_surface(320, 180);
        while (running.load()) {
            (void)presenter.present(frame);
        }
    });
    for (int index = 0; index < 50; ++index) {
        presenter.set_visible(index % 2 == 0);
        std::this_thread::yield();
    }
    running.store(false);
    worker.join();
    const auto stats = presenter.statistics();
    CHECK(stats.frames_presented + stats.frames_dropped > 0);
}

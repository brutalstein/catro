#include <catro/platform/macos/video_presenter.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>

using namespace catro::platform::macos;

namespace {

class FakePresenterAdapter final : public VideoPresenterNativeAdapter {
public:
    std::optional<VideoPresenterError> replace_surface(
        void* surface) noexcept override {
        ++replace_calls;
        current_surface = surface;
        return replace_error;
    }

    std::optional<VideoPresenterError> set_visible(
        bool visible) noexcept override {
        ++visibility_calls;
        is_visible = visible;
        return visibility_error;
    }

    std::optional<VideoPresenterError> present(
        const NativeVideoFrame& frame) noexcept override {
        ++present_calls;
        presented = frame;
        return present_error;
    }

    void reset() noexcept override {
        ++reset_calls;
        current_surface = nullptr;
        is_visible = false;
    }

    void* current_surface = nullptr;
    NativeVideoFrame presented;
    std::optional<VideoPresenterError> replace_error;
    std::optional<VideoPresenterError> visibility_error;
    std::optional<VideoPresenterError> present_error;
    int replace_calls = 0;
    int visibility_calls = 0;
    int present_calls = 0;
    int reset_calls = 0;
    bool is_visible = false;
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
    };
}

} // namespace

TEST_CASE("macOS presenter creates no work until visible with a surface") {
    auto adapter = std::make_unique<FakePresenterAdapter>();
    auto* fake = adapter.get();
    MacVideoPresenter presenter(std::move(adapter));

    CHECK(presenter.present(frame(1, 1280, 720)).value().code ==
          VideoPresenterErrorCode::not_visible);
    CHECK(fake->present_calls == 0);

    REQUIRE_FALSE(presenter.set_visible(true));
    CHECK(presenter.present(frame(1, 1280, 720)).value().code ==
          VideoPresenterErrorCode::surface_unavailable);
    CHECK(fake->present_calls == 0);
}

TEST_CASE("macOS presenter replaces surfaces and reconfigures on resize") {
    auto adapter = std::make_unique<FakePresenterAdapter>();
    auto* fake = adapter.get();
    MacVideoPresenter presenter(std::move(adapter));
    auto* first_surface = reinterpret_cast<void*>(1);
    auto* second_surface = reinterpret_cast<void*>(2);

    REQUIRE_FALSE(presenter.replace_surface(first_surface));
    REQUIRE_FALSE(presenter.set_visible(true));
    REQUIRE_FALSE(presenter.present(frame(1, 1280, 720)));
    REQUIRE_FALSE(presenter.present(frame(2, 1920, 1080)));
    REQUIRE_FALSE(presenter.replace_surface(second_surface));

    const auto stats = presenter.statistics();
    CHECK(stats.frames_presented == 2);
    CHECK(stats.reconfigurations == 2);
    CHECK(stats.surface_replacements == 1);
    CHECK(stats.source_width == 1920);
    CHECK(stats.source_height == 1080);
    CHECK(fake->current_surface == second_surface);
}

TEST_CASE("macOS presenter detaches native surfaces before teardown") {
    auto adapter = std::make_unique<FakePresenterAdapter>();
    auto* fake = adapter.get();
    MacVideoPresenter presenter(std::move(adapter));
    REQUIRE_FALSE(presenter.replace_surface(reinterpret_cast<void*>(1)));
    REQUIRE_FALSE(presenter.set_visible(true));

    presenter.reset();
    presenter.reset();

    CHECK(fake->reset_calls == 2);
    CHECK(fake->current_surface == nullptr);
    CHECK_FALSE(presenter.visible());
    CHECK(presenter.surface() == nullptr);
}

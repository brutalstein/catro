#include <catro/platform/windows/video_presenter.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace catro::platform::windows;

TEST_CASE("D3D11 composition video presenter is inert until a GPU frame arrives") {
    D3D11CompositionVideoPresenter presenter;

    CHECK_FALSE(presenter.swap_chain());
    const auto stats = presenter.statistics();
    CHECK(stats.frames_presented == 0);
    CHECK(stats.frames_dropped == 0);
    CHECK(stats.reconfigurations == 0);
    CHECK(stats.source_width == 0);
    CHECK(stats.output_width == 0);

    presenter.reset();
    presenter.reset();
    CHECK_FALSE(presenter.swap_chain());
}

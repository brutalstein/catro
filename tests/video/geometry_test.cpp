#include <catro/video/geometry.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace catro::video;

TEST_CASE("even video fit preserves common display ratios at the largest bounded scale") {
    CHECK(fit_even_video_extent(2560, 1600, 2560, 1080) ==
          VideoExtent{1728, 1080});
    CHECK(fit_even_video_extent(3840, 2160, 2560, 1080) ==
          VideoExtent{1920, 1080});
    CHECK(fit_even_video_extent(1920, 1080, 1280, 720) ==
          VideoExtent{1280, 720});
}

TEST_CASE("even video fit handles coprime odd source dimensions without collapsing scale") {
    const auto fitted = fit_even_video_extent(1365, 767, 2560, 1080);
    REQUIRE(fitted);
    CHECK(*fitted == VideoExtent{1364, 766});
}

TEST_CASE("even video fit never upscales a smaller source") {
    CHECK(fit_even_video_extent(1280, 720, 2560, 1080) ==
          VideoExtent{1280, 720});
}

TEST_CASE("even video fit rejects dimensions that cannot form an NV12 frame") {
    CHECK_FALSE(fit_even_video_extent(0, 1080, 1920, 1080));
    CHECK_FALSE(fit_even_video_extent(1, 1, 1920, 1080));
}

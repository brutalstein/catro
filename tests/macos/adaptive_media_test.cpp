#include <catro/macos_adaptive_media.hpp>

#include "fixtures/capability_fixtures.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

catro::platform::macos::CaptureSource display_source() {
    return {
        .kind = catro::platform::macos::CaptureSourceKind::display,
        .native_id = 1,
        .title = "Display",
        .width = 3024,
        .height = 1964,
        // Zero deliberately uses the snapshot's primary display, so fixture IDs do not need to
        // mimic production CGDirectDisplayID strings.
        .display_id = 0,
        .primary = true,
    };
}

} // namespace

TEST_CASE("macOS adaptive share is conservative until capabilities arrive") {
    const auto choices = catro::product::adaptive_share_qualities(nullptr, display_source());
    REQUIRE_FALSE(choices.empty());
    CHECK(choices.back().max_height <= 720);
    CHECK(choices.back().fps <= 30);
    CHECK(choices.back().recommended);
}

TEST_CASE("macOS adaptive share uses the balanced Apple Silicon media envelope") {
    const auto snapshot = catro::fixtures::apple_silicon_macbook();
    const auto choices = catro::product::adaptive_share_qualities(&snapshot, display_source());
    REQUIRE_FALSE(choices.empty());
    CHECK(choices.back().max_height <= 1440);
    CHECK(choices.back().fps <= 60);
    CHECK(choices.back().max_height > 720);
    CHECK(choices.back().recommended);
}

TEST_CASE("macOS adaptive share drops to the thermal-safe envelope") {
    const auto snapshot = catro::fixtures::hot_apple_silicon();
    const auto quality = catro::product::adaptive_share_quality(
        &snapshot, display_source(), 2560, 1440, 60);
    CHECK(quality.max_height <= 720);
    CHECK(quality.fps <= 30);
    CHECK(quality.profile == catro::capabilities::OperatingProfile::thermal_constrained);
}

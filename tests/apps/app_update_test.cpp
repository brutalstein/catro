#include <AppUpdate.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace catro::app;

TEST_CASE("the latest-release redirect names the release tag") {
    CHECK(release_tag("https://github.com/brutalstein/catro/releases/tag/v0.3.8") == "v0.3.8");
    CHECK(release_tag("https://github.com/brutalstein/catro/releases/tag/v1.10.0/") == "v1.10.0");
    // No release yet: GitHub answers the plain releases page.
    CHECK_FALSE(release_tag("https://github.com/brutalstein/catro/releases"));
    CHECK_FALSE(release_tag("https://github.com/brutalstein/catro/releases/tag/"));
    CHECK_FALSE(release_tag("https://example.com/releases/tag/v9.9.9"));
    CHECK_FALSE(release_tag("https://github.com/brutalstein/catro/releases/tag/v0.3.8&x=1"));
}

TEST_CASE("only a strictly newer x.y.z release is offered") {
    CHECK(is_newer_release("v0.3.8", "0.3.7"));
    CHECK(is_newer_release("v0.4.0", "0.3.9"));
    CHECK(is_newer_release("v0.10.0", "0.9.9"));
    CHECK(is_newer_release("1.0.0", "0.99.99"));
    CHECK_FALSE(is_newer_release("v0.3.7", "0.3.7"));
    CHECK_FALSE(is_newer_release("v0.3.6", "0.3.7"));
    CHECK_FALSE(is_newer_release("v0.3", "0.3.7"));
    CHECK_FALSE(is_newer_release("v0.3.8-beta", "0.3.7"));
    CHECK_FALSE(is_newer_release("latest", "0.3.7"));
    CHECK_FALSE(is_newer_release("v0.3.8", "not-a-version"));
}

TEST_CASE("the app knows the version it was built as") {
    CHECK(is_newer_release("v999.0.0", app_version()));
    CHECK_FALSE(is_newer_release(app_version(), app_version()));
}

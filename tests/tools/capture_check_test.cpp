#include <capture_check.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string_view>
#include <vector>

using namespace catro::tools;

namespace {

std::optional<CaptureCheckOptions> parse(std::vector<std::string_view> arguments) {
    return parse_capture_check_arguments(arguments);
}

} // namespace

TEST_CASE("capture-check defaults to a short bounded validation run") {
    const auto defaults = parse({});
    REQUIRE(defaults);
    CHECK(defaults->duration == std::chrono::seconds(10));
}

TEST_CASE("capture-check accepts seconds once and rejects ambiguous arguments") {
    const auto explicit_duration = parse({"--seconds", "20"});
    REQUIRE(explicit_duration);
    CHECK(explicit_duration->duration == std::chrono::seconds(20));

    CHECK_FALSE(parse({"--seconds"}));
    CHECK_FALSE(parse({"--seconds", "0"}));
    CHECK_FALSE(parse({"--seconds", "61"}));
    CHECK_FALSE(parse({"--seconds", "2s"}));
    CHECK_FALSE(parse({"--seconds", "5", "--seconds", "5"}));
    CHECK_FALSE(parse({"--source", "primary"}));
}

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
    CHECK_FALSE(parse({"--source"}));
    CHECK_FALSE(parse({"--source", "a", "--source", "b"}));
    CHECK_FALSE(parse({"--list", "--list"}));
}

TEST_CASE("capture-check lists sources or captures one by name") {
    const auto listed = parse({"--list"});
    REQUIRE(listed);
    CHECK(listed->list);
    const auto named = parse({"--source", "Counter-Strike", "--seconds", "5"});
    REQUIRE(named);
    CHECK(named->source == "Counter-Strike");
    CHECK(named->duration == std::chrono::seconds(5));
}

#include "ShellModel.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>

using namespace catro::app;

TEST_CASE("shell sections have stable unique ids and non-empty presentation") {
    std::set<std::string> ids;
    for (const auto& section : kShellSections) {
        CHECK_FALSE(section.id.empty());
        CHECK_FALSE(section.title.empty());
        CHECK_FALSE(section.eyebrow.empty());
        CHECK_FALSE(section.summary.empty());
        CHECK_FALSE(section.empty_title.empty());
        CHECK_FALSE(section.empty_detail.empty());
        CHECK(ids.insert(std::string(section.id)).second);
        CHECK(shell_section_spec(section.section) == section);
        REQUIRE(shell_section_from_id(section.id));
        CHECK(*shell_section_from_id(section.id) == section.section);
    }
}

TEST_CASE("shell state changes only for valid distinct destinations") {
    ShellState state;
    CHECK(state.active() == ShellSection::home);
    CHECK(state.active_spec().id == "home");

    CHECK(state.activate("voice"));
    CHECK(state.active() == ShellSection::voice);
    CHECK_FALSE(state.activate("voice"));
    CHECK_FALSE(state.activate("not-a-section"));
    CHECK(state.active() == ShellSection::voice);

    CHECK(state.activate(ShellSection::settings));
    CHECK(state.active_spec().id == "settings");
}

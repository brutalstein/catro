#include <UiActivityPolicy.hpp>
#include <Settings/Profile.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;
using catro::shell::WindowActivity;
using catro::shell::ui_refresh_policy;

TEST_CASE("minimized Windows workspace has no presentation polling or self preview") {
    const auto policy = ui_refresh_policy(WindowActivity::hidden, true, true);
    CHECK(policy.messages == 0ms);
    CHECK(policy.roster == 0ms);
    CHECK(policy.voice == 0ms);
    CHECK(policy.screen == 0ms);
    CHECK_FALSE(policy.local_preview);
}

TEST_CASE("inactive visible workspace reduces polling without stopping a visible stream") {
    const auto policy = ui_refresh_policy(WindowActivity::background, true, true);
    CHECK(policy.messages == 5s);
    CHECK(policy.roster == 30s);
    CHECK(policy.voice == 2s);
    CHECK(policy.screen == 1s);
    CHECK(policy.local_preview);
}

TEST_CASE("foreground restores responsive polling and only previews the voice page") {
    const auto policy = ui_refresh_policy(WindowActivity::foreground, true, true);
    CHECK(policy.messages == 1s);
    CHECK(policy.roster == 5s);
    CHECK(policy.voice == 250ms);
    CHECK(policy.screen == 250ms);
    CHECK(policy.local_preview);
    CHECK_FALSE(ui_refresh_policy(WindowActivity::foreground, true, false).local_preview);
}

TEST_CASE("unloaded pages sleep while an explicitly opened popout remains refreshable") {
    for (const auto activity : {WindowActivity::foreground, WindowActivity::background,
                                WindowActivity::hidden}) {
        const auto unloaded = ui_refresh_policy(activity, false, true);
        CHECK(unloaded.messages == 0ms);
        CHECK(unloaded.roster == 0ms);
        CHECK(unloaded.voice == 0ms);
        CHECK(unloaded.screen == 0ms);
        CHECK_FALSE(unloaded.local_preview);

        const auto popout = ui_refresh_policy(activity, true, true, true);
        CHECK(popout.screen > 0ms);
    }
}

TEST_CASE("profile names are trimmed bounded and cannot inject extra chat row fields") {
    CHECK(catro::shell::profile_name("  Ada  ") == "Ada");
    CHECK(catro::shell::profile_name("Ada Lovelace") == "Ada Lovelace");
    CHECK_FALSE(catro::shell::profile_name(" \t "));
    CHECK_FALSE(catro::shell::profile_name("Ada\nAdmin"));
    CHECK_FALSE(catro::shell::profile_name(std::string(65, 'x')));
    CHECK(catro::shell::profile_name(std::string(64, 'x')).has_value());
}

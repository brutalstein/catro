#include <UiActivityPolicy.hpp>
#include <Settings/Profile.hpp>
#include <Settings/UserVolumes.hpp>
#include <Server/ChatTimeline.hpp>
#include <catch2/catch_test_macros.hpp>

#include <sstream>

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

        // The stream window covers Catro, so Catro itself is in the background or hidden.
        const auto popout = ui_refresh_policy(activity, true, true, true);
        CHECK(popout.screen == 250ms);
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

TEST_CASE("chat groups local calendar days across year and leap day boundaries") {
    std::tm today{};
    today.tm_year = 126;
    today.tm_mon = 0;
    today.tm_mday = 1;
    today.tm_hour = 0;
    auto same_day = today;
    same_day.tm_hour = 23;
    CHECK(catro::shell::message_day(today) == catro::shell::message_day(same_day));
    auto yesterday = today;
    yesterday.tm_year = 125;
    yesterday.tm_mon = 11;
    yesterday.tm_mday = 31;
    CHECK(catro::shell::message_day_age(yesterday, today) == 1);
    CHECK(catro::shell::message_day(yesterday) != catro::shell::message_day(today));
    today.tm_year = 124;
    today.tm_mon = 2;
    today.tm_mday = 1;
    yesterday.tm_year = 124;
    yesterday.tm_mon = 1;
    yesterday.tm_mday = 29;
    CHECK(catro::shell::message_day_age(yesterday, today) == 1);
    yesterday.tm_mday = 28;
    CHECK(catro::shell::message_day_age(yesterday, today) == 2);
}

TEST_CASE("voice channel participants use connected membership with immediate self correction") {
    using catro::shell::voice_member_visible;
    CHECK(voice_member_visible(false, false, "voice", "voice"));
    CHECK_FALSE(voice_member_visible(false, true, "", "voice"));
    CHECK_FALSE(voice_member_visible(false, true, "other", "voice"));
    CHECK_FALSE(voice_member_visible(false, true, "", ""));
    CHECK(voice_member_visible(true, true, "", "voice"));
    CHECK_FALSE(voice_member_visible(true, false, "voice", "voice"));
}

TEST_CASE("saved participant volumes round trip independently and reject malformed values") {
    const std::string alice(64, 'a');
    const std::string bob(64, 'b');
    std::map<std::string, float> volumes;
    catro::shell::read_user_volume(volumes, "user-volume." + alice, "1.75");
    catro::shell::read_user_volume(volumes, "user-volume." + bob, "0");
    CHECK(catro::shell::user_volume(volumes, alice) == 1.75F);
    CHECK(catro::shell::user_volume(volumes, bob) == 0.0F);
    CHECK(catro::shell::user_volume(volumes, std::string(64, 'c')) == 1.0F);
    std::ostringstream saved;
    catro::shell::write_user_volumes(saved, volumes);
    std::map<std::string, float> reloaded;
    std::istringstream input(saved.str());
    std::string line;
    while (std::getline(input, line)) {
        const auto split = line.find('=');
        catro::shell::read_user_volume(reloaded, line.substr(0, split), line.substr(split + 1));
    }
    CHECK(reloaded == volumes);
    catro::shell::read_user_volume(reloaded, "user-volume." + alice, "5");
    CHECK(catro::shell::user_volume(reloaded, alice) == 2.0F);
    catro::shell::read_user_volume(reloaded, "user-volume." + alice, "-1");
    CHECK(catro::shell::user_volume(reloaded, alice) == 0.0F);
    for (const auto text : {"nan", "inf", "bad", "1.5junk"}) {
        catro::shell::read_user_volume(reloaded, "user-volume." + alice, text);
        CHECK(catro::shell::user_volume(reloaded, alice) == 0.0F);
    }
    catro::shell::read_user_volume(reloaded, "user-volume.invalid", "1");
    CHECK(reloaded.size() == 2);
}

TEST_CASE("voice gain updates reclaim departed participants and skip unchanged gains") {
    std::map<std::string, float> applied;
    std::map<std::string, float> writes;
    const auto set_volume = [&](const std::string& id, float gain) { writes[id] = gain; };
    const std::map<std::string, float> first{{"alice", 1.75F}, {"bob", 0.0F}};
    catro::shell::sync_user_volumes(applied, first, set_volume);
    CHECK(applied == first);
    CHECK(writes == first);
    writes.clear();
    catro::shell::sync_user_volumes(applied, first, set_volume);
    CHECK(writes.empty());

    const std::map<std::string, float> second{{"bob", 0.5F}, {"carol", 1.25F}};
    catro::shell::sync_user_volumes(applied, second, set_volume);
    CHECK(writes.size() == 3);
    CHECK(writes["alice"] == 1.0F);
    CHECK(writes["bob"] == 0.5F);
    CHECK(writes["carol"] == 1.25F);
    CHECK(applied == second);
    writes.clear();
    catro::shell::sync_user_volumes(applied, {}, set_volume);
    CHECK(writes.size() == 2);
    CHECK(writes["bob"] == 1.0F);
    CHECK(writes["carol"] == 1.0F);
    CHECK(applied.empty());
}

#include <SharePresets.hpp>
#include <catch2/catch_test_macros.hpp>

using catro::shell::share_qualities;
using catro::shell::share_window_name;

TEST_CASE("a strong GPU on a 1440p screen offers up to 1440p at 60 FPS") {
    const auto choice = share_qualities(2560, 1440, true);
    REQUIRE(choice.options.size() == 3);
    CHECK(choice.options[0].label == L"720p");
    CHECK(choice.options[2].label == L"1440p");
    CHECK(choice.recommended == 2);
    for (const auto& option : choice.options) {
        CHECK(option.fps == 60);
        CHECK(option.bitrate >= 2'500'000);
        CHECK(option.bitrate <= 25'000'000);
    }
}

TEST_CASE("a 4K source adds Source but recommends 1440p") {
    const auto choice = share_qualities(3840, 2160, true);
    REQUIRE(choice.options.size() == 4);
    CHECK(choice.options[3].label == L"Source");
    CHECK(choice.options[3].max_width == 3840);
    CHECK(choice.options[3].max_height == 2160);
    CHECK(choice.options[choice.recommended].label == L"1440p");
}

TEST_CASE("an integrated GPU stops at 1080p and keeps 60 FPS only at 720p") {
    const auto choice = share_qualities(2560, 1440, false);
    REQUIRE(choice.options.size() == 2);
    CHECK(choice.options[0].fps == 60);
    CHECK(choice.options[1].label == L"1080p");
    CHECK(choice.options[1].fps == 30);
    CHECK(choice.recommended == 1);
}

TEST_CASE("presets never upscale; an odd-sized screen gets its own Source") {
    const auto laptop = share_qualities(1728, 1080, true);
    CHECK(laptop.options.back().label == L"1080p");

    const auto small = share_qualities(1366, 768, false);
    REQUIRE(small.options.size() == 2);
    CHECK(small.options[1].label == L"Source");
    CHECK(small.options[1].max_height == 768);

    const auto tiny = share_qualities(800, 600, true);
    REQUIRE(tiny.options.size() == 1);
    CHECK(tiny.options[0].label == L"Source");
}

TEST_CASE("browser windows read as browser and site") {
    CHECK(share_window_name(L"Lofi beats - YouTube - Brave", L"brave.exe", L"Brave Browser", false) ==
          L"Brave - YouTube");
    CHECK(share_window_name(L"(3) Inbox - Gmail — Mozilla Firefox", L"firefox.exe", L"", false) ==
          L"Firefox - Gmail");
    CHECK(share_window_name(L"New Tab - Google Chrome", L"CHROME.EXE", L"", false) == L"Chrome - New Tab");
    CHECK(share_window_name(L"Brave", L"brave.exe", L"", false) == L"Brave");
}

TEST_CASE("games show only their name; apps show their program name") {
    CHECK(share_window_name(L"Counter-Strike 2", L"cs2.exe", L"", true) == L"Counter-Strike 2");
    CHECK(share_window_name(L"main.cpp - catro - Visual Studio Code", L"Code.exe",
                            L"Visual Studio Code", false) == L"Visual Studio Code");
    CHECK(share_window_name(L"", L"spotify.exe", L"", false) == L"Spotify");
}

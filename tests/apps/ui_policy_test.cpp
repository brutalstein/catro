#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

std::string read(std::filesystem::path path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("product shell XAML stays on the low-cost composition path") {
    const std::filesystem::path root = CATRO_WINDOWS_XAML_DIR;
    const std::array files{
        "App.xaml",
        "MainWindow.xaml",
        "Server/ServerView.xaml",
        "Settings/SettingsView.xaml",
    };
    const std::array<std::string_view, 8> forbidden{
        "MicaBackdrop",
        "DesktopAcrylicBackdrop",
        "AcrylicBrush",
        "ThemeShadow",
        "DropShadow",
        "<Storyboard",
        "DoubleAnimation",
        "ColorAnimation",
    };

    for (const auto* relative : files) {
        INFO(relative);
        const auto text = read(root / relative);
        CHECK(text.size() < 64U * 1024U);
        for (const auto token : forbidden) {
            CHECK(text.find(token) == std::string::npos);
        }
        // Product decoration stays vector/text based; bitmap content must be justified explicitly.
        CHECK(text.find("<Image") == std::string::npos);
    }
}

TEST_CASE("product shell palette does not regress to a blue accent") {
    const auto app = read(std::filesystem::path(CATRO_WINDOWS_XAML_DIR) / "App.xaml");
    CHECK(app.find("CatroAccentBrush") != std::string::npos);
    CHECK(app.find("#0078D4") == std::string::npos);
    CHECK(app.find("#0067C0") == std::string::npos);
    CHECK(app.find("#60CDFF") == std::string::npos);
}

TEST_CASE("room screen sharing coordinates ownership with signaling") {
    const auto source =
        read(std::filesystem::path(CATRO_WINDOWS_XAML_DIR) /
             "Server/ServerView.xaml.cpp");

    CHECK(source.find(".max_remote_peers") != std::string::npos);
    CHECK(source.find("catro_room_runtime_claim_screen") != std::string::npos);
    CHECK(source.find("catro_room_runtime_release_screen") != std::string::npos);
    CHECK(source.find("std::chrono::milliseconds{50}") != std::string::npos);
    CHECK(source.find("std::chrono::seconds{3}") != std::string::npos);
    CHECK(source.find("Another participant is sharing") != std::string::npos);
    CHECK(source.find("Screen ownership request timed out") != std::string::npos);
}


TEST_CASE("text channel timeline remains virtualized and bounded") {
    const auto xaml =
        read(std::filesystem::path(CATRO_WINDOWS_XAML_DIR) /
             "Server/ServerView.xaml");
    const auto source =
        read(std::filesystem::path(CATRO_WINDOWS_XAML_DIR) /
             "Server/ServerView.xaml.cpp");

    CHECK(xaml.find("<ListView x:Name=\"MessageList\"") != std::string::npos);
    CHECK(xaml.find("<ListView.ItemTemplate>") != std::string::npos);
    CHECK(xaml.find("<DataTemplate>") != std::string::npos);
    CHECK(source.find("message_timer_.Interval(1s)") != std::string::npos);
    CHECK(source.find("MessageList().Items().Size() > 512") != std::string::npos);
    CHECK(source.find("winrt::resume_background()") != std::string::npos);
    CHECK(source.find("message_generation_") != std::string::npos);
}


TEST_CASE("member rail is virtualized and refreshes from bounded snapshots") {
    const auto xaml =
        read(std::filesystem::path(CATRO_WINDOWS_XAML_DIR) /
             "Server/ServerView.xaml");
    const auto source =
        read(std::filesystem::path(CATRO_WINDOWS_XAML_DIR) /
             "Server/ServerView.xaml.cpp");

    CHECK(xaml.find("<ListView x:Name=\"MemberList\"") != std::string::npos);
    CHECK(xaml.find("<ListView.ItemTemplate>") != std::string::npos);
    CHECK(source.find("member_timer_.Interval(5s)") != std::string::npos);
    CHECK(source.find("member_refresh_pending_") != std::string::npos);
    CHECK(source.find("member_generation_") != std::string::npos);
    CHECK(source.find("list_directory_members") != std::string::npos);
}


TEST_CASE("Server Code join flow stays bounded and preserves direct invites") {
    const auto xaml =
        read(std::filesystem::path(CATRO_WINDOWS_XAML_DIR) /
             "MainWindow.xaml");
    const auto source =
        read(std::filesystem::path(CATRO_WINDOWS_XAML_DIR) /
             "MainWindow.xaml.cpp");

    CHECK(xaml.find("ToolTipService.ToolTip=\"Add server\"") != std::string::npos);
    CHECK(source.find("Find by Server Code") != std::string::npos);
    CHECK(source.find("Use Invite Code") != std::string::npos);
    CHECK(source.find("lookup_directory_server") != std::string::npos);
    CHECK(source.find("create_directory_join_request") != std::string::npos);
    CHECK(source.find("cancel_directory_join_request") != std::string::npos);
    CHECK(source.find("accept_directory_invite") != std::string::npos);
    CHECK(source.find("join_request_timer_.Interval(std::chrono::seconds{5})") !=
          std::string::npos);
    CHECK(source.find("join_request_refresh_pending_") != std::string::npos);
    CHECK(source.find("list_outgoing_directory_join_requests") != std::string::npos);
    CHECK(source.find("BeginDirectoryServerRefresh") != std::string::npos);
    CHECK(source.find("approved_join_requests_waiting_refresh_") != std::string::npos);
}

TEST_CASE("owner access requests stay virtualized authorized and bounded") {
    const auto xaml =
        read(std::filesystem::path(CATRO_WINDOWS_XAML_DIR) /
             "Server/ServerView.xaml");
    const auto source =
        read(std::filesystem::path(CATRO_WINDOWS_XAML_DIR) /
             "Server/ServerView.xaml.cpp");

    CHECK(xaml.find("x:Name=\"AccessButton\"") != std::string::npos);
    CHECK(xaml.find("Visibility=\"Collapsed\"") != std::string::npos);
    CHECK(source.find("access_timer_.Interval(5s)") != std::string::npos);
    CHECK(source.find("access_refresh_pending_") != std::string::npos);
    CHECK(source.find("access_generation_") != std::string::npos);
    CHECK(source.find("controls::ListView request_list") != std::string::npos);
    CHECK(source.find("list_pending_directory_join_requests") != std::string::npos);
    CHECK(source.find("decide_directory_join_request") != std::string::npos);
    CHECK(source.find("directory_server_->role != \"owner\"") != std::string::npos);
    CHECK(source.find("directory_server_->public_code.empty()") != std::string::npos);
}

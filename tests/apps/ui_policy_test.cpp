#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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

std::size_t line_count(std::string_view text) {
    return 1U + static_cast<std::size_t>(std::ranges::count(text, '\n'));
}

std::string read_main_sources(const std::filesystem::path& root) {
    return read(root / "MainWindow.xaml.cpp") +
           read(root / "MainWindow.Directory.cpp");
}

std::string read_server_sources(const std::filesystem::path& root) {
    return read(root / "Server/ServerView.xaml.cpp") +
           read(root / "Server/ServerView.Directory.cpp") +
           read(root / "Server/ServerView.Screen.cpp") +
           read(root / "Server/ServerView.State.cpp") +
           read(root / "Server/ServerView.Voice.cpp");
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

TEST_CASE("shell exposes connection state instead of silently disabling online actions") {
    const auto root = std::filesystem::path(CATRO_WINDOWS_XAML_DIR);
    const auto main_xaml = read(root / "MainWindow.xaml");
    const auto server_xaml = read(root / "Server/ServerView.xaml");

    CHECK(main_xaml.find("x:Name=\"ShellStatusText\"") != std::string::npos);
    CHECK(main_xaml.find("x:Name=\"JoinServerButton\"") != std::string::npos);
    CHECK(main_xaml.find("IsEnabled=\"False\"") == std::string::npos);
    CHECK(server_xaml.find("x:Name=\"OnlineStatusText\"") != std::string::npos);
    CHECK(server_xaml.find("AutomationProperties.LiveSetting=\"Polite\"") !=
          std::string::npos);
}

TEST_CASE("product shell avoids continuous decorative motion") {
    const auto source =
        read(std::filesystem::path(CATRO_WINDOWS_XAML_DIR) /
             "MainWindow.xaml.cpp");
    CHECK(source.find("StartAnimation") == std::string::npos);
    CHECK(source.find("IterationBehavior::Forever") == std::string::npos);
}

TEST_CASE("Windows product code-behind is split by responsibility") {
    const auto root = std::filesystem::path(CATRO_WINDOWS_XAML_DIR);
    const std::array files{
        "MainWindow.xaml.cpp",
        "MainWindow.Directory.cpp",
        "Server/ServerView.xaml.cpp",
        "Server/ServerView.Directory.cpp",
        "Server/ServerView.Voice.cpp",
        "Server/ServerView.Screen.cpp",
        "Server/ServerView.State.cpp",
    };

    for (const auto* relative : files) {
        INFO(relative);
        REQUIRE(std::filesystem::exists(root / relative));
        CHECK(line_count(read(root / relative)) < 1500U);
    }

    CHECK(line_count(read(root / "MainWindow.xaml.cpp")) < 450U);
    CHECK(line_count(read(root / "Server/ServerView.xaml.cpp")) < 700U);
}

TEST_CASE("async server actions publish visible busy and failure reasons") {
    const auto root = std::filesystem::path(CATRO_WINDOWS_XAML_DIR);
    const auto main_source = read_main_sources(root);
    const auto server_source = read_server_sources(root);

    CHECK(main_source.find("workspace_state_.join_server.reason") !=
          std::string::npos);
    CHECK(main_source.find("Looking up Server Code") != std::string::npos);
    CHECK(main_source.find("Sending access request") != std::string::npos);
    CHECK(main_source.find("Joining server") != std::string::npos);
    CHECK(main_source.find("workspace_state_.join_server.fail") !=
          std::string::npos);
    CHECK(server_source.find("Creating invite") != std::string::npos);
    CHECK(server_source.find("OnlineStatusText().Text") != std::string::npos);
}

TEST_CASE("server action presentation state is authoritative") {
    const auto root = std::filesystem::path(CATRO_WINDOWS_XAML_DIR);
    const auto source = read_server_sources(root);
    const auto directory =
        read(root / "Server/ServerView.Directory.cpp");

    CHECK(source.find("workspace_state_.send_message.begin") !=
          std::string::npos);
    CHECK(source.find("workspace_state_.send_message.fail") !=
          std::string::npos);
    CHECK(source.find("workspace_state_.send_message.enable") !=
          std::string::npos);
    CHECK(source.find("workspace_state_.join_voice.begin") !=
          std::string::npos);
    CHECK(source.find("workspace_state_.join_voice.fail") !=
          std::string::npos);
    CHECK(source.find("workspace_state_.join_voice.enable") !=
          std::string::npos);
    CHECK(source.find("workspace_state_.share_screen.begin") !=
          std::string::npos);
    CHECK(source.find("workspace_state_.share_screen.fail") !=
          std::string::npos);
    CHECK(source.find("workspace_state_.share_screen.enable") !=
          std::string::npos);
    CHECK(directory.find("workspace_state_.synchronize()") ==
          std::string::npos);
    CHECK(directory.find("workspace_state_.connection =") !=
          std::string::npos);
    CHECK(directory.find("catro::app::ConnectionState::synchronized") !=
          std::string::npos);
}

TEST_CASE("server switches invalidate pending invite and voice results") {
    const auto root = std::filesystem::path(CATRO_WINDOWS_XAML_DIR);
    const auto header = read(root / "Server/ServerView.xaml.h");
    const auto directory = read(root / "Server/ServerView.Directory.cpp");
    const auto voice = read(root / "Server/ServerView.Voice.cpp");

    CHECK(header.find("invite_generation_") != std::string::npos);
    CHECK(header.find("voice_join_generation_") != std::string::npos);
    CHECK(directory.find("const auto generation = ++invite_generation_") !=
          std::string::npos);
    CHECK(directory.find("generation != lifetime->invite_generation_") !=
          std::string::npos);
    CHECK(voice.find("const auto generation = ++voice_join_generation_") !=
          std::string::npos);
    CHECK(voice.find("generation != lifetime->voice_join_generation_") !=
          std::string::npos);
}

TEST_CASE("screen source enumeration leaves the UI thread") {
    const auto source =
        read(std::filesystem::path(CATRO_WINDOWS_XAML_DIR) /
             "Server/ServerView.Screen.cpp");
    const auto begin = source.find("ServerView::BeginScreenShare()");
    const auto background =
        source.find("co_await winrt::resume_background()", begin);
    const auto enumerate =
        source.find("enumerate_capture_sources()", begin);
    const auto ui =
        source.find("co_await ui_thread", enumerate);

    REQUIRE(begin != std::string::npos);
    REQUIRE(background != std::string::npos);
    REQUIRE(enumerate != std::string::npos);
    REQUIRE(ui != std::string::npos);
    CHECK(begin < background);
    CHECK(background < enumerate);
    CHECK(enumerate < ui);
}

TEST_CASE("Windows keyboard smoke avoids the crashing UI Automation path") {
    const auto xaml_root = std::filesystem::path(CATRO_WINDOWS_XAML_DIR);
    const auto repo_root =
        xaml_root.parent_path().parent_path().parent_path();
    const auto script =
        read(repo_root / "tests/ui/windows_keyboard_smoke.ps1");

    CHECK(script.find("SendInput") != std::string::npos);
    CHECK(script.find("HasExited") != std::string::npos);
    CHECK(script.find("CopyFromScreen") != std::string::npos);
    CHECK(script.find("System.Windows.Automation") == std::string::npos);
    CHECK(script.find("IUIAutomation") == std::string::npos);
    CHECK(script.find("if (-not [CatroKeyboardSmoke.Native]::SetForegroundWindow") !=
          std::string::npos);
    CHECK(script.find("Keyboard smoke passed") == std::string::npos);
    CHECK(script.find("manual visual inspection required") !=
          std::string::npos);
}

TEST_CASE("icon-only server controls keep accessible names and minimum targets") {
    const auto xaml =
        read(std::filesystem::path(CATRO_WINDOWS_XAML_DIR) /
             "Server/ServerView.xaml");

    CHECK(xaml.find("AutomationProperties.Name=\"Profile mute microphone\"") !=
          std::string::npos);
    CHECK(xaml.find("AutomationProperties.Name=\"Profile deafen audio\"") !=
          std::string::npos);
    CHECK(xaml.find("Width=\"28\" Height=\"28\"") == std::string::npos);
    CHECK(xaml.find("Width=\"34\"") == std::string::npos);
    CHECK(xaml.find("Width=\"30\"") == std::string::npos);
}

TEST_CASE("dynamic server controls keep accessible names") {
    const auto source =
        read(std::filesystem::path(CATRO_WINDOWS_XAML_DIR) /
             "MainWindow.Directory.cpp");

    CHECK(source.find("AutomationProperties::SetName") !=
          std::string::npos);
    CHECK(source.find("name.empty() ? hstring{L\"Server\"} : name") !=
          std::string::npos);
}

TEST_CASE("room screen sharing coordinates ownership with signaling") {
    const auto source =
        read_server_sources(std::filesystem::path(CATRO_WINDOWS_XAML_DIR));

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
        read_server_sources(std::filesystem::path(CATRO_WINDOWS_XAML_DIR));

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
        read_server_sources(std::filesystem::path(CATRO_WINDOWS_XAML_DIR));

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
        read_main_sources(std::filesystem::path(CATRO_WINDOWS_XAML_DIR));

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
        read_server_sources(std::filesystem::path(CATRO_WINDOWS_XAML_DIR));

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

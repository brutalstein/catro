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
           read(root / "Server/ServerView.Activity.cpp") +
           read(root / "Server/ServerView.Voice.cpp");
}

} // namespace

TEST_CASE("product shell XAML stays on the low-cost composition path") {
    const std::filesystem::path root = CATRO_WINDOWS_XAML_DIR;
    const std::array files{
        "App.xaml",
        "Themes/Palette.xaml",
        "Themes/Controls.xaml",
        "Server/ServerTemplates.xaml",
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
    const auto app = read(std::filesystem::path(CATRO_WINDOWS_XAML_DIR) / "Themes/Palette.xaml");
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
    CHECK(xaml.find("CatroMessageTemplate") != std::string::npos);
    CHECK(read(std::filesystem::path(CATRO_WINDOWS_XAML_DIR) / "Server/ServerTemplates.xaml").find("<DataTemplate") != std::string::npos);
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
    CHECK(xaml.find("CatroMemberTemplate") != std::string::npos);
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

TEST_CASE("macOS product shell stays native accessible and free of view-body media work") {
    const std::filesystem::path root = CATRO_MACOS_UI_DIR;
    const std::array views{
        "Product/ServerWorkspace.swift", "Product/ChannelSidebar.swift", "Product/MemberSidebar.swift",
        "Product/VoiceControls.swift",   "Product/StreamViewer.swift",   "Product/SourcePicker.swift",
        "Product/SettingsView.swift",    "CatroApp.swift",
    };
    std::string all;
    for (const auto* file : views) {
        const auto source = read(root / file);
        INFO(file);
        // Only AppModel owns the bridge; views never reach native media directly.
        CHECK(source.find("CatroProductBridge") == std::string::npos);
        CHECK(source.find("bridge.") == std::string::npos);
        // Catro's own source picker, never the stock ScreenCaptureKit one.
        CHECK(source.find("SCContentSharingPicker") == std::string::npos);
        all += source;
    }

    CHECK(read(root / "Product/ServerWorkspace.swift").find("NavigationSplitView") != std::string::npos);
    for (const auto* file : {"Product/ServerWorkspace.swift", "Product/ChannelSidebar.swift",
                             "Product/MemberSidebar.swift", "Product/SourcePicker.swift"}) {
        INFO(file);
        const auto source = read(root / file);
        CHECK((source.find("List(") != std::string::npos || source.find("List {") != std::string::npos));
    }
    CHECK(all.find(".accessibilityLabel(") != std::string::npos);
    CHECK(all.find(".help(") != std::string::npos);

    // Shared audio follows the source like Discord: app audio for a window, system audio for a display.
    const auto picker = read(root / "Product/SourcePicker.swift");
    CHECK(picker.find("\"Share app audio\"") != std::string::npos);
    CHECK(picker.find("\"Share computer audio\"") != std::string::npos);
    CHECK(picker.find(".disabled(true)") == std::string::npos);

    const auto app = read(root / "CatroApp.swift");
    CHECK(app.find(".keyboardShortcut(\"j\", modifiers: [.command, .shift])") != std::string::npos);
    CHECK(app.find(".keyboardShortcut(\"m\", modifiers: [.command, .shift])") != std::string::npos);
    CHECK(app.find(".keyboardShortcut(\"d\", modifiers: [.command, .shift])") != std::string::npos);

    // Any motion must honour Reduce Motion.
    if (all.find("withAnimation") != std::string::npos || all.find(".animation(") != std::string::npos) {
        CHECK(all.find("accessibilityReduceMotion") != std::string::npos);
    }

    // Layers attach only from NSView lifecycle callbacks.
    const auto stream = read(root / "Product/StreamViewer.swift");
    CHECK(stream.find("NSViewRepresentable") != std::string::npos);
    CHECK(stream.find("dismantleNSView") != std::string::npos);

    // Same Settings rows and values as Windows: an editable profile name and the System, Ivory and
    // Espresso themes the Windows appearance picker offers.
    const auto settings = read(root / "Product/SettingsView.swift");
    for (const auto* row : {"Section(\"Profile\")", "model.rename(", "Picker(\"Theme\"", "\"Ivory\"",
                            "\"Espresso\"", "\"Microphone\", value: \"Default\"",
                            "\"Output\", value: \"Default\"", "\"Profile\", value: \"Balanced\""}) {
        INFO(row);
        CHECK(settings.find(row) != std::string::npos);
    }
}

TEST_CASE("Windows shell coroutines return to the UI thread through its DispatcherQueue") {
    // WinUI 3's generated wWinMain initializes the UI thread in the MTA, so an apartment_context
    // captured there never marshals back and the next XAML call fails with RPC_E_WRONG_THREAD.
    const auto root = std::filesystem::path(CATRO_WINDOWS_XAML_DIR);
    const auto source = read_main_sources(root) + read_server_sources(root);
    CHECK(source.find("apartment_context") == std::string::npos);
    CHECK(source.find("UiThread ui_thread") != std::string::npos);

    const auto pch = read(root / "pch.h");
    CHECK(pch.find("TryEnqueue") != std::string::npos);
    CHECK(pch.find("HasThreadAccess") != std::string::npos);

    // An STA UI thread keeps the XAML island walkable by cross-process UI Automation clients.
    CHECK(read(root / "Catro.vcxproj").find("DISABLE_XAML_GENERATED_MAIN") != std::string::npos);
    CHECK(read(root / "App.xaml.cpp").find("init_apartment(winrt::apartment_type::single_threaded)") !=
          std::string::npos);
}

TEST_CASE("Windows shell defaults to the accessible ivory theme with a persisted appearance choice") {
    const auto root = std::filesystem::path(CATRO_WINDOWS_XAML_DIR);
    const auto app = read(root / "Themes/Palette.xaml") + read(root / "Themes/Controls.xaml");
    // Ivory canvas, and WinUI's own accent consumers (NavigationView, accent buttons) follow Catro.
    CHECK(app.find("Color=\"#FBF8F1\"") != std::string::npos);
    CHECK(app.find("x:Key=\"SystemAccentColor\"") != std::string::npos);
    CHECK(app.find("BasedOn=\"{StaticResource AccentButtonStyle}\"") != std::string::npos);
    CHECK(app.find("x:Key=\"TextOnAccentFillColorPrimaryBrush\"") != std::string::npos);

    const auto settings = read(root / "Settings/SettingsView.xaml");
    CHECK(settings.find("x:Name=\"AppearanceBox\"") != std::string::npos);
    for (const auto* choice : {"Ivory", "Dark", "System"}) {
        INFO(choice);
        CHECK(settings.find(std::string{"Content=\""} + choice + "\"") != std::string::npos);
    }
    const auto main = read_main_sources(root);
    CHECK(main.find("load_appearance") != std::string::npos);
    CHECK(main.find("ActualThemeChanged") != std::string::npos);
}

TEST_CASE("Windows product text stays readable") {
    const auto root = std::filesystem::path(CATRO_WINDOWS_XAML_DIR);
    for (const auto* relative : {"MainWindow.xaml", "Server/ServerView.xaml", "Settings/SettingsView.xaml"}) {
        INFO(relative);
        const auto xaml = read(root / relative);
        for (std::size_t at = xaml.find("FontSize=\""); at != std::string::npos;
             at = xaml.find("FontSize=\"", at + 1)) {
            const auto size = std::stoi(xaml.substr(at + 10, 3));
            INFO(xaml.substr(at, 16));
            CHECK(size >= 11);
        }
    }
}

TEST_CASE("Windows message and member rows render through recycled containers") {
    const auto root = std::filesystem::path(CATRO_WINDOWS_XAML_DIR);
    const auto xaml = read(root / "Server/ServerView.xaml");
    CHECK(xaml.find("ContainerContentChanging=\"OnMessageContainerChanging\"") != std::string::npos);
    CHECK(xaml.find("ContainerContentChanging=\"OnMemberContainerChanging\"") != std::string::npos);
    // Rows must not fall back to one flat string with a single style.
    CHECK(xaml.find("<TextBlock Text=\"{Binding}\"") == std::string::npos);
}

TEST_CASE("Windows efficiency policy is wired to real window and page lifecycle") {
    const auto root = std::filesystem::path(CATRO_WINDOWS_XAML_DIR);
    const auto main = read_main_sources(root);
    const auto activity = read(root / "Server/ServerView.Activity.cpp");
    CHECK(main.find("IsIconic(hwnd)") != std::string::npos);
    CHECK(main.find("VisibilityChanged(") != std::string::npos);
    CHECK(main.find("AppWindow().Changed(") != std::string::npos);
    CHECK(main.find("SetWindowActivity(window_activity_)") != std::string::npos);
    CHECK(activity.find("ui_refresh_policy(") != std::string::npos);
    CHECK(activity.find("timer.Stop()") != std::string::npos);
    // Presentation pacing must not disconnect media.
    CHECK(activity.find("catro_voice_runtime_stop") == std::string::npos);
    CHECK(activity.find("catro_room_runtime_leave") == std::string::npos);
    CHECK(activity.find("screen_runtime_->stop()") == std::string::npos);
    CHECK(activity.find("set_remote_viewing_enabled(false)") == std::string::npos);
}

TEST_CASE("Windows resources are modular and settings explain automatic efficiency") {
    const auto root = std::filesystem::path(CATRO_WINDOWS_XAML_DIR);
    CHECK(line_count(read(root / "App.xaml")) < 30U);
    CHECK(line_count(read(root / "MainWindow.xaml")) < 240U);
    const auto settings = read(root / "Settings/SettingsView.xaml");
    CHECK(settings.find("<ScrollViewer") != std::string::npos);
    CHECK(settings.find("Automatic efficiency") != std::string::npos);
}

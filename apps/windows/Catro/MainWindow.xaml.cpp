#include "pch.h"

#include "MainWindow.xaml.h"
#include "resource.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "Diagnostics/DiagnosticsView.xaml.h"
#include "Server/ServerView.xaml.h"
#include "Settings/Appearance.hpp"
#include "Settings/SettingsView.xaml.h"

#include <catro/platform/windows/local_state.hpp>

#include <winrt/Windows.UI.h>

#include <chrono>
#include <filesystem>

namespace winrt::Catro::implementation {

namespace {

namespace xaml = Microsoft::UI::Xaml;
namespace controls = Microsoft::UI::Xaml::Controls;

} // namespace

MainWindow::~MainWindow() {
    if (join_request_timer_) {
        join_request_timer_.Stop();
    }
}

void MainWindow::InitializeComponent() {
    MainWindowT<MainWindow>::InitializeComponent();

    ExtendsContentIntoTitleBar(true);
    SetTitleBar(AppTitleBar());

    const auto transparent = Windows::UI::Color{0, 0, 0, 0};
    auto title_bar = AppWindow().TitleBar();
    title_bar.ButtonBackgroundColor(transparent);
    title_bar.ButtonInactiveBackgroundColor(transparent);

    ShellRoot().RequestedTheme(
        catro::shell::element_theme(catro::shell::load_appearance()));
    ShellRoot().ActualThemeChanged(
        [this](auto&&, auto&&) { UpdateCaptionButtons(); });
    UpdateCaptionButtons();

    const auto hwnd =
        Microsoft::UI::GetWindowFromWindowId(AppWindow().Id());

    std::wstring module_path(32768, L'\0');
    const auto module_length = GetModuleFileNameW(
        nullptr, module_path.data(), static_cast<DWORD>(module_path.size()));
    if (module_length > 0 && module_length < module_path.size()) {
        module_path.resize(module_length);
        const auto icon_path =
            std::filesystem::path{module_path}.parent_path() /
            L"Assets" / L"Catro.ico";
        std::error_code icon_error;
        if (std::filesystem::exists(icon_path, icon_error) &&
            !icon_error) {
            try {
                const hstring resolved_icon{icon_path.wstring()};
                AppWindow().SetIcon(resolved_icon);
                AppWindow().SetTaskbarIcon(resolved_icon);
                AppWindow().SetTitleBarIcon(resolved_icon);
            } catch (const winrt::hresult_error&) {
            }
        }
    }

    const auto icon = reinterpret_cast<HICON>(
        LoadImageW(
            GetModuleHandleW(nullptr),
            MAKEINTRESOURCEW(IDI_CATRO_APP),
            IMAGE_ICON,
            0,
            0,
            LR_DEFAULTSIZE | LR_SHARED));
    if (icon != nullptr && hwnd != nullptr) {
        (void)SendMessageW(
            hwnd, WM_SETICON, ICON_BIG,
            reinterpret_cast<LPARAM>(icon));
        (void)SendMessageW(
            hwnd, WM_SETICON, ICON_SMALL,
            reinterpret_cast<LPARAM>(icon));
    }

    const auto scale = GetDpiForWindow(hwnd) / 96.0;
    AppWindow().Resize({static_cast<int32_t>(1280 * scale), static_cast<int32_t>(820 * scale)});
    if (const auto presenter =
            AppWindow().Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>()) {
        presenter.PreferredMinimumWidth(static_cast<int32_t>(700 * scale));
        presenter.PreferredMinimumHeight(static_cast<int32_t>(480 * scale));
    }

    join_request_timer_ = DispatcherQueue().CreateTimer();
    join_request_timer_.Interval(std::chrono::seconds{5});
    join_request_timer_.Tick(
        [this](auto&&, auto&&) {
            BeginOutgoingJoinRequestRefresh();
        });
    Activated([this](auto&&, xaml::WindowActivatedEventArgs const& args) {
        window_active_ = args.WindowActivationState() != xaml::WindowActivationState::Deactivated;
        UpdateWindowActivity();
    });
    startup_timer_ = DispatcherQueue().CreateTimer();
    startup_timer_.Interval(std::chrono::seconds{6});
    startup_timer_.IsRepeating(false);
    startup_timer_.Tick([this](auto&&, auto&&) {
        if (startup_splash_) {
            StartupOfflineButton().Visibility(xaml::Visibility::Visible);
        } else {
            StartupSplash().Visibility(xaml::Visibility::Collapsed);
        }
    });
    startup_timer_.Start();
    Closed([this](auto&&, auto&&) {
        window_closed_ = true;
        join_request_timer_.Stop();
        startup_timer_.Stop();
    });
    VisibilityChanged([this](auto&&, auto&&) { UpdateWindowActivity(); });
    AppWindow().Changed([this](auto&&, auto&&) { UpdateWindowActivity(); });

    const auto local = catro::platform::windows::load_or_create_default_local_state();
    if (const auto* state = std::get_if<catro::community::LocalState>(&local)) {
        local_state_ = *state;
        TitleContext().Text(to_hstring(state->personal_server.name));
    } else {
        TitleContext().Text(L"State unavailable");
    }

    Activate(catro::app::AppDestination::server);
    UpdateConnectionUi();
    BeginDirectoryBootstrap();
}

void MainWindow::OnServer(IInspectable const&, xaml::RoutedEventArgs const&) {
    if (local_state_) {
        const auto personal_id =
            catro::community::to_hex(
                local_state_->personal_server.id);
        const auto found =
            std::ranges::find(
                directory_servers_,
                personal_id,
                &catro::platform::windows::
                    DirectoryServer::id);
        if (found != directory_servers_.end()) {
            ActivateDirectoryServer(found->id);
            return;
        }
    }
    active_directory_server_.reset();
    Activate(catro::app::AppDestination::server);
}

void MainWindow::OnJoinServer(
    IInspectable const&, xaml::RoutedEventArgs const&) {
    BeginJoinServer();
}

void MainWindow::OnDiagnostics(IInspectable const&, xaml::RoutedEventArgs const&) {
    Activate(catro::app::AppDestination::diagnostics);
}

void MainWindow::OnSettings(IInspectable const&, xaml::RoutedEventArgs const&) {
    Activate(catro::app::AppDestination::settings);
    get_self<winrt::Catro::implementation::SettingsView>(
        settings_page_.as<Catro::SettingsView>())->ShowProfile(false);
}

void MainWindow::OnProfile(IInspectable const&, xaml::RoutedEventArgs const&) {
    Activate(catro::app::AppDestination::settings);
    get_self<winrt::Catro::implementation::SettingsView>(
        settings_page_.as<Catro::SettingsView>())->ShowProfile(true);
}

xaml::UIElement MainWindow::PageFor(catro::app::AppDestination destination) {
    switch (destination) {
    case catro::app::AppDestination::server:
        if (!server_page_) {
            auto page = Catro::ServerView{};
            if (local_state_) {
                get_self<winrt::Catro::implementation::ServerView>(page)->SetLocalState(*local_state_);
            }
            server_page_ = page;
            get_self<winrt::Catro::implementation::ServerView>(page)->SetWindowActivity(window_activity_);
            ApplyDirectoryServerToPage();
        }
        return server_page_;
    case catro::app::AppDestination::diagnostics:
        if (!diagnostics_page_) {
            diagnostics_page_ = Catro::DiagnosticsView{};
        }
        return diagnostics_page_;
    case catro::app::AppDestination::settings:
        if (!settings_page_) {
            auto page = Catro::SettingsView{};
            if (local_state_) {
                get_self<winrt::Catro::implementation::SettingsView>(page)->SetProfile(
                    *local_state_, [weak = get_weak()](catro::community::LocalState const& state) {
                        if (const auto self = weak.get()) {
                            self->local_state_ = state;
                            if (self->server_page_) {
                                get_self<winrt::Catro::implementation::ServerView>(
                                    self->server_page_.as<Catro::ServerView>())->SetLocalState(state);
                            }
                        }
                    });
            }
            settings_page_ = page;
        }
        return settings_page_;
    }
    return nullptr;
}

void MainWindow::Activate(catro::app::AppDestination destination) {
    const auto previous = shell_state_.destination();
    switch (destination) {
    case catro::app::AppDestination::server:
        (void)shell_state_.open_server();
        if (active_directory_server_) {
            TitleContext().Text(
                to_hstring(
                    active_directory_server_->name));
        } else {
            TitleContext().Text(
                local_state_
                    ? to_hstring(
                          local_state_->
                              personal_server.name)
                    : hstring{L"State unavailable"});
        }
        break;
    case catro::app::AppDestination::diagnostics:
        (void)shell_state_.open_diagnostics();
        TitleContext().Text(L"System");
        break;
    case catro::app::AppDestination::settings:
        (void)shell_state_.open_settings();
        TitleContext().Text(L"Settings");
        break;
    }

    WorkspaceHost().Content(PageFor(destination));

    // Diagnostics can materialize hundreds of evidence rows. Release it when leaving so the normal
    // chat/voice shell remains close to a static tree while a game is running.
    if (previous == catro::app::AppDestination::diagnostics &&
        destination != catro::app::AppDestination::diagnostics) {
        diagnostics_page_ = nullptr;
    }

    UpdateRail();
}

void MainWindow::UpdateWindowActivity() {
    if (window_closed_) {
        return;
    }
    const auto hwnd = Microsoft::UI::GetWindowFromWindowId(AppWindow().Id());
    window_activity_ = !Visible() || (hwnd && IsIconic(hwnd))
        ? catro::shell::WindowActivity::hidden
        : window_active_ ? catro::shell::WindowActivity::foreground
                         : catro::shell::WindowActivity::background;
    ActivityLabel().Text(window_activity_ == catro::shell::WindowActivity::foreground
                            ? L"AUTO · FOCUSED" : L"AUTO · ECO");
    const auto policy = catro::shell::ui_refresh_policy(window_activity_, true, false);
    if (join_request_timer_) {
        if (policy.roster.count() == 0 || !directory_service_ || directory_access_token_.empty()) {
            join_request_timer_.Stop();
        } else {
            if (join_request_timer_.Interval() != policy.roster) {
                join_request_timer_.Interval(policy.roster);
            }
            if (!join_request_timer_.IsRunning()) {
                join_request_timer_.Start();
                BeginOutgoingJoinRequestRefresh();
            }
        }
    }
    if (server_page_) {
        get_self<winrt::Catro::implementation::ServerView>(
            server_page_.as<Catro::ServerView>())->SetWindowActivity(window_activity_);
    }
}

void MainWindow::OnContinueOffline(IInspectable const&, xaml::RoutedEventArgs const&) {
    // Sign-in keeps retrying behind the status bar.
    DismissStartupSplash();
}

void MainWindow::DismissStartupSplash() {
    if (!startup_splash_) {
        return;
    }
    startup_splash_ = false;
    StartupRing().IsActive(false);
    StartupSplash().IsHitTestVisible(false);
    StartupSplash().Opacity(0.0);
    startup_timer_.Stop();
    startup_timer_.Interval(std::chrono::milliseconds{300});
    startup_timer_.Start();
}

void MainWindow::UpdateConnectionUi() {
    if (startup_splash_) {
        // Stays up while connecting, retries included; any settled state ends it.
        if (workspace_state_.connection == catro::app::ConnectionState::connecting) {
            StartupStatusText().Text(to_hstring(workspace_state_.connection_message));
        } else {
            DismissStartupSplash();
        }
    }
    const auto& join = workspace_state_.join_server;
    const bool show_action =
        (join.availability == catro::app::Availability::busy ||
         join.availability == catro::app::Availability::failed) &&
        !join.reason.empty();
    ShellStatusText().Text(
        to_hstring(
            show_action
                ? workspace_state_.join_server.reason
                : workspace_state_.connection_message));
    JoinServerButton().IsEnabled(
        join.availability != catro::app::Availability::busy);
    ShellStatusBar().Visibility(
        workspace_state_.connection ==
                    catro::app::ConnectionState::synchronized &&
                !show_action
            ? xaml::Visibility::Collapsed
            : xaml::Visibility::Visible);
}

// The caption buttons are drawn by the system, so they follow the ivory or espresso palette
// explicitly whenever the effective theme changes.
void MainWindow::UpdateCaptionButtons() {
    const bool dark =
        ShellRoot().ActualTheme() == xaml::ElementTheme::Dark;
    const auto foreground = dark
        ? Windows::UI::Color{255, 0xF4, 0xEE, 0xE6}
        : Windows::UI::Color{255, 0x2A, 0x24, 0x1D};
    const auto hover = dark
        ? Windows::UI::Color{255, 0x2A, 0x25, 0x1F}
        : Windows::UI::Color{255, 0xED, 0xE5, 0xD6};
    const auto inactive = dark
        ? Windows::UI::Color{255, 0x9A, 0x8F, 0x83}
        : Windows::UI::Color{255, 0x6F, 0x65, 0x59};
    auto title_bar = AppWindow().TitleBar();
    title_bar.ButtonForegroundColor(foreground);
    title_bar.ButtonHoverForegroundColor(foreground);
    title_bar.ButtonHoverBackgroundColor(hover);
    title_bar.ButtonPressedForegroundColor(foreground);
    title_bar.ButtonPressedBackgroundColor(hover);
    title_bar.ButtonInactiveForegroundColor(inactive);
}

void MainWindow::UpdateRail() {
    const auto current = shell_state_.destination();
    bool personal_selected =
        current ==
            catro::app::AppDestination::server;
    if (personal_selected &&
        local_state_ &&
        active_directory_server_) {
        personal_selected =
            active_directory_server_->id ==
            catro::community::to_hex(
                local_state_->
                    personal_server.id);
    }
    ServerSelection().Visibility(
        personal_selected
            ? xaml::Visibility::Visible
            : xaml::Visibility::Collapsed);
    DiagnosticsSelection().Visibility(current == catro::app::AppDestination::diagnostics
                                          ? xaml::Visibility::Visible
                                          : xaml::Visibility::Collapsed);
    SettingsSelection().Visibility(current == catro::app::AppDestination::settings
                                       ? xaml::Visibility::Visible
                                       : xaml::Visibility::Collapsed);
}

} // namespace winrt::Catro::implementation

#pragma once

#include "MainWindow.g.h"

#include <PresentationState.hpp>
#include <ShellModel.hpp>
#include "UiActivityPolicy.hpp"

#include <catro/community/model.hpp>
#include <catro/platform/windows/directory_client.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace winrt::Catro::implementation {

// What one successful directory handshake yields.
struct DirectoryLink {
    std::string access_token;
    catro::platform::windows::DirectoryServer personal;
    std::vector<catro::platform::windows::DirectoryServer> servers;
};

struct MainWindow : MainWindowT<MainWindow> {
    MainWindow() = default;
    ~MainWindow();

    void InitializeComponent();
    void OnServer(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnJoinServer(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnDiagnostics(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnSettings(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnProfile(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnContinueOffline(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

private:
    Microsoft::UI::Xaml::UIElement PageFor(catro::app::AppDestination destination);
    void Activate(catro::app::AppDestination destination);
    void ActivateDirectoryServer(std::string_view server_id);
    winrt::fire_and_forget BeginDirectoryBootstrap();
    void ApplyDirectoryLink(
        catro::platform::windows::DirectoryServiceConfig service,
        DirectoryLink link);
    winrt::fire_and_forget BeginJoinServer();
    winrt::fire_and_forget BeginServerCodeLookup(std::string server_code);
    winrt::fire_and_forget BeginInviteJoin(std::string invite_code);
    winrt::fire_and_forget BeginOutgoingJoinRequestRefresh();
    winrt::fire_and_forget BeginDirectoryServerRefresh();
    void RefreshDirectoryRail();
    void ApplyDirectoryServerToPage();
    void UpdateRail();
    void UpdateConnectionUi();
    void DismissStartupSplash();
    void UpdateCaptionButtons();
    void UpdateWindowActivity();

    catro::app::ShellState shell_state_;
    catro::app::WorkspaceSnapshot workspace_state_ =
        catro::app::WorkspaceSnapshot::connecting();
    Microsoft::UI::Xaml::UIElement server_page_{nullptr};
    Microsoft::UI::Xaml::UIElement diagnostics_page_{nullptr};
    Microsoft::UI::Xaml::UIElement settings_page_{nullptr};
    std::optional<catro::community::LocalState> local_state_;

    std::optional<
        catro::platform::windows::DirectoryServiceConfig>
        directory_service_;
    std::string directory_access_token_;
    std::vector<
        catro::platform::windows::DirectoryServer>
        directory_servers_;
    std::optional<
        catro::platform::windows::DirectoryServer>
        active_directory_server_;

    Microsoft::UI::Dispatching::DispatcherQueueTimer join_request_timer_{nullptr};
    // Offers "Continue offline" on a slow start, then collapses the startup screen after its fade.
    Microsoft::UI::Dispatching::DispatcherQueueTimer startup_timer_{nullptr};
    bool startup_splash_ = true;
    std::unordered_set<std::string> observed_join_request_ids_;
    std::unordered_set<std::string> approved_join_requests_waiting_refresh_;
    bool join_request_refresh_pending_ = false;
    bool directory_server_refresh_pending_ = false;
    bool window_active_ = true;
    // A closed WinUI window throws from Visible()/AppWindow(); late events must not query it.
    bool window_closed_ = false;
    catro::shell::WindowActivity window_activity_ = catro::shell::WindowActivity::foreground;
};

} // namespace winrt::Catro::implementation

namespace winrt::Catro::factory_implementation {

struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};

} // namespace winrt::Catro::factory_implementation

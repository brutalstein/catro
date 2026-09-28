#pragma once

#include "MainWindow.g.h"

#include <ShellModel.hpp>

#include <catro/community/model.hpp>
#include <catro/platform/windows/directory_client.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace winrt::Catro::implementation {

struct MainWindow : MainWindowT<MainWindow> {
    MainWindow() = default;

    void InitializeComponent();
    void OnServer(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnJoinServer(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnDiagnostics(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnSettings(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

private:
    Microsoft::UI::Xaml::UIElement PageFor(catro::app::AppDestination destination);
    void Activate(catro::app::AppDestination destination);
    void ActivateDirectoryServer(std::string_view server_id);
    winrt::fire_and_forget BeginDirectoryBootstrap();
    winrt::fire_and_forget BeginJoinServer();
    void RefreshDirectoryRail();
    void ApplyDirectoryServerToPage();
    void UpdateRail();

    catro::app::ShellState shell_state_;
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
};

} // namespace winrt::Catro::implementation

namespace winrt::Catro::factory_implementation {

struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};

} // namespace winrt::Catro::factory_implementation

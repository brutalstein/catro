#include "pch.h"

#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "Diagnostics/DiagnosticsView.xaml.h"
#include "Server/ServerView.xaml.h"
#include "Settings/SettingsView.xaml.h"

#include <winrt/Windows.UI.h>

namespace winrt::Catro::implementation {
namespace {

namespace xaml = Microsoft::UI::Xaml;

} // namespace

void MainWindow::InitializeComponent() {
    MainWindowT<MainWindow>::InitializeComponent();

    ExtendsContentIntoTitleBar(true);
    SetTitleBar(AppTitleBar());

    const auto transparent = Windows::UI::Color{0, 0, 0, 0};
    auto title_bar = AppWindow().TitleBar();
    title_bar.ButtonBackgroundColor(transparent);
    title_bar.ButtonInactiveBackgroundColor(transparent);

    const auto hwnd = Microsoft::UI::GetWindowFromWindowId(AppWindow().Id());
    const auto scale = GetDpiForWindow(hwnd) / 96.0;
    AppWindow().Resize({static_cast<int32_t>(1280 * scale), static_cast<int32_t>(820 * scale)});

    Activate(catro::app::AppDestination::server);
}

void MainWindow::OnServer(IInspectable const&, xaml::RoutedEventArgs const&) {
    Activate(catro::app::AppDestination::server);
}

void MainWindow::OnDiagnostics(IInspectable const&, xaml::RoutedEventArgs const&) {
    Activate(catro::app::AppDestination::diagnostics);
}

void MainWindow::OnSettings(IInspectable const&, xaml::RoutedEventArgs const&) {
    Activate(catro::app::AppDestination::settings);
}

xaml::UIElement MainWindow::PageFor(catro::app::AppDestination destination) {
    switch (destination) {
    case catro::app::AppDestination::server:
        if (!server_page_) {
            server_page_ = Catro::ServerView{};
        }
        return server_page_;
    case catro::app::AppDestination::diagnostics:
        if (!diagnostics_page_) {
            diagnostics_page_ = Catro::DiagnosticsView{};
        }
        return diagnostics_page_;
    case catro::app::AppDestination::settings:
        if (!settings_page_) {
            settings_page_ = Catro::SettingsView{};
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
        TitleContext().Text(L"My Server");
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

void MainWindow::UpdateRail() {
    const auto current = shell_state_.destination();
    ServerSelection().Visibility(current == catro::app::AppDestination::server
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

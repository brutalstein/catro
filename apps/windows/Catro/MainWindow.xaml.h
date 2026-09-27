#pragma once

#include "MainWindow.g.h"

#include <ShellModel.hpp>

namespace winrt::Catro::implementation {

struct MainWindow : MainWindowT<MainWindow> {
    MainWindow() = default;

    void InitializeComponent();
    void OnServer(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnDiagnostics(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnSettings(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

private:
    Microsoft::UI::Xaml::UIElement PageFor(catro::app::AppDestination destination);
    void Activate(catro::app::AppDestination destination);
    void UpdateRail();

    catro::app::ShellState shell_state_;
    Microsoft::UI::Xaml::UIElement server_page_{nullptr};
    Microsoft::UI::Xaml::UIElement diagnostics_page_{nullptr};
    Microsoft::UI::Xaml::UIElement settings_page_{nullptr};
};

} // namespace winrt::Catro::implementation

namespace winrt::Catro::factory_implementation {

struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};

} // namespace winrt::Catro::factory_implementation

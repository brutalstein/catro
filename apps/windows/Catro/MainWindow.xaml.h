#pragma once

#include "MainWindow.g.h"

#include <ShellModel.hpp>

namespace winrt::Catro::implementation {

struct MainWindow : MainWindowT<MainWindow> {
    MainWindow() = default;

    void InitializeComponent();
    void OnNavigationClick(IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&);

private:
    Microsoft::UI::Xaml::UIElement PageFor(catro::app::ShellSection section);
    void Activate(catro::app::ShellSection section);
    void UpdateNavigationVisuals(catro::app::ShellSection section);

    catro::app::ShellState shell_state_;
    Microsoft::UI::Xaml::UIElement home_page_{nullptr};
    Microsoft::UI::Xaml::UIElement voice_page_{nullptr};
    Microsoft::UI::Xaml::UIElement share_page_{nullptr};
    Microsoft::UI::Xaml::UIElement diagnostics_page_{nullptr};
    Microsoft::UI::Xaml::UIElement settings_page_{nullptr};
};

} // namespace winrt::Catro::implementation

namespace winrt::Catro::factory_implementation {

struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};

} // namespace winrt::Catro::factory_implementation

#include "pch.h"

#include "Settings/SettingsView.xaml.h"
#include "Settings/Appearance.hpp"
#if __has_include("SettingsView.g.cpp")
#include "SettingsView.g.cpp"
#endif

namespace winrt::Catro::implementation {

// Items are ordered like catro::shell::Appearance: Ivory, Dark, System.
void SettingsView::InitializeComponent() {
    SettingsViewT<SettingsView>::InitializeComponent();
    AppearanceBox().SelectedIndex(
        static_cast<int32_t>(catro::shell::load_appearance()));
}

void SettingsView::OnAppearanceChanged(
    IInspectable const&,
    Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&) {
    const auto root = XamlRoot();
    const auto index = AppearanceBox().SelectedIndex();
    if (!root || index < 0) {
        return; // Initial selection while the page is not yet in the window.
    }
    const auto appearance = static_cast<catro::shell::Appearance>(index);
    catro::shell::save_appearance(appearance);
    if (const auto shell = root.Content().try_as<Microsoft::UI::Xaml::FrameworkElement>()) {
        shell.RequestedTheme(catro::shell::element_theme(appearance));
    }
}

} // namespace winrt::Catro::implementation

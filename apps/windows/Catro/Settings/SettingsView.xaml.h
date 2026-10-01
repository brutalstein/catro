#pragma once

#include "SettingsView.g.h"

namespace winrt::Catro::implementation {

struct SettingsView : SettingsViewT<SettingsView> {
    SettingsView() = default;
    void InitializeComponent();
    void OnAppearanceChanged(
        IInspectable const&,
        Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
};

} // namespace winrt::Catro::implementation

namespace winrt::Catro::factory_implementation {
struct SettingsView : SettingsViewT<SettingsView, implementation::SettingsView> {};
} // namespace winrt::Catro::factory_implementation

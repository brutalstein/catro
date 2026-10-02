#pragma once

#include "SettingsView.g.h"
#include <catro/community/model.hpp>
#include <functional>
#include <optional>

namespace winrt::Catro::implementation {

struct SettingsView : SettingsViewT<SettingsView> {
    SettingsView() = default;
    void InitializeComponent();
    void SetProfile(catro::community::LocalState const& state,
                    std::function<void(catro::community::LocalState const&)> changed);
    void ShowProfile(bool show);
    void OnProfileTab(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnAppearanceTab(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnSaveProfile(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnCopyIdentity(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnPreviewChanged(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnAppearanceChanged(
        IInspectable const&,
        Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);

private:
    winrt::fire_and_forget SaveProfile();
    std::optional<catro::community::LocalState> local_state_;
    std::function<void(catro::community::LocalState const&)> profile_changed_;
    bool saving_ = false;
};

} // namespace winrt::Catro::implementation

namespace winrt::Catro::factory_implementation {
struct SettingsView : SettingsViewT<SettingsView, implementation::SettingsView> {};
} // namespace winrt::Catro::factory_implementation

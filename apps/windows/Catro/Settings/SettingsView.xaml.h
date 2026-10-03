#pragma once

#include "SettingsView.g.h"
#include <catro/community/model.hpp>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace winrt::Catro::implementation {

struct SettingsView : SettingsViewT<SettingsView> {
    SettingsView() = default;
    void InitializeComponent();
    void SetProfile(catro::community::LocalState const& state,
                    std::function<void(catro::community::LocalState const&)> changed);
    void ShowProfile(bool show);
    void OnProfileTab(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnAppearanceTab(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnVoiceTab(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnInputModeChanged(IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void OnVoiceToggleChanged(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnSensitivityChanged(
        IInspectable const&, Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&);
    void OnRecordPushToTalk(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnDeviceChanged(IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void OnSaveProfile(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnCopyIdentity(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnPreviewChanged(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnAppearanceChanged(
        IInspectable const&,
        Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);

private:
    enum class Panel { profile, appearance, voice };
    void ShowPanel(Panel panel);
    void LoadVoicePreferences();
    void SaveVoicePreferences();
    void ShowPushToTalkKey();
    winrt::fire_and_forget SaveProfile();
    Microsoft::UI::Dispatching::DispatcherQueueTimer record_timer_{nullptr};
    bool loading_voice_ = false;
    // Endpoint ids behind the device pickers; index 0 is the Windows default (empty).
    std::vector<std::string> input_ids_;
    std::vector<std::string> output_ids_;
    std::optional<catro::community::LocalState> local_state_;
    std::function<void(catro::community::LocalState const&)> profile_changed_;
    bool saving_ = false;
};

} // namespace winrt::Catro::implementation

namespace winrt::Catro::factory_implementation {
struct SettingsView : SettingsViewT<SettingsView, implementation::SettingsView> {};
} // namespace winrt::Catro::factory_implementation

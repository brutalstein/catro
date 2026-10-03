#include "pch.h"

#include "Settings/SettingsView.xaml.h"
#include "Settings/Appearance.hpp"
#include "Settings/Performance.hpp"
#include "Settings/Voice.hpp"

#include <catro/platform/windows/audio_platform.hpp>

#include <chrono>
#if __has_include("SettingsView.g.cpp")
#include "SettingsView.g.cpp"
#endif

namespace winrt::Catro::implementation {

// Items are ordered like catro::shell::Appearance: Ivory, Dark, System.
void SettingsView::InitializeComponent() {
    SettingsViewT<SettingsView>::InitializeComponent();
    AppearanceBox().SelectedIndex(
        static_cast<int32_t>(catro::shell::load_appearance()));
    LocalPreviewToggle().IsOn(catro::shell::local_preview_preference());
    LoadVoicePreferences();
}

namespace {

std::wstring key_name(std::uint32_t key) {
    switch (key) {
    case 0:
        return L"Record keybind";
    case VK_MBUTTON:
        return L"Mouse 3";
    case VK_XBUTTON1:
        return L"Mouse 4";
    case VK_XBUTTON2:
        return L"Mouse 5";
    default:
        break;
    }
    wchar_t name[64]{};
    const auto scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
    if (scan != 0 && GetKeyNameTextW(static_cast<LONG>(scan << 16U), name, 64) > 0) {
        return name;
    }
    return L"Key " + std::to_wstring(key);
}

// Default first, then every active device; a saved device that is unplugged stays listed so the
// choice survives until it returns.
void fill_devices(Microsoft::UI::Xaml::Controls::ComboBox const& box, std::vector<std::string>& ids,
                  catro::audio::DeviceDirection direction, std::string const& selected) {
    ids.assign(1, std::string{});
    box.Items().Clear();
    box.Items().Append(box_value(hstring{L"Default"}));
    int32_t index = 0;
    for (auto& device : catro::platform::windows::list_audio_devices(direction)) {
        if (device.id == selected) {
            index = static_cast<int32_t>(ids.size());
        }
        box.Items().Append(box_value(to_hstring(device.name)));
        ids.push_back(std::move(device.id));
    }
    if (!selected.empty() && index == 0) {
        index = static_cast<int32_t>(ids.size());
        box.Items().Append(box_value(hstring{L"Disconnected device"}));
        ids.push_back(selected);
    }
    box.SelectedIndex(index);
}

std::string chosen(Microsoft::UI::Xaml::Controls::ComboBox const& box, std::vector<std::string> const& ids) {
    const auto index = box.SelectedIndex();
    return index > 0 && static_cast<std::size_t>(index) < ids.size() ? ids[static_cast<std::size_t>(index)]
                                                                      : std::string{};
}

} // namespace

void SettingsView::LoadVoicePreferences() {
    loading_voice_ = true;
    const auto& preferences = catro::shell::voice_preferences();
    InputModeBox().SelectedIndex(preferences.push_to_talk ? 1 : 0);
    AutoSensitivityToggle().IsOn(preferences.automatic_sensitivity);
    SensitivitySlider().Value(preferences.sensitivity_db);
    SensitivitySlider().IsEnabled(!preferences.automatic_sensitivity);
    EchoToggle().IsOn(preferences.echo_cancellation);
    NoiseToggle().IsOn(preferences.noise_suppression);
    GainToggle().IsOn(preferences.automatic_gain);
    SoundsToggle().IsOn(preferences.sounds);
    fill_devices(InputDeviceBox(), input_ids_, catro::audio::DeviceDirection::capture, preferences.input_device);
    fill_devices(OutputDeviceBox(), output_ids_, catro::audio::DeviceDirection::render, preferences.output_device);
    PushToTalkRow().Visibility(preferences.push_to_talk ? Microsoft::UI::Xaml::Visibility::Visible
                                                        : Microsoft::UI::Xaml::Visibility::Collapsed);
    ShowPushToTalkKey();
    loading_voice_ = false;
}

void SettingsView::ShowPushToTalkKey() {
    PushToTalkKeyButton().Content(box_value(hstring{key_name(catro::shell::voice_preferences().push_to_talk_key)}));
}

void SettingsView::SaveVoicePreferences() {
    if (loading_voice_) {
        return;
    }
    auto preferences = catro::shell::voice_preferences();
    preferences.push_to_talk = InputModeBox().SelectedIndex() == 1;
    preferences.automatic_sensitivity = AutoSensitivityToggle().IsOn();
    preferences.sensitivity_db = static_cast<float>(SensitivitySlider().Value());
    preferences.echo_cancellation = EchoToggle().IsOn();
    preferences.noise_suppression = NoiseToggle().IsOn();
    preferences.automatic_gain = GainToggle().IsOn();
    preferences.sounds = SoundsToggle().IsOn();
    preferences.input_device = chosen(InputDeviceBox(), input_ids_);
    preferences.output_device = chosen(OutputDeviceBox(), output_ids_);
    catro::shell::save_voice_preferences(preferences);
    SensitivitySlider().IsEnabled(!preferences.automatic_sensitivity);
    PushToTalkRow().Visibility(preferences.push_to_talk ? Microsoft::UI::Xaml::Visibility::Visible
                                                        : Microsoft::UI::Xaml::Visibility::Collapsed);
}

void SettingsView::OnInputModeChanged(IInspectable const&,
                                      Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&) {
    SaveVoicePreferences();
}

void SettingsView::OnVoiceToggleChanged(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) {
    SaveVoicePreferences();
}

void SettingsView::OnDeviceChanged(IInspectable const&,
                                   Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&) {
    SaveVoicePreferences();
}

void SettingsView::OnSensitivityChanged(
    IInspectable const&, Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&) {
    SaveVoicePreferences();
}

// Polls every key and mouse button instead of listening to this page, so side buttons and keys
// pressed while another window has focus are captured the same way push-to-talk reads them.
void SettingsView::OnRecordPushToTalk(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) {
    using namespace std::chrono_literals;
    if (!record_timer_) {
        record_timer_ = DispatcherQueue().CreateTimer();
        record_timer_.Interval(15ms);
        record_timer_.Tick([this](auto&&, auto&&) {
            for (std::uint32_t key = VK_RBUTTON; key <= 0xFEU; ++key) {
                if ((GetAsyncKeyState(static_cast<int>(key)) & 0x8000) == 0) {
                    continue;
                }
                record_timer_.Stop();
                if (key != VK_ESCAPE) {
                    auto preferences = catro::shell::voice_preferences();
                    preferences.push_to_talk_key = key;
                    catro::shell::save_voice_preferences(preferences);
                }
                ShowPushToTalkKey();
                return;
            }
        });
    }
    PushToTalkKeyButton().Content(box_value(L"Press a key…"));
    record_timer_.Start();
}

void SettingsView::OnPreviewChanged(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) {
    if (!XamlRoot()) {
        return;
    }
    const bool enabled = LocalPreviewToggle().IsOn();
    if (!catro::shell::save_local_preview(enabled)) {
        LocalPreviewToggle().IsOn(catro::shell::local_preview_preference());
    }
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

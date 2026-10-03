#include "pch.h"
#include "Settings/SettingsView.xaml.h"
#include "Settings/Profile.hpp"

#include <catro/platform/windows/directory_client.hpp>
#include <catro/platform/windows/local_state.hpp>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>

namespace winrt::Catro::implementation {

namespace xaml = Microsoft::UI::Xaml;
namespace platform = catro::platform::windows;

void SettingsView::SetProfile(
    catro::community::LocalState const& state,
    std::function<void(catro::community::LocalState const&)> changed) {
    local_state_ = state;
    profile_changed_ = std::move(changed);
    UsernameBox().Text(to_hstring(state.identity.display_name));
    IdentityBox().Text(to_hstring(catro::community::to_hex(state.identity.id)));
}

void SettingsView::ShowProfile(bool show) {
    ShowPanel(show ? Panel::profile : Panel::appearance);
}

void SettingsView::ShowPanel(Panel panel) {
    const auto visible = [](bool shown) { return shown ? xaml::Visibility::Visible : xaml::Visibility::Collapsed; };
    ProfilePanel().Visibility(visible(panel == Panel::profile));
    AppearancePanel().Visibility(visible(panel == Panel::appearance));
    VoicePanel().Visibility(visible(panel == Panel::voice));
    ProfileTab().IsChecked(panel == Panel::profile);
    AppearanceTab().IsChecked(panel == Panel::appearance);
    VoiceTab().IsChecked(panel == Panel::voice);
}

void SettingsView::OnProfileTab(IInspectable const&, xaml::RoutedEventArgs const&) {
    ShowPanel(Panel::profile);
}

void SettingsView::OnAppearanceTab(IInspectable const&, xaml::RoutedEventArgs const&) {
    ShowPanel(Panel::appearance);
}

void SettingsView::OnVoiceTab(IInspectable const&, xaml::RoutedEventArgs const&) {
    ShowPanel(Panel::voice);
}

void SettingsView::OnSaveProfile(IInspectable const&, xaml::RoutedEventArgs const&) {
    SaveProfile();
}

void SettingsView::OnCopyIdentity(IInspectable const&, xaml::RoutedEventArgs const&) {
    try {
        Windows::ApplicationModel::DataTransfer::DataPackage content;
        content.SetText(IdentityBox().Text());
        Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(content);
        ProfileStatus().Text(L"User ID copied. This ID is not an invite code.");
    } catch (winrt::hresult_error const&) {
        ProfileStatus().Text(L"Clipboard unavailable. Select and copy your ID manually.");
    }
}

winrt::fire_and_forget SettingsView::SaveProfile() {
    auto lifetime = get_strong();
    if (saving_ || !local_state_) {
        co_return;
    }
    const auto name = catro::shell::profile_name(to_string(UsernameBox().Text()));
    if (!name) {
        ProfileStatus().Text(L"Enter a name without line breaks, up to 64 UTF-8 bytes.");
        co_return;
    }
    auto updated = *local_state_;
    updated.identity.display_name = *name;
    saving_ = true;
    SaveProfileButton().IsEnabled(false);
    UsernameBox().IsEnabled(false);
    ProfileStatus().Text(L"Saving your profile…");
    UiThread ui_thread;
    co_await winrt::resume_background();

    bool saved = false;
    std::string status;
    try {
        const auto path = platform::default_local_state_path();
        if (const auto* file = std::get_if<std::filesystem::path>(&path)) {
            if (const auto failure = platform::save_local_state_atomic(*file, updated)) {
                status = "Could not save your profile. Your previous profile is unchanged.";
            } else {
                saved = true;
                status = "Saved on this device. Your name will sync when you reconnect.";
                const auto service = platform::load_directory_service_config();
                const auto credential = platform::load_or_create_directory_credential();
                if (const auto* config = std::get_if<platform::DirectoryServiceConfig>(&service)) {
                    if (const auto* secret = std::get_if<std::string>(&credential)) {
                        const auto result = platform::register_directory_identity(
                            *config, updated.identity, *secret);
                        status = std::holds_alternative<std::string>(result)
                            ? "Saved and synced. Your ID and memberships have not changed."
                            : "Saved locally, but online sync failed. Press Save to retry.";
                    }
                }
            }
        } else {
            status = "Your profile location is unavailable. Nothing was changed.";
        }
    } catch (std::exception const&) {
        status = saved ? "Saved locally. Online sync failed; press Save to retry."
                       : "Could not save your profile. Please try again.";
    }
    co_await ui_thread;
    lifetime->saving_ = false;
    lifetime->SaveProfileButton().IsEnabled(true);
    lifetime->UsernameBox().IsEnabled(true);
    if (saved) {
        lifetime->local_state_ = updated;
        lifetime->UsernameBox().Text(to_hstring(updated.identity.display_name));
        if (lifetime->profile_changed_) {
            lifetime->profile_changed_(updated);
        }
    }
    lifetime->ProfileStatus().Text(to_hstring(status));
}

} // namespace winrt::Catro::implementation

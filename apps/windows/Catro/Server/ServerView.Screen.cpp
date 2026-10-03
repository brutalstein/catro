#include "pch.h"

#include "Server/ServerView.xaml.h"
#include "Server/ServerView.RuntimeConfig.hpp"
#include "Settings/Performance.hpp"

#include <catro/platform/windows/screen_capture.hpp>
#include <catro/screen_runtime.hpp>
#include <catro/video/geometry.hpp>

#include <microsoft.ui.xaml.media.dxinterop.h>

#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Security.Authorization.AppCapabilityAccess.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Text.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace winrt::Catro::implementation {

void ServerView::OnLocalPreviewChanged(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) {
    const bool enabled = LocalPreviewToggle().IsOn();
    if (enabled == catro::shell::local_preview_preference()) {
        return;
    }
    if (!catro::shell::save_local_preview(enabled)) {
        LocalPreviewToggle().IsOn(catro::shell::local_preview_preference());
        VoiceStateText().Text(L"Could not save the preview preference");
        return;
    }
    ApplyActivityPolicy();
    UpdateScreenShareUi();
}

namespace {

namespace xaml = Microsoft::UI::Xaml;
namespace controls = Microsoft::UI::Xaml::Controls;
using namespace std::chrono_literals;
using server_view_detail::direct_video_config;
using server_view_detail::direct_voice_config;

struct ViewportSize {
    double width = 0.0;
    double height = 0.0;
};

[[nodiscard]] ViewportSize fit_viewport(
    std::uint32_t source_width,
    std::uint32_t source_height,
    double max_width,
    double max_height) noexcept {
    if (source_width == 0 || source_height == 0 ||
        !std::isfinite(max_width) || !std::isfinite(max_height) ||
        max_width <= 0.0 || max_height <= 0.0) {
        return {};
    }
    const auto scale = std::min(
        max_width / static_cast<double>(source_width),
        max_height / static_cast<double>(source_height));
    if (!std::isfinite(scale) || scale <= 0.0) {
        return {};
    }
    return {
        std::max(2.0, static_cast<double>(source_width) * scale),
        std::max(2.0, static_cast<double>(source_height) * scale),
    };
}

std::wstring capture_source_label(
    const catro::platform::windows::CaptureSource& source) {
    std::wstring label =
        source.game ? L"Game — "
        : source.kind == catro::platform::windows::CaptureSourceKind::display
            ? L"Display — "
            : L"Window — ";
    if (source.fullscreen_like && !source.game &&
        source.kind == catro::platform::windows::CaptureSourceKind::window) {
        label += L"Fullscreen/game — ";
    }
    label += to_hstring(source.title).c_str();
    if (!source.process_name.empty()) {
        label += L"  ·  ";
        label += to_hstring(source.process_name).c_str();
    }
    label += L"  ·  ";
    label += std::to_wstring(source.width);
    label += L"×";
    label += std::to_wstring(source.height);
    return label;
}

bool chromium_window(
    const catro::platform::windows::CaptureSource& source) noexcept {
    if (source.kind != catro::platform::windows::CaptureSourceKind::window) {
        return false;
    }
    return source.process_name == "brave.exe" ||
           source.process_name == "chrome.exe" ||
           source.process_name == "msedge.exe" ||
           source.process_name == "chromium.exe" ||
           source.process_name == "opera.exe" ||
           source.process_name == "vivaldi.exe";
}

} // namespace

void ServerView::OnShareScreen(IInspectable const&, xaml::RoutedEventArgs const&) {
    if (!screen_runtime_ || share_dialog_open_) {
        return;
    }

    const auto snapshot = screen_runtime_->snapshot();
    if (snapshot.state == catro::screen::ScreenShareState::starting ||
        snapshot.state == catro::screen::ScreenShareState::sharing) {
        StopScreenShare();
        UpdateVoiceUi();
        UpdateScreenShareUi();
        return;
    }

    BeginScreenShare();
}

void ServerView::OnWatchStream(
    IInspectable const&, xaml::RoutedEventArgs const&) {
    if (!screen_runtime_) {
        return;
    }
    screen_runtime_->set_remote_viewing_enabled(true);
    if (screen_timer_) {
        ApplyActivityPolicy();
    }
    UpdateScreenShareUi();
}

void ServerView::OnLeaveStream(
    IInspectable const&, xaml::RoutedEventArgs const&) {
    if (!screen_runtime_) {
        return;
    }
    screen_runtime_->set_remote_viewing_enabled(false);
    CloseStreamWindow();
    DetachRemoteSwapChain();
    UpdateScreenShareUi();
}

void ServerView::OnPopOutStream(
    IInspectable const&, xaml::RoutedEventArgs const&) {
    OpenStreamWindow(false);
}

void ServerView::OnFullScreenStream(
    IInspectable const&, xaml::RoutedEventArgs const&) {
    OpenStreamWindow(true);
}

void ServerView::OnSizeChanged(IInspectable const&, xaml::SizeChangedEventArgs const& args) {
    const auto width = args.NewSize().Width;
    const bool show_members = width >= 920.0;
    MembersColumn().Width(xaml::GridLengthHelper::FromPixels(show_members ? 216.0 : 0.0));
    MembersPane().Visibility(show_members ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    ChannelsColumn().Width(xaml::GridLengthHelper::FromPixels(width >= 760.0 ? 240.0 : 196.0));
    // The same action remains available in the voice control bar on compact windows.
    ShareScreenButton().Visibility(width >= 760.0 ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    UpdateScreenShareUi();
}

winrt::Windows::Foundation::IAsyncOperation<bool>
ServerView::ClaimScreenOwnership() {
    auto lifetime = get_strong();
    if (!room_mode_active_) {
        co_return true;
    }
    if (room_runtime_ == nullptr ||
        room_peer_id_.empty()) {
        VoiceStateText().Text(
            L"Screen ownership request failed");
        co_return false;
    }

    const auto peer_id = room_peer_id_;
    auto room =
        catro_room_runtime_snapshot(
            room_runtime_);
    if (room.screen_owner[0] != '\0' &&
        std::string_view{room.screen_owner} !=
            peer_id) {
        VoiceStateText().Text(
            L"Another participant is sharing");
        co_return false;
    }

    if (catro_room_runtime_claim_screen(
            room_runtime_) != 0) {
        VoiceStateText().Text(
            L"Screen ownership request failed");
        co_return false;
    }
    if (std::string_view{room.screen_owner} ==
        peer_id) {
        co_return true;
    }

    UiThread ui_thread;
    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::seconds{3};

    while (std::chrono::steady_clock::now() <
           deadline) {
        co_await winrt::resume_after(
            std::chrono::milliseconds{50});
        room = catro_room_runtime_snapshot(
            room_runtime_);

        if (std::string_view{room.screen_owner} ==
            peer_id) {
            co_await ui_thread;
            co_return true;
        }
        if (room.screen_owner[0] != '\0') {
            co_await ui_thread;
            if (room_mode_active_ &&
                room_peer_id_ == peer_id) {
                VoiceStateText().Text(
                    L"Another participant is sharing");
            }
            co_return false;
        }
    }

    co_await ui_thread;
    if (room_mode_active_ &&
        room_peer_id_ == peer_id) {
        VoiceStateText().Text(
            L"Screen ownership request timed out");
    }
    co_return false;
}

winrt::fire_and_forget ServerView::BeginScreenShare() {
    [[maybe_unused]] auto lifetime = get_strong();
    if (workspace_state_.share_screen.availability ==
        catro::app::Availability::busy) {
        co_return;
    }
    workspace_state_.share_screen.enable();
    (void)workspace_state_.share_screen.begin(
        "Loading shareable sources…");
    share_dialog_open_ = true;
    bool ownership_claimed = false;
    UpdateVoiceUi();

    try {
        UiThread ui_thread;
        std::vector<
            catro::platform::windows::CaptureSource> sources;
        std::exception_ptr enumeration_failure;
        co_await winrt::resume_background();
        try {
            sources =
                catro::platform::windows::enumerate_capture_sources();
        } catch (...) {
            enumeration_failure =
                std::current_exception();
        }
        co_await ui_thread;
        if (enumeration_failure) {
            std::rethrow_exception(
                enumeration_failure);
        }
        if (sources.empty()) {
            share_dialog_open_ = false;
            workspace_state_.share_screen.fail(
                "No shareable windows or displays");
            UpdateVoiceUi();
            co_return;
        }

        controls::ContentDialog dialog;
        dialog.XamlRoot(XamlRoot());
        dialog.RequestedTheme(ActualTheme());
        dialog.Title(box_value(hstring{L"Share your screen"}));
        dialog.PrimaryButtonText(L"Go live");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(controls::ContentDialogButton::Primary);

        controls::StackPanel form;
        form.Spacing(10);

        controls::TextBlock source_label;
        source_label.Text(L"Source");
        form.Children().Append(source_label);

        controls::ComboBox source_box;
        source_box.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
        for (const auto& source : sources) {
            source_box.Items().Append(
                box_value(hstring{capture_source_label(source)}));
        }
        source_box.SelectedIndex(0);
        form.Children().Append(source_box);

        controls::TextBlock game_note;
        game_note.Text(
            L"Border-free display/game capture uses GPU-only DXGI Desktop Duplication for full "
            L"displays and Counter-Strike 2, including fullscreen mode changes, without injecting "
            L"code into the game process.");
        game_note.TextWrapping(xaml::TextWrapping::Wrap);
        game_note.Visibility(
            catro::platform::windows::recommended_capture_backend(sources.front()) ==
                    catro::platform::windows::ScreenCaptureBackend::desktop_duplication
                ? xaml::Visibility::Visible
                : xaml::Visibility::Collapsed);
        form.Children().Append(game_note);

        controls::TextBlock browser_note;
        browser_note.Text(
            L"Browser video compatibility: Brave/Chrome/Edge may stop rendering a hardware video "
            L"surface when the selected window is completely covered. If only the video region "
            L"turns black, restart the browser with native window occlusion disabled. "
            L"Protected/DRM video can remain unavailable to capture by design.");
        browser_note.TextWrapping(xaml::TextWrapping::Wrap);
        browser_note.Visibility(
            chromium_window(sources.front())
                ? xaml::Visibility::Visible
                : xaml::Visibility::Collapsed);
        form.Children().Append(browser_note);

        controls::ToggleSwitch share_audio_box;
        share_audio_box.Header(
            box_value(hstring{L"Share audio"}));
        share_audio_box.OnContent(
            box_value(hstring{L"On"}));
        share_audio_box.OffContent(
            box_value(hstring{L"Off"}));
        // Like Discord: an app or game share includes its sound; computer audio for a whole
        // display is opt-in and never includes Catro's own voices.
        const auto audio_for = [this](const catro::platform::windows::CaptureSource& source) {
            return room_mode_active_ &&
                   (source.kind == catro::platform::windows::CaptureSourceKind::display ||
                    source.process_id != 0);
        };
        const auto audio_default = [](const catro::platform::windows::CaptureSource& source) {
            return source.kind == catro::platform::windows::CaptureSourceKind::window;
        };
        share_audio_box.IsEnabled(audio_for(sources.front()));
        share_audio_box.IsOn(audio_for(sources.front()) && audio_default(sources.front()));
        form.Children().Append(share_audio_box);

        controls::TextBlock audio_note;
        audio_note.Text(
            room_mode_active_
                ? L"A window or game shares only its own sound. A display can share computer audio; voices in Catro are never included."
                : L"Stream audio is enabled for secure RTC rooms; local direct-peer engineering mode carries video only.");
        audio_note.TextWrapping(
            xaml::TextWrapping::Wrap);
        audio_note.FontSize(11);
        form.Children().Append(audio_note);

        controls::ToggleSwitch preview_box;
        preview_box.Header(box_value(hstring{L"Show my preview"}));
        preview_box.IsOn(catro::shell::local_preview_preference());
        form.Children().Append(preview_box);

        controls::TextBlock preview_note;
        preview_note.Text(L"Preview only changes what you see here. You can turn it on or off while sharing.");
        preview_note.TextWrapping(xaml::TextWrapping::Wrap);
        form.Children().Append(preview_note);

        source_box.SelectionChanged(
            [&sources,
             audio_for,
             audio_default,
             browser_note,
             game_note,
             share_audio_box](auto const& sender, auto const&) {
                const auto selected =
                    sender.as<controls::ComboBox>().SelectedIndex();
                const bool valid =
                    selected >= 0 &&
                    static_cast<std::size_t>(selected) <
                        sources.size();
                const auto* source =
                    valid
                        ? &sources[
                              static_cast<std::size_t>(
                                  selected)]
                        : nullptr;

                browser_note.Visibility(
                    source != nullptr &&
                            chromium_window(*source)
                        ? xaml::Visibility::Visible
                        : xaml::Visibility::Collapsed);
                const bool game_visible =
                    source != nullptr &&
                    catro::platform::windows::
                            recommended_capture_backend(
                                *source) ==
                        catro::platform::windows::
                            ScreenCaptureBackend::
                                desktop_duplication;
                game_note.Visibility(
                    game_visible
                        ? xaml::Visibility::Visible
                        : xaml::Visibility::Collapsed);

                const bool audio_available =
                    source != nullptr && audio_for(*source);
                share_audio_box.IsEnabled(
                    audio_available);
                share_audio_box.IsOn(
                    audio_available && audio_default(*source));
            });

        controls::ToggleSwitch borderless_box;
        borderless_box.Header(box_value(hstring{L"Hide Windows capture border"}));
        borderless_box.OnContent(box_value(hstring{L"On"}));
        borderless_box.OffContent(box_value(hstring{L"Off"}));
        borderless_box.IsOn(true);
        form.Children().Append(borderless_box);

        controls::TextBlock resolution_label;
        resolution_label.Text(L"Stream resolution ceiling");
        form.Children().Append(resolution_label);

        controls::ComboBox preset_box;
        preset_box.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
        preset_box.Items().Append(box_value(hstring{L"1080p"}));
        preset_box.Items().Append(box_value(hstring{L"900p"}));
        preset_box.Items().Append(box_value(hstring{L"720p"}));
        preset_box.Items().Append(box_value(hstring{L"Custom"}));
        preset_box.SelectedIndex(0);
        form.Children().Append(preset_box);

        controls::NumberBox width_box;
        width_box.Header(box_value(hstring{L"Maximum width"}));
        width_box.Minimum(320);
        width_box.Maximum(7680);
        width_box.SmallChange(2);
        width_box.Value(1920);
        form.Children().Append(width_box);

        controls::NumberBox height_box;
        height_box.Header(box_value(hstring{L"Maximum height"}));
        height_box.Minimum(180);
        height_box.Maximum(4320);
        height_box.SmallChange(2);
        height_box.Value(1080);
        form.Children().Append(height_box);

        preset_box.SelectionChanged(
            [width_box, height_box](auto const& sender, auto const&) {
                const auto selected =
                    sender.as<controls::ComboBox>().SelectedIndex();
                if (selected == 0) {
                    width_box.Value(1920);
                    height_box.Value(1080);
                } else if (selected == 1) {
                    width_box.Value(1600);
                    height_box.Value(900);
                } else if (selected == 2) {
                    width_box.Value(1280);
                    height_box.Value(720);
                }
            });

        controls::NumberBox fps_box;
        fps_box.Header(box_value(hstring{L"Frames per second"}));
        fps_box.Minimum(1);
        fps_box.Maximum(120);
        fps_box.SmallChange(1);
        // Games move fast; 60 FPS keeps motion smooth and the bitrate adapts to the network.
        fps_box.Value(sources.front().game ? 60 : 30);
        form.Children().Append(fps_box);
        source_box.SelectionChanged(
            [&sources, fps_box](auto const& sender, auto const&) {
                const auto selected = sender.as<controls::ComboBox>().SelectedIndex();
                if (selected >= 0 && static_cast<std::size_t>(selected) < sources.size() &&
                    sources[static_cast<std::size_t>(selected)].game) {
                    fps_box.Value(60);
                }
            });

        controls::NumberBox bitrate_box;
        bitrate_box.Header(box_value(hstring{L"Bitrate (Mbps)"}));
        bitrate_box.Minimum(0.128);
        bitrate_box.Maximum(50.0);
        bitrate_box.SmallChange(0.5);
        bitrate_box.Value(6.0);
        form.Children().Append(bitrate_box);

        controls::TextBlock note;
        note.Text(
            L"Catro preserves the selected source aspect ratio, never upscales it, and rounds only "
            L"to the even dimensions required by NV12/H.264. Your actual outgoing size is shown "
            L"under the preview after the stream starts.");
        note.TextWrapping(xaml::TextWrapping::Wrap);
        form.Children().Append(note);

        dialog.Content(form);
        const auto result = co_await dialog.ShowAsync();
        share_dialog_open_ = false;

        if (result != controls::ContentDialogResult::Primary) {
            workspace_state_.share_screen.enable();
            UpdateVoiceUi();
            co_return;
        }

        const auto selected = source_box.SelectedIndex();
        const auto width_value = width_box.Value();
        const auto height_value = height_box.Value();
        const auto fps_value = fps_box.Value();
        const auto bitrate_value = bitrate_box.Value();
        const bool request_borderless = borderless_box.IsOn();
        const bool request_audio =
            share_audio_box.IsEnabled() &&
            share_audio_box.IsOn();

        if (selected < 0 ||
            static_cast<std::size_t>(selected) >= sources.size() ||
            !std::isfinite(width_value) ||
            !std::isfinite(height_value) ||
            !std::isfinite(fps_value) ||
            !std::isfinite(bitrate_value)) {
            workspace_state_.share_screen.fail(
                "Invalid screen-share settings");
            UpdateVoiceUi();
            co_return;
        }

        const auto width =
            static_cast<std::uint32_t>(width_value);
        const auto height =
            static_cast<std::uint32_t>(height_value);
        const auto fps =
            static_cast<std::uint32_t>(fps_value);
        const auto bitrate = static_cast<std::uint32_t>(
            bitrate_value * 1'000'000.0 + 0.5);

        const auto fitted = catro::video::fit_even_video_extent(
            sources[static_cast<std::size_t>(selected)].width,
            sources[static_cast<std::size_t>(selected)].height,
            width,
            height);
        if (!fitted || fps == 0 || fps > 120 ||
            bitrate < 128'000 || bitrate > 50'000'000) {
            workspace_state_.share_screen.fail(
                "Invalid screen-share settings");
            UpdateVoiceUi();
            co_return;
        }

        const auto& selected_source =
            sources[static_cast<std::size_t>(selected)];
        if (!catro::shell::save_local_preview(preview_box.IsOn())) {
            workspace_state_.share_screen.fail("Could not save the preview preference");
            UpdateVoiceUi();
            co_return;
        }
        ApplyActivityPolicy();
        const auto capture_backend =
            catro::platform::windows::recommended_capture_backend(
                selected_source);

        bool borderless_allowed = false;
        if (request_borderless &&
            capture_backend ==
                catro::platform::windows::ScreenCaptureBackend::windows_graphics_capture) {
            try {
                namespace graphics = Windows::Graphics::Capture;
                namespace capability =
                    Windows::Security::Authorization::AppCapabilityAccess;
                const auto access = co_await
                    graphics::GraphicsCaptureAccess::RequestAccessAsync(
                        graphics::GraphicsCaptureAccessKind::Borderless);
                borderless_allowed =
                    access == capability::AppCapabilityAccessStatus::Allowed;
                if (!borderless_allowed) {
                    VoiceStateText().Text(
                        L"Windows kept the capture border; streaming continues");
                }
            } catch (const winrt::hresult_error&) {
                VoiceStateText().Text(
                    L"Borderless capture unavailable in this launch; streaming continues");
            }
        }

        const auto direct =
            direct_video_config();
        catro::screen::ScreenShareConfig config;
        config.source = selected_source;
        config.borderless = borderless_allowed;
        config.room_runtime =
            room_mode_active_
                ? room_runtime_
                : nullptr;
        if (!room_mode_active_) {
            config.bind = direct.bind;
            config.peer = direct.peer;
        }
        config.max_width = width;
        config.max_height = height;
        config.fps = fps;
        config.bitrate = bitrate;
        config.share_audio = request_audio;
        config.stream_audio_bitrate = 128'000;
        config.ssrc = LocalStreamId() ^ 0x56494430U;
        if (config.ssrc == 0) {
            config.ssrc = 1;
        }

        ownership_claimed =
            co_await ClaimScreenOwnership();
        if (!ownership_claimed) {
            workspace_state_.share_screen.fail(
                "Screen ownership request failed");
            UpdateScreenShareUi();
            UpdateVoiceUi();
            co_return;
        }

        if (const auto failure =
                screen_runtime_->start(config)) {
            if (room_mode_active_ &&
                room_runtime_ != nullptr) {
                catro_room_runtime_release_screen(
                    room_runtime_);
            }
            ownership_claimed = false;
            workspace_state_.share_screen.fail(
                failure->message);
            UpdateVoiceUi();
            co_return;
        }

        workspace_state_.share_screen.enable();
        SharePreviewHost().Visibility(xaml::Visibility::Visible);
        VoiceIdentityPanel().Visibility(xaml::Visibility::Collapsed);
        ApplyActivityPolicy();
        UpdateScreenShareUi();
        UpdateVoiceUi();
    } catch (const winrt::hresult_error& failure) {
        if (ownership_claimed &&
            room_mode_active_ &&
            room_runtime_ != nullptr) {
            catro_room_runtime_release_screen(
                room_runtime_);
        }
        share_dialog_open_ = false;
        std::wstring message = L"Screen share UI error: ";
        message += failure.message().c_str();
        workspace_state_.share_screen.fail(
            winrt::to_string(message));
        UpdateVoiceUi();
    } catch (...) {
        if (ownership_claimed &&
            room_mode_active_ &&
            room_runtime_ != nullptr) {
            catro_room_runtime_release_screen(
                room_runtime_);
        }
        share_dialog_open_ = false;
        workspace_state_.share_screen.fail(
            "Screen share UI error");
        UpdateVoiceUi();
    }
}

void ServerView::OpenStreamWindow(bool fullscreen) {
    if (!screen_runtime_ ||
        !screen_runtime_->snapshot().remote_viewing) {
        return;
    }

    if (stream_window_) {
        SetStreamWindowFullscreen(fullscreen);
        stream_window_.Activate();
        UpdateStreamWindowLayout();
        return;
    }

    try {
        namespace media = Microsoft::UI::Xaml::Media;

        DetachRemoteSwapChain();

        stream_window_ = xaml::Window{};
        stream_window_.Title(L"Catro — Live Stream");

        stream_window_root_ = controls::Grid{};
        stream_window_root_.Background(
            media::SolidColorBrush(
                Windows::UI::Color{255, 0, 0, 0}));

        stream_window_viewport_ = controls::Border{};
        stream_window_viewport_.Background(
            media::SolidColorBrush(
                Windows::UI::Color{255, 0, 0, 0}));
        stream_window_viewport_.HorizontalAlignment(
            xaml::HorizontalAlignment::Center);
        stream_window_viewport_.VerticalAlignment(
            xaml::VerticalAlignment::Center);

        stream_window_swap_chain_panel_ =
            controls::SwapChainPanel{};
        stream_window_swap_chain_panel_.HorizontalAlignment(
            xaml::HorizontalAlignment::Stretch);
        stream_window_swap_chain_panel_.VerticalAlignment(
            xaml::VerticalAlignment::Stretch);
        stream_window_viewport_.Child(
            stream_window_swap_chain_panel_);
        stream_window_root_.Children().Append(
            stream_window_viewport_);

        controls::StackPanel toolbar;
        toolbar.Orientation(
            controls::Orientation::Horizontal);
        toolbar.Spacing(8);
        toolbar.Margin(xaml::Thickness{14.0});
        toolbar.HorizontalAlignment(
            xaml::HorizontalAlignment::Right);
        toolbar.VerticalAlignment(
            xaml::VerticalAlignment::Top);

        controls::Border live_badge;
        live_badge.Padding(xaml::Thickness{9.0, 5.0, 9.0, 5.0});
        live_badge.CornerRadius(xaml::CornerRadius{7.0});
        live_badge.Background(
            media::SolidColorBrush(
                Windows::UI::Color{220, 35, 35, 35}));
        controls::TextBlock live_text;
        live_text.Text(L"LIVE");
        live_text.FontSize(10.0);
        live_text.FontWeight(
            Windows::UI::Text::FontWeights::SemiBold());
        live_badge.Child(live_text);
        toolbar.Children().Append(live_badge);

        stream_window_mode_button_ = controls::Button{};
        stream_window_mode_button_.Padding(
            xaml::Thickness{12.0, 6.0, 12.0, 6.0});
        stream_window_mode_button_.Click(
            [this](auto const&, auto const&) {
                SetStreamWindowFullscreen(
                    !stream_window_fullscreen_);
            });
        toolbar.Children().Append(
            stream_window_mode_button_);

        stream_window_topmost_button_ =
            controls::Button{};
        stream_window_topmost_button_.Content(
            box_value(hstring{L"Stay On Top"}));
        stream_window_topmost_button_.Padding(
            xaml::Thickness{12.0, 6.0, 12.0, 6.0});
        stream_window_topmost_button_.Click(
            [this](auto const&, auto const&) {
                SetStreamWindowAlwaysOnTop(
                    !stream_window_topmost_);
            });
        toolbar.Children().Append(
            stream_window_topmost_button_);

        controls::Button back_button;
        back_button.Content(box_value(hstring{L"Back to Catro"}));
        back_button.Padding(
            xaml::Thickness{12.0, 6.0, 12.0, 6.0});
        back_button.Click(
            [this](auto const&, auto const&) {
                CloseStreamWindow();
                UpdateScreenShareUi();
            });
        toolbar.Children().Append(back_button);

        controls::Button leave_button;
        leave_button.Content(box_value(hstring{L"Leave Stream"}));
        leave_button.Padding(
            xaml::Thickness{12.0, 6.0, 12.0, 6.0});
        leave_button.Click(
            [this](auto const&, auto const&) {
                if (screen_runtime_) {
                    screen_runtime_->set_remote_viewing_enabled(
                        false);
                }
                CloseStreamWindow();
                UpdateScreenShareUi();
            });
        toolbar.Children().Append(leave_button);

        stream_window_root_.Children().Append(toolbar);
        stream_window_root_.SizeChanged(
            [this](auto const&, auto const&) {
                UpdateStreamWindowLayout();
            });

        stream_window_.Closed(
            [this](auto const&, auto const&) {
                try {
                    if (stream_window_swap_chain_panel_) {
                        auto native =
                            stream_window_swap_chain_panel_
                                .as<ISwapChainPanelNative>();
                        (void)native->SetSwapChain(nullptr);
                    }
                } catch (...) {
                }
                stream_window_swap_chain_.Reset();
                stream_window_swap_chain_panel_ = nullptr;
                stream_window_viewport_ = nullptr;
                stream_window_root_ = nullptr;
                stream_window_mode_button_ = nullptr;
                stream_window_topmost_button_ = nullptr;
                stream_window_ = nullptr;
                stream_window_fullscreen_ = false;
                stream_window_topmost_ = false;
                UpdateScreenShareUi();
                ApplyActivityPolicy();
            });

        stream_window_.Content(stream_window_root_);
        stream_window_.Activate();

        if (!fullscreen) {
            const auto snapshot =
                screen_runtime_->snapshot();
            if (snapshot.remote_width != 0 &&
                snapshot.remote_height != 0) {
                const auto initial =
                    fit_viewport(
                        snapshot.remote_width,
                        snapshot.remote_height,
                        1280.0,
                        760.0);
                stream_window_.AppWindow().Resize(
                    {
                        static_cast<std::int32_t>(
                            std::max(
                                640.0,
                                initial.width)),
                        static_cast<std::int32_t>(
                            std::max(
                                420.0,
                                initial.height + 40.0)),
                    });
            } else {
                stream_window_.AppWindow().Resize(
                    {960, 640});
            }
        }

        SetStreamWindowFullscreen(fullscreen);
        UpdateStreamWindowLayout();
    } catch (...) {
        CloseStreamWindow();
    }
}

void ServerView::SetStreamWindowFullscreen(
    bool fullscreen) {
    if (!stream_window_) {
        return;
    }

    try {
        const auto kind =
            fullscreen
                ? Microsoft::UI::Windowing::
                      AppWindowPresenterKind::FullScreen
                : Microsoft::UI::Windowing::
                      AppWindowPresenterKind::Overlapped;
        stream_window_.AppWindow().SetPresenter(kind);
        stream_window_fullscreen_ = fullscreen;
        if (stream_window_mode_button_) {
            stream_window_mode_button_.Content(
                box_value(
                    fullscreen
                        ? hstring{L"Exit Full Screen"}
                        : hstring{L"Full Screen"}));
        }
        if (stream_window_topmost_button_) {
            stream_window_topmost_button_.IsEnabled(
                !fullscreen);
        }
        if (!fullscreen && stream_window_topmost_) {
            SetStreamWindowAlwaysOnTop(true);
        }
        UpdateStreamWindowLayout();
    } catch (...) {
    }
}

void ServerView::SetStreamWindowAlwaysOnTop(
    bool enabled) {
    if (!stream_window_ ||
        stream_window_fullscreen_) {
        return;
    }

    try {
        const auto presenter =
            stream_window_.AppWindow().Presenter();
        const auto overlapped =
            presenter.try_as<
                Microsoft::UI::Windowing::
                    OverlappedPresenter>();
        if (!overlapped) {
            return;
        }
        overlapped.IsAlwaysOnTop(enabled);
        stream_window_topmost_ = enabled;
        if (stream_window_topmost_button_) {
            stream_window_topmost_button_.Content(
                box_value(
                    enabled
                        ? hstring{L"Remove From Top"}
                        : hstring{L"Stay On Top"}));
        }
    } catch (...) {
    }
}

void ServerView::UpdateStreamWindowLayout() {
    if (!stream_window_ ||
        !stream_window_root_ ||
        !stream_window_viewport_ ||
        !stream_window_swap_chain_panel_ ||
        !screen_runtime_) {
        return;
    }

    const auto snapshot = screen_runtime_->snapshot();
    if (!snapshot.remote_viewing) {
        CloseStreamWindow();
        return;
    }

    if (snapshot.remote_width == 0 ||
        snapshot.remote_height == 0) {
        stream_window_viewport_.Visibility(
            xaml::Visibility::Collapsed);
        return;
    }
    stream_window_viewport_.Visibility(
        xaml::Visibility::Visible);

    const auto source_width =
        snapshot.remote_width;
    const auto source_height =
        snapshot.remote_height;
    const auto available_width =
        std::max(2.0, stream_window_root_.ActualWidth());
    const auto available_height =
        std::max(
            2.0,
            stream_window_root_.ActualHeight());
    const auto viewport =
        fit_viewport(
            source_width,
            source_height,
            available_width,
            available_height);
    if (viewport.width > 0.0 &&
        viewport.height > 0.0) {
        stream_window_viewport_.Width(viewport.width);
        stream_window_viewport_.Height(viewport.height);
    }

    const auto swap =
        screen_runtime_->remote_swap_chain();
    if (swap &&
        stream_window_swap_chain_.Get() != swap.Get()) {
        try {
            auto native =
                stream_window_swap_chain_panel_
                    .as<ISwapChainPanelNative>();
            if (SUCCEEDED(
                    native->SetSwapChain(swap.Get()))) {
                stream_window_swap_chain_ = swap;
            }
        } catch (...) {
        }
    }
}

void ServerView::CloseStreamWindow() noexcept {
    if (!stream_window_) {
        return;
    }

    auto window = stream_window_;
    try {
        if (stream_window_swap_chain_panel_) {
            auto native =
                stream_window_swap_chain_panel_
                    .as<ISwapChainPanelNative>();
            (void)native->SetSwapChain(nullptr);
        }
    } catch (...) {
    }

    stream_window_swap_chain_.Reset();
    stream_window_swap_chain_panel_ = nullptr;
    stream_window_viewport_ = nullptr;
    stream_window_root_ = nullptr;
    stream_window_mode_button_ = nullptr;
    stream_window_topmost_button_ = nullptr;
    stream_window_ = nullptr;
    stream_window_fullscreen_ = false;
    stream_window_topmost_ = false;

    try {
        window.Close();
    } catch (...) {
    }
}

void ServerView::DetachPreviewSwapChain() noexcept {
    try {
        auto panel_native =
            LocalShareSwapChainPanel().as<ISwapChainPanelNative>();
        (void)panel_native->SetSwapChain(nullptr);
    } catch (...) {
    }
    attached_preview_swap_chain_.Reset();
}

void ServerView::DetachRemoteSwapChain() noexcept {
    try {
        auto panel_native =
            RemoteShareSwapChainPanel().as<ISwapChainPanelNative>();
        (void)panel_native->SetSwapChain(nullptr);
    } catch (...) {
    }
    attached_remote_swap_chain_.Reset();
}

void ServerView::StopScreenShare() {
    if (room_mode_active_ &&
        room_runtime_ != nullptr) {
        catro_room_runtime_release_screen(
            room_runtime_);
    }
    if (screen_runtime_) {
        screen_runtime_->stop_sharing();
    }
    workspace_state_.share_screen.enable();
    DetachPreviewSwapChain();
    SharePreviewHost().Visibility(xaml::Visibility::Collapsed);

    const auto remote =
        screen_runtime_
            ? screen_runtime_->snapshot()
            : catro::screen::ScreenShareSnapshot{};
    VoiceIdentityPanel().Visibility(
        remote.remote_available || remote.remote_viewing
            ? xaml::Visibility::Collapsed
            : xaml::Visibility::Visible);

    ShareScreenButton().Content(
        box_value(hstring{L"Share screen"}));
    controls::ToolTipService::SetToolTip(
        ShareScreenIconButton(),
        box_value(hstring{L"Share screen"}));
}

void ServerView::UpdateScreenShareUi() {
    if (!screen_runtime_) {
        return;
    }

    const auto snapshot = screen_runtime_->snapshot();
    const bool local_active =
        snapshot.state == catro::screen::ScreenShareState::starting ||
        snapshot.state == catro::screen::ScreenShareState::sharing;
    const bool remote_available = snapshot.remote_available;
    const bool remote_viewing = snapshot.remote_viewing;
    const bool remote_active = snapshot.remote_active;
    const auto room =
        room_mode_active_ &&
                room_runtime_ != nullptr
            ? catro_room_runtime_snapshot(
                  room_runtime_)
            : CatroRoomRuntimeSnapshot{};
    const bool another_participant_sharing =
        room_mode_active_ &&
        room.screen_owner[0] != '\0' &&
        std::string_view{room.screen_owner} !=
            room_peer_id_;
    const auto voice =
        voice_runtime_ != nullptr
            ? catro_voice_runtime_snapshot(
                  voice_runtime_)
            : CatroVoiceRuntimeSnapshot{};
    const bool joined =
        voice.state == CATRO_VOICE_JOINED &&
        (!room_mode_active_ ||
         room.state == CATRO_ROOM_JOINED);
    const bool share_busy =
        workspace_state_.share_screen.availability ==
        catro::app::Availability::busy;
    const bool can_share =
        local_active ||
        (joined && !share_busy &&
         !another_participant_sharing);
    ShareScreenButton().IsEnabled(can_share);
    ShareScreenIconButton().IsEnabled(can_share);

    if (stream_window_ && !remote_viewing) {
        CloseStreamWindow();
    }

    const auto panel_width = VoicePanel().ActualWidth();
    const auto panel_height = VoicePanel().ActualHeight();

    // Remote viewing follows the actual available voice-panel viewport. Do not impose a 960x720
    // product cap here: a large Catro window should let a 16:9, 16:10, 4:3, ultrawide, or portrait
    // stream grow until one real panel edge becomes limiting. fit_viewport() preserves the decoded
    // source aspect ratio while presentation scales independently from the encoder's no-upscale
    // policy.
    const auto remote_max_stream_width =
        panel_width > 80.0
            ? std::max(160.0, panel_width - 44.0)
            : 720.0;
    const auto remote_max_stream_height =
        panel_height > 140.0
            ? std::max(120.0, panel_height - 120.0)
            : 405.0;

    // Self-preview is not the primary content. Keep its surface bounded so maximizing Catro while
    // sharing cannot turn the optional local preview into a large extra GPU presentation workload.
    const auto local_max_stream_width =
        std::min(960.0, remote_max_stream_width);
    const auto local_max_stream_height =
        std::min(720.0, remote_max_stream_height);

    ShareScreenButton().Content(
        box_value(
            local_active
                ? hstring{L"Stop sharing"}
                : share_busy
                      ? hstring{L"Preparing…"}
                      : hstring{L"Share screen"}));
    const auto share_tip =
        another_participant_sharing &&
                !local_active
            ? hstring{
                  L"Another participant is sharing"}
            : local_active
                  ? hstring{L"Stop sharing"}
                  : !workspace_state_.share_screen.reason.empty()
                        ? to_hstring(
                              workspace_state_.share_screen.reason)
                        : hstring{L"Share screen"};
    controls::ToolTipService::SetToolTip(
        ShareScreenButton(),
        box_value(share_tip));
    controls::ToolTipService::SetToolTip(
        ShareScreenIconButton(),
        box_value(share_tip));

    VoiceIdentityPanel().Visibility(
        local_active || remote_available || remote_viewing
            ? xaml::Visibility::Collapsed
            : xaml::Visibility::Visible);

    RemoteStreamInvite().Visibility(
        remote_available && !remote_viewing
            ? xaml::Visibility::Visible
            : xaml::Visibility::Collapsed);

    if (remote_viewing) {
        if (stream_window_) {
            RemoteShareHost().Visibility(
                xaml::Visibility::Collapsed);
            DetachRemoteSwapChain();
            UpdateStreamWindowLayout();
        } else {
            RemoteShareHost().Visibility(
                xaml::Visibility::Visible);
        }

        const auto remote_size =
            fit_viewport(
                snapshot.remote_width,
                snapshot.remote_height,
                remote_max_stream_width,
                remote_max_stream_height);
        const bool have_remote_geometry =
            remote_size.width > 0.0 &&
            remote_size.height > 0.0;
        RemoteShareViewport().Visibility(
            have_remote_geometry
                ? xaml::Visibility::Visible
                : xaml::Visibility::Collapsed);
        if (have_remote_geometry) {
            RemoteShareViewport().Width(
                remote_size.width);
            RemoteShareViewport().Height(
                remote_size.height);
        }

        if (!stream_window_) {
            const auto remote_swap =
                screen_runtime_->remote_swap_chain();
            if (remote_swap &&
                attached_remote_swap_chain_.Get() !=
                    remote_swap.Get()) {
                try {
                    auto panel_native =
                        RemoteShareSwapChainPanel()
                            .as<ISwapChainPanelNative>();
                    if (SUCCEEDED(
                            panel_native->SetSwapChain(
                                remote_swap.Get()))) {
                        attached_remote_swap_chain_ =
                            remote_swap;
                    }
                } catch (...) {
                }
            }
        }

        if (remote_active &&
            snapshot.remote_width != 0 &&
            snapshot.remote_height != 0) {
            std::wstring meta =
                std::to_wstring(snapshot.remote_width);
            meta += L"×";
            meta += std::to_wstring(snapshot.remote_height);
            meta += L"  ·  LIVE";
            if (snapshot.remote_stream_audio_active) {
                meta += L"  ·  AUDIO";
            }
            RemoteShareMetaText().Text(hstring{meta});
        } else if (remote_available) {
            RemoteShareMetaText().Text(
                L"Connecting to live stream…");
        } else {
            RemoteShareMetaText().Text(
                L"Waiting for stream…");
        }
    } else {
        DetachRemoteSwapChain();
        RemoteShareHost().Visibility(xaml::Visibility::Collapsed);
    }

    if (local_active) {
        SharePreviewHost().Visibility(xaml::Visibility::Visible);
        LocalShareViewport().Visibility(local_preview_enabled_ ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
        LocalPreviewPausedHint().Visibility(local_preview_enabled_ ? xaml::Visibility::Collapsed : xaml::Visibility::Visible);

        const auto local_swap =
            local_preview_enabled_ ? screen_runtime_->preview_swap_chain() : nullptr;
        if (local_swap &&
            attached_preview_swap_chain_.Get() !=
                local_swap.Get()) {
            try {
                auto panel_native =
                    LocalShareSwapChainPanel().as<ISwapChainPanelNative>();
                if (SUCCEEDED(panel_native->SetSwapChain(
                        local_swap.Get()))) {
                    attached_preview_swap_chain_ = local_swap;
                }
            } catch (...) {
            }
        }

        ShareSourceText().Text(
            snapshot.source_title.empty()
                ? hstring{L"Starting…"}
                : to_hstring(snapshot.source_title));

        if (snapshot.frames_sent > 0 && snapshot.encoded_width != 0 &&
            snapshot.encoded_height != 0) {
            std::wstring meta =
                std::to_wstring(snapshot.encoded_width);
            meta += L"×";
            meta += std::to_wstring(snapshot.encoded_height);
            meta += L"  ·  SENDING";
            if (snapshot.stream_audio_active) {
                meta += L"  ·  AUDIO";
            } else if (
                snapshot.stream_audio_enabled &&
                !snapshot.stream_audio_error.empty()) {
                meta += L"  ·  VIDEO ONLY";
            }
            ShareMetaText().Text(hstring{meta});
        } else {
            ShareMetaText().Text(L"Starting…");
        }

        const auto local_source_width =
            snapshot.encoded_width != 0
                ? snapshot.encoded_width
                : (snapshot.source_width != 0
                       ? snapshot.source_width
                       : 1280U);
        const auto local_source_height =
            snapshot.encoded_height != 0
                ? snapshot.encoded_height
                : (snapshot.source_height != 0
                       ? snapshot.source_height
                       : 720U);

        if (remote_viewing) {
            const auto local_size =
                fit_viewport(
                    local_source_width,
                    local_source_height,
                    300.0,
                    210.0);
            if (local_size.width > 0.0 &&
                local_size.height > 0.0) {
                LocalShareViewport().Width(local_size.width);
                LocalShareViewport().Height(local_size.height);
            }
            SharePreviewHost().HorizontalAlignment(
                xaml::HorizontalAlignment::Right);
            SharePreviewHost().VerticalAlignment(
                xaml::VerticalAlignment::Bottom);
            SharePreviewHost().Margin(
                xaml::Thickness{22.0, 22.0, 22.0, 22.0});
            Microsoft::UI::Xaml::Controls::Canvas::SetZIndex(
                SharePreviewHost(), 10);
        } else {
            const auto local_size =
                fit_viewport(
                    local_source_width,
                    local_source_height,
                    local_max_stream_width,
                    local_max_stream_height);
            if (local_size.width > 0.0 &&
                local_size.height > 0.0) {
                LocalShareViewport().Width(local_size.width);
                LocalShareViewport().Height(local_size.height);
            }
            SharePreviewHost().HorizontalAlignment(
                xaml::HorizontalAlignment::Center);
            SharePreviewHost().VerticalAlignment(
                xaml::VerticalAlignment::Center);
            SharePreviewHost().Margin(
                xaml::Thickness{22.0, 22.0, 22.0, 22.0});
            Microsoft::UI::Xaml::Controls::Canvas::SetZIndex(
                SharePreviewHost(), 0);
        }
    } else {
        DetachPreviewSwapChain();
        SharePreviewHost().Visibility(xaml::Visibility::Collapsed);
    }

    if (stream_window_) {
        UpdateStreamWindowLayout();
    }

    if (!snapshot.stream_audio_error.empty()) {
        controls::ToolTipService::SetToolTip(
            ShareMetaText(),
            box_value(
                to_hstring(
                    snapshot.stream_audio_error)));
    } else {
        controls::ToolTipService::SetToolTip(
            ShareMetaText(), nullptr);
    }

    if (snapshot.state ==
            catro::screen::ScreenShareState::failed &&
        !snapshot.error.empty()) {
        controls::ToolTipService::SetToolTip(
            ShareScreenButton(),
            box_value(to_hstring(snapshot.error)));
        VoiceStateText().Text(to_hstring(snapshot.error));
    }
}

std::uint32_t ServerView::LocalStreamId() const noexcept {
    std::uint32_t value = 0x4354524fU; // "CTRO"
    if (local_state_) {
        for (std::size_t index = 0; index < 4; ++index) {
            value = (value << 5U) ^ (value >> 27U) ^
                    std::to_integer<std::uint8_t>(local_state_->identity.id.bytes[index]);
        }
    }
    // Rooms identify a speaker by identity alone (catro::voice::user_stream_id), the same on every
    // platform. Two engineering direct peers on one machine share an identity, so the port tells
    // them apart.
    if (!room_mode_active_) {
        value ^= static_cast<std::uint32_t>(direct_voice_config().bind.port);
    }
    return value == 0 ? 1U : value;
}


} // namespace winrt::Catro::implementation

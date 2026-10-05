#include "pch.h"

#include "Server/ServerView.xaml.h"
#include "Server/StreamViewport.hpp"
#include "Settings/Voice.hpp"

#include <catro/screen_runtime.hpp>

#include <microsoft.ui.xaml.media.dxinterop.h>

#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Text.h>

#include <algorithm>
#include <chrono>
#include <cstdint>

// Full screen stays inside Catro, like Discord: the stream fills the server page while the
// channel list, header, members and call controls step aside. Nothing switches the monitor's
// presentation, so the stream keeps its one swap chain and panel throughout. The separate
// pop-out window is a normal resizable window for watching beside another app.

namespace winrt::Catro::implementation {

namespace {

namespace xaml = Microsoft::UI::Xaml;
namespace controls = Microsoft::UI::Xaml::Controls;
namespace media = Microsoft::UI::Xaml::Media;
using namespace std::chrono_literals;
using stream_viewport::fit_swap_chain;
using stream_viewport::fit_viewport;

[[nodiscard]] bool sharing(const catro::screen::ScreenShareSnapshot& snapshot) noexcept {
    return snapshot.state == catro::screen::ScreenShareState::starting ||
           snapshot.state == catro::screen::ScreenShareState::sharing;
}

} // namespace

void ServerView::OnSizeChanged(IInspectable const&, xaml::SizeChangedEventArgs const&) {
    ApplyServerLayout();
    UpdateScreenShareUi();
}

void ServerView::ApplyServerLayout() {
    const auto width = ServerLayout().ActualWidth();
    const auto visible = [](bool shown) {
        return shown ? xaml::Visibility::Visible : xaml::Visibility::Collapsed;
    };
    const bool show_members = !stage_active_ && width >= 920.0;
    MembersColumn().Width(xaml::GridLengthHelper::FromPixels(show_members ? 216.0 : 0.0));
    MembersPane().Visibility(visible(show_members));
    ChannelsColumn().Width(xaml::GridLengthHelper::FromPixels(
        stage_active_ ? 0.0 : width >= 760.0 ? 240.0 : 196.0));
    ChannelsPane().Visibility(visible(!stage_active_));
    ChannelHeaderRow().Height(xaml::GridLengthHelper::FromPixels(stage_active_ ? 0.0 : 76.0));
    ChannelHeader().Visibility(visible(!stage_active_));
    VoiceControlsBar().Visibility(visible(!stage_active_));
    // The same action remains available in the voice control bar on compact windows.
    ShareScreenButton().Visibility(visible(width >= 760.0));
}

void ServerView::SetStage(bool enabled, bool local) {
    if (enabled) {
        if (!screen_runtime_) {
            return;
        }
        const auto snapshot = screen_runtime_->snapshot();
        if (local ? !sharing(snapshot) : !snapshot.remote_viewing) {
            return;
        }
        CloseStreamWindow();
    }
    local = enabled && local;
    if (stage_active_ == enabled && stage_local_ == local) {
        return;
    }
    stage_active_ = enabled;
    stage_local_ = local;

    if (enabled) {
        VoicePanel().Background(media::SolidColorBrush(Windows::UI::Color{255, 0, 0, 0}));
    } else {
        VoicePanel().ClearValue(controls::Panel::BackgroundProperty());
    }
    FullScreenStreamText().Text(stage_active_ && !stage_local_ ? L"Exit Full Screen" : L"Full Screen");
    FullScreenStreamIcon().Glyph(stage_active_ && !stage_local_ ? L"" : L"");
    LocalStageExitButton().Visibility(
        stage_local_ ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    ShowStageControls();
    ApplyServerLayout();
    // Your own stream renders at full size only while it fills Catro.
    ApplyActivityPolicy();
    UpdateScreenShareUi();
    // Esc needs focus inside the page, and the button that opened the stage may have collapsed.
    if (stage_active_) {
        (void)(stage_local_ ? LocalStageExitButton() : FullScreenStreamButton())
            .Focus(xaml::FocusState::Programmatic);
    }
}

void ServerView::ShowStageControls() {
    RemoteStreamToolbar().Opacity(1.0);
    LocalStageExitButton().Opacity(1.0);
    if (!stage_active_) {
        if (stage_idle_timer_) {
            stage_idle_timer_.Stop();
        }
        return;
    }
    if (!stage_idle_timer_) {
        stage_idle_timer_ = DispatcherQueue().CreateTimer();
        stage_idle_timer_.Interval(2500ms);
        stage_idle_timer_.IsRepeating(false);
        stage_idle_timer_.Tick([weak = get_weak()](auto const&, auto const&) {
            // The controls fade while the mouse rests over the stream and return when it moves.
            if (auto self = weak.get(); self && self->stage_active_) {
                self->RemoteStreamToolbar().Opacity(0.0);
                self->LocalStageExitButton().Opacity(0.0);
            }
        });
    }
    stage_idle_timer_.Stop();
    stage_idle_timer_.Start();
}

void ServerView::OnFullScreenStream(IInspectable const&, xaml::RoutedEventArgs const&) {
    SetStage(!(stage_active_ && !stage_local_), false);
}

void ServerView::OnRemoteStreamDoubleTapped(
    IInspectable const&, xaml::Input::DoubleTappedRoutedEventArgs const&) {
    SetStage(!(stage_active_ && !stage_local_), false);
}

void ServerView::OnLocalStreamDoubleTapped(
    IInspectable const&, xaml::Input::DoubleTappedRoutedEventArgs const&) {
    SetStage(!stage_local_, true);
}

void ServerView::OnLocalFullScreen(IInspectable const&, xaml::RoutedEventArgs const&) {
    SetStage(true, true);
}

void ServerView::OnExitStage(IInspectable const&, xaml::RoutedEventArgs const&) {
    SetStage(false, false);
}

void ServerView::OnStageEscape(
    xaml::Input::KeyboardAccelerator const&,
    xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args) {
    if (stage_active_) {
        args.Handled(true);
        SetStage(false, false);
    }
}

// A quick double-click on a stream button must not also toggle full screen.
void ServerView::OnSwallowDoubleTap(
    IInspectable const&, xaml::Input::DoubleTappedRoutedEventArgs const& args) {
    args.Handled(true);
}

void ServerView::OnStagePointerMoved(
    IInspectable const&, xaml::Input::PointerRoutedEventArgs const&) {
    if (stage_active_) {
        ShowStageControls();
    }
}

void ServerView::OnPopOutStream(IInspectable const&, xaml::RoutedEventArgs const&) {
    OpenStreamWindow();
}

void ServerView::OpenStreamWindow() {
    if (!screen_runtime_ || !screen_runtime_->snapshot().remote_viewing) {
        return;
    }
    SetStage(false, false);
    if (stream_window_) {
        stream_window_.Activate();
        UpdateStreamWindowLayout();
        return;
    }

    try {
        DetachRemoteSwapChain();

        stream_window_ = xaml::Window{};
        stream_window_.Title(L"Catro — Live Stream");

        const media::SolidColorBrush black{Windows::UI::Color{255, 0, 0, 0}};
        stream_window_root_ = controls::Grid{};
        stream_window_root_.Background(black);

        stream_window_viewport_ = controls::Border{};
        stream_window_viewport_.Background(black);
        stream_window_viewport_.HorizontalAlignment(xaml::HorizontalAlignment::Center);
        stream_window_viewport_.VerticalAlignment(xaml::VerticalAlignment::Center);
        stream_window_swap_chain_panel_ = controls::SwapChainPanel{};
        stream_window_viewport_.Child(stream_window_swap_chain_panel_);
        stream_window_root_.Children().Append(stream_window_viewport_);

        controls::StackPanel toolbar;
        toolbar.Orientation(controls::Orientation::Horizontal);
        toolbar.Spacing(8);
        toolbar.Margin(xaml::Thickness{14.0});
        toolbar.HorizontalAlignment(xaml::HorizontalAlignment::Right);
        toolbar.VerticalAlignment(xaml::VerticalAlignment::Top);

        controls::Border live_badge;
        live_badge.Padding(xaml::Thickness{9.0, 5.0, 9.0, 5.0});
        live_badge.CornerRadius(xaml::CornerRadius{7.0});
        live_badge.Background(media::SolidColorBrush(Windows::UI::Color{220, 35, 35, 35}));
        controls::TextBlock live_text;
        live_text.Text(L"LIVE");
        live_text.FontSize(10.0);
        live_text.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        live_badge.Child(live_text);
        toolbar.Children().Append(live_badge);

        controls::Border volume_background;
        volume_background.Padding(xaml::Thickness{8.0, 0.0, 8.0, 0.0});
        volume_background.CornerRadius(xaml::CornerRadius{7.0});
        volume_background.Background(media::SolidColorBrush(Windows::UI::Color{220, 35, 35, 35}));
        controls::StackPanel volume_controls;
        volume_controls.Orientation(controls::Orientation::Horizontal);
        volume_controls.Spacing(6);
        controls::TextBlock volume_title;
        volume_title.Text(L"Stream");
        volume_title.VerticalAlignment(xaml::VerticalAlignment::Center);
        volume_title.Foreground(media::SolidColorBrush(Windows::UI::Color{255, 255, 255, 255}));
        volume_controls.Children().Append(volume_title);
        stream_window_volume_slider_ = controls::Slider{};
        stream_window_volume_slider_.Minimum(0);
        stream_window_volume_slider_.Maximum(200);
        stream_window_volume_slider_.StepFrequency(5);
        stream_window_volume_slider_.Width(110);
        stream_window_volume_slider_.VerticalAlignment(xaml::VerticalAlignment::Center);
        stream_window_volume_slider_.Value(catro::shell::voice_preferences().stream_volume * 100.0);
        xaml::Automation::AutomationProperties::SetName(stream_window_volume_slider_, L"Stream volume");
        stream_window_volume_slider_.ValueChanged(
            [this](auto const& sender, auto const& args) { OnStreamVolumeChanged(sender, args); });
        volume_controls.Children().Append(stream_window_volume_slider_);
        stream_window_volume_label_ = controls::TextBlock{};
        stream_window_volume_label_.Width(38);
        stream_window_volume_label_.VerticalAlignment(xaml::VerticalAlignment::Center);
        stream_window_volume_label_.Foreground(volume_title.Foreground());
        volume_controls.Children().Append(stream_window_volume_label_);
        volume_background.Child(volume_controls);
        toolbar.Children().Append(volume_background);
        ApplyStreamVolume();

        const auto add_button = [&toolbar](hstring const& label) {
            controls::Button button;
            button.Content(box_value(label));
            button.Padding(xaml::Thickness{12.0, 6.0, 12.0, 6.0});
            toolbar.Children().Append(button);
            return button;
        };
        stream_window_topmost_button_ = add_button(L"Stay On Top");
        stream_window_topmost_button_.Click([this](auto const&, auto const&) {
            SetStreamWindowAlwaysOnTop(!stream_window_topmost_);
        });
        add_button(L"Back to Catro").Click([this](auto const&, auto const&) {
            CloseStreamWindow();
            UpdateScreenShareUi();
        });
        add_button(L"Leave Stream").Click([this](auto const&, auto const&) {
            if (screen_runtime_) {
                screen_runtime_->set_remote_viewing_enabled(false);
            }
            CloseStreamWindow();
            UpdateScreenShareUi();
        });
        stream_window_root_.Children().Append(toolbar);
        stream_window_root_.SizeChanged([this](auto const&, auto const&) {
            UpdateStreamWindowLayout();
        });

        stream_window_.Closed([this](auto const& sender, auto const&) {
            // CloseStreamWindow already let go of this window when it closed it.
            if (stream_window_ && sender == stream_window_) {
                ReleaseStreamWindow();
            }
            UpdateScreenShareUi();
            ApplyActivityPolicy();
        });

        stream_window_.Content(stream_window_root_);
        stream_window_.Activate();

        const auto snapshot = screen_runtime_->snapshot();
        if (snapshot.remote_width != 0 && snapshot.remote_height != 0) {
            const auto initial =
                fit_viewport(snapshot.remote_width, snapshot.remote_height, 1280.0, 760.0);
            stream_window_.AppWindow().Resize({
                static_cast<std::int32_t>(std::max(640.0, initial.width)),
                static_cast<std::int32_t>(std::max(420.0, initial.height + 40.0)),
            });
        } else {
            stream_window_.AppWindow().Resize({960, 640});
        }

        ApplyActivityPolicy();
        UpdateStreamWindowLayout();
    } catch (...) {
        CloseStreamWindow();
    }
}

void ServerView::SetStreamWindowAlwaysOnTop(bool enabled) {
    if (!stream_window_) {
        return;
    }
    try {
        const auto overlapped = stream_window_.AppWindow()
                                    .Presenter()
                                    .try_as<Microsoft::UI::Windowing::OverlappedPresenter>();
        if (!overlapped) {
            return;
        }
        overlapped.IsAlwaysOnTop(enabled);
        stream_window_topmost_ = enabled;
        if (stream_window_topmost_button_) {
            stream_window_topmost_button_.Content(
                box_value(enabled ? hstring{L"Remove From Top"} : hstring{L"Stay On Top"}));
        }
    } catch (...) {
    }
}

void ServerView::UpdateStreamWindowLayout() {
    if (!stream_window_ || !stream_window_root_ || !stream_window_viewport_ ||
        !stream_window_swap_chain_panel_ || !screen_runtime_) {
        return;
    }

    const auto snapshot = screen_runtime_->snapshot();
    if (!snapshot.remote_viewing) {
        CloseStreamWindow();
        return;
    }
    if (snapshot.remote_width == 0 || snapshot.remote_height == 0) {
        stream_window_viewport_.Visibility(xaml::Visibility::Collapsed);
        return;
    }
    stream_window_viewport_.Visibility(xaml::Visibility::Visible);
    const auto viewport = fit_viewport(
        snapshot.remote_width,
        snapshot.remote_height,
        std::max(2.0, stream_window_root_.ActualWidth()),
        std::max(2.0, stream_window_root_.ActualHeight()));
    if (viewport.width > 0.0 && viewport.height > 0.0) {
        stream_window_viewport_.Width(viewport.width);
        stream_window_viewport_.Height(viewport.height);
    }

    const auto swap = screen_runtime_->remote_swap_chain();
    if (swap && stream_window_swap_chain_.Get() != swap.Get()) {
        try {
            auto native = stream_window_swap_chain_panel_.as<ISwapChainPanelNative>();
            if (SUCCEEDED(native->SetSwapChain(swap.Get()))) {
                stream_window_swap_chain_ = swap;
            }
        } catch (...) {
        }
    }
    fit_swap_chain(stream_window_swap_chain_.Get(), viewport.width, viewport.height);
}

void ServerView::ReleaseStreamWindow() noexcept {
    try {
        if (stream_window_swap_chain_panel_) {
            (void)stream_window_swap_chain_panel_.as<ISwapChainPanelNative>()->SetSwapChain(nullptr);
        }
    } catch (...) {
    }
    stream_window_swap_chain_.Reset();
    stream_window_swap_chain_panel_ = nullptr;
    stream_window_viewport_ = nullptr;
    stream_window_root_ = nullptr;
    stream_window_topmost_button_ = nullptr;
    stream_window_volume_slider_ = nullptr;
    stream_window_volume_label_ = nullptr;
    stream_window_ = nullptr;
    stream_window_topmost_ = false;
}

void ServerView::CloseStreamWindow() noexcept {
    if (!stream_window_) {
        return;
    }
    auto window = stream_window_;
    ReleaseStreamWindow();
    try {
        window.Close();
    } catch (...) {
    }
}

} // namespace winrt::Catro::implementation

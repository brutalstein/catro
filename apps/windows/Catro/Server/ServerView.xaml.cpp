#include "pch.h"

#include "Server/ServerView.xaml.h"
#include "Server/ServerView.RuntimeConfig.hpp"
#if __has_include("ServerView.g.cpp")
#include "ServerView.g.cpp"
#endif

#include <catro/screen_runtime.hpp>

#include <chrono>
#include <memory>
#include <string>

namespace winrt::Catro::implementation {

namespace xaml = Microsoft::UI::Xaml;
using namespace std::chrono_literals;
using server_view_detail::environment;

ServerView::~ServerView() {
    CloseStreamWindow();
    if (screen_timer_) {
        screen_timer_.Stop();
    }
    if (screen_runtime_) {
        screen_runtime_->stop();
    }
    if (voice_timer_) {
        voice_timer_.Stop();
    }
    if (message_timer_) {
        message_timer_.Stop();
    }
    if (member_timer_) {
        member_timer_.Stop();
    }
    if (access_timer_) {
        access_timer_.Stop();
    }
    if (voice_runtime_ != nullptr) {
        catro_voice_runtime_destroy(voice_runtime_);
        voice_runtime_ = nullptr;
    }
    if (room_runtime_ != nullptr) {
        catro_room_runtime_destroy(room_runtime_);
        room_runtime_ = nullptr;
    }
}

void ServerView::InitializeComponent() {
    ServerViewT<ServerView>::InitializeComponent();
    room_runtime_ = catro_room_runtime_create();
    voice_runtime_ = catro_voice_runtime_create();
    screen_runtime_ =
        std::make_unique<catro::screen::WindowsScreenShareRuntime>();

    screen_timer_ = DispatcherQueue().CreateTimer();
    screen_timer_.Interval(100ms);
    screen_timer_.Tick(
        [this](auto&&, auto&&) { UpdateScreenShareUi(); });

    voice_timer_ = DispatcherQueue().CreateTimer();
    voice_timer_.Interval(500ms);
    voice_timer_.Tick([this](auto&&, auto&&) { UpdateVoiceUi(); });

    message_timer_ = DispatcherQueue().CreateTimer();
    message_timer_.Interval(1s);
    message_timer_.Tick(
        [this](auto&&, auto&&) { BeginMessageRefresh(); });

    member_timer_ = DispatcherQueue().CreateTimer();
    member_timer_.Interval(5s);
    member_timer_.Tick(
        [this](auto&&, auto&&) { BeginMemberRefresh(); });

    access_timer_ = DispatcherQueue().CreateTimer();
    access_timer_.Interval(5s);
    access_timer_.Tick(
        [this](auto&&, auto&&) { BeginJoinRequestRefresh(); });

    Loaded([this](auto&&, auto&&) {
        page_loaded_ = true;
        if (voice_runtime_ != nullptr) {
            const auto snapshot = catro_voice_runtime_snapshot(voice_runtime_);
            if (snapshot.state == CATRO_VOICE_STARTING || snapshot.state == CATRO_VOICE_JOINED) {
                voice_timer_.Start();
            }
        }
        if (screen_runtime_) {
            const bool local_preview_enabled =
                environment("CATRO_LOCAL_PREVIEW").value_or("1") != "0";
            screen_runtime_->set_local_preview_enabled(local_preview_enabled);
            const auto share = screen_runtime_->snapshot();
            if (share.state != catro::screen::ScreenShareState::idle) {
                screen_timer_.Start();
            }
        }
        UpdateVoiceUi();
        UpdateScreenShareUi();
        UpdateMessageUi();
        if (directory_service_ &&
            !directory_access_token_.empty() &&
            directory_server_) {
            member_timer_.Start();
            if (directory_server_->role == "owner" &&
                access_timer_) {
                access_timer_.Start();
            }
        }
        BeginMemberRefresh();
        BeginJoinRequestRefresh();
        BeginMessageRefresh();
    });
    Unloaded([this](auto&&, auto&&) {
        page_loaded_ = false;
        // Voice and screen share are room state, not page state. Navigating to Settings/System must
        // not disconnect either media worker. Suspend only presentation/polling work that cannot be
        // seen while this page is unloaded; transport and encode continue uninterrupted.
        if (voice_timer_) {
            voice_timer_.Stop();
        }
        if (screen_timer_) {
            screen_timer_.Stop();
        }
        if (message_timer_) {
            message_timer_.Stop();
        }
        if (member_timer_) {
            member_timer_.Stop();
        }
        if (access_timer_) {
            access_timer_.Stop();
        }
        if (screen_runtime_) {
            screen_runtime_->set_local_preview_enabled(false);
            screen_runtime_->set_remote_viewing_enabled(false);
        }
        DetachPreviewSwapChain();
        DetachRemoteSwapChain();
        CloseStreamWindow();
    });

    ShowChannel("general");
    UpdateOnlineStatus();
    UpdateVoiceUi();
    UpdateScreenShareUi();
    UpdateMessageUi();
}

void ServerView::SetLocalState(const catro::community::LocalState& state) {
    local_state_ = state;
    ServerName().Text(to_hstring(state.personal_server.name));
    ProfileName().Text(to_hstring(state.identity.display_name));
    VoiceLocalName().Text(to_hstring(state.identity.display_name));
    if (!directory_server_) {
        ShowLocalMemberFallback();
    }

    for (const auto& channel : state.personal_server.channels) {
        if (channel.kind == catro::community::ChannelKind::text) {
            TextChannelName().Text(to_hstring(channel.name));
            TextEmptyTitle().Text(to_hstring(std::string("# ") + channel.name));
            Composer().PlaceholderText(to_hstring(std::string("Message #") + channel.name));
            break;
        }
    }
    for (const auto& channel : state.personal_server.channels) {
        if (channel.kind == catro::community::ChannelKind::voice) {
            VoiceChannelName().Text(to_hstring(channel.name));
            break;
        }
    }
    ShowChannel(state_.channel_id());
}

void ServerView::OnTextChannel(IInspectable const&, xaml::RoutedEventArgs const&) {
    ShowChannel("general");
}

void ServerView::OnVoiceChannel(IInspectable const&, xaml::RoutedEventArgs const&) {
    ShowChannel("voice");
}

void ServerView::OnComposerKeyDown(
    IInspectable const&,
    Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args) {
    if (args.Key() == Windows::System::VirtualKey::Enter) {
        args.Handled(true);
        BeginSendMessage();
    }
}

void ServerView::OnSendMessage(
    IInspectable const&,
    xaml::RoutedEventArgs const&) {
    BeginSendMessage();
}

void ServerView::OnInvite(
    IInspectable const&,
    xaml::RoutedEventArgs const&) {
    BeginInvite();
}

void ServerView::OnAccess(
    IInspectable const&,
    xaml::RoutedEventArgs const&) {
    ShowAccessDialog();
}

void ServerView::ShowChannel(std::string_view id) {
    if (!catro::app::channel_kind(id)) {
        return;
    }
    (void)state_.select_channel(id);
    const bool voice = state_.active_channel_kind() == catro::community::ChannelKind::voice;

    TextSelection().Visibility(voice ? xaml::Visibility::Collapsed : xaml::Visibility::Visible);
    VoiceSelection().Visibility(voice ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    TextPanel().Visibility(voice ? xaml::Visibility::Collapsed : xaml::Visibility::Visible);
    VoicePanel().Visibility(voice ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    VoiceToolbar().Visibility(voice ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    TextChannelGlyph().Visibility(voice ? xaml::Visibility::Collapsed : xaml::Visibility::Visible);
    VoiceChannelGlyph().Visibility(voice ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    ChannelTitle().Text(voice ? VoiceChannelName().Text() : TextChannelName().Text());
    if (voice) {
        UpdateVoiceUi();
    }
    UpdateMessageUi();
    if (!voice) {
        BeginMessageRefresh();
    }
}


} // namespace winrt::Catro::implementation

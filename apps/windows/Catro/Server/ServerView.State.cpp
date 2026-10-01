#include "pch.h"

#include "Server/ServerView.xaml.h"

#include <algorithm>
#include <string>

namespace winrt::Catro::implementation {

namespace xaml = Microsoft::UI::Xaml;
namespace controls = Microsoft::UI::Xaml::Controls;

void ServerView::UpdateOnlineStatus() {
    // The shell status bar carries the full connection message; the channel header stays short.
    switch (workspace_state_.connection) {
    case catro::app::ConnectionState::synchronized:
        OnlineStatusText().Text(L"Connected");
        break;
    case catro::app::ConnectionState::local_only:
        OnlineStatusText().Text(L"Local mode");
        break;
    default:
        OnlineStatusText().Text(
            to_hstring(workspace_state_.connection_message));
        break;
    }
}

void ServerView::UpdateAccessUi() {
    const bool owner =
        directory_server_ &&
        directory_server_->role == "owner" &&
        !directory_server_->public_code.empty();

    AccessButton().Visibility(
        owner
            ? xaml::Visibility::Visible
            : xaml::Visibility::Collapsed);
    AccessButton().IsEnabled(
        owner &&
        !access_decision_pending_ &&
        !access_dialog_open_);

    AccessRequestCount().Text(
        hstring{
            std::to_wstring(
                pending_join_requests_.size())});

    if (owner) {
        std::wstring tooltip =
            L"Server access · ";
        tooltip += std::to_wstring(
            pending_join_requests_.size());
        tooltip += L" pending";
        controls::ToolTipService::SetToolTip(
            AccessButton(),
            box_value(hstring{tooltip}));
    }

    if (access_timer_) {
        if (page_loaded_ && owner) {
            access_timer_.Start();
        } else {
            access_timer_.Stop();
        }
    }
}

void ServerView::UpdateMessageUi() {
    const bool text_active =
        state_.active_channel_kind() ==
        catro::community::ChannelKind::text;
    const bool configured =
        directory_service_.has_value() &&
        !directory_access_token_.empty() &&
        directory_server_.has_value() &&
        !directory_server_->text_channel_id.empty();
    const bool sending =
        workspace_state_.send_message.availability ==
        catro::app::Availability::busy;

    Composer().IsEnabled(
        text_active && configured && !sending);
    SendMessageButton().IsEnabled(
        text_active && configured && !sending);

    if (message_timer_) {
        if (page_loaded_ && text_active && configured) {
            message_timer_.Start();
        } else {
            message_timer_.Stop();
        }
    }

    // The shell status bar already shows the connection message; repeat only other reasons.
    const bool own_reason =
        workspace_state_.send_message.reason !=
        workspace_state_.connection_message;
    if (text_active && !configured && own_reason) {
        TextStatusText().Text(
            to_hstring(workspace_state_.send_message.reason));
        TextStatusText().Visibility(
            xaml::Visibility::Visible);
    } else if (
        text_active && own_reason &&
        workspace_state_.send_message.availability !=
            catro::app::Availability::ready &&
        !workspace_state_.send_message.reason.empty()) {
        TextStatusText().Text(
            to_hstring(workspace_state_.send_message.reason));
        TextStatusText().Visibility(
            xaml::Visibility::Visible);
    }
}

void ServerView::UpdateVoiceUi() {
    if (voice_runtime_ == nullptr) {
        JoinVoiceButton().IsEnabled(false);
        ShareScreenButton().IsEnabled(false);
        ShareScreenIconButton().IsEnabled(false);
        VoiceStateText().Text(L"Unavailable");
        return;
    }

    const auto snapshot =
        catro_voice_runtime_snapshot(
            voice_runtime_);
    const auto room =
        room_mode_active_ &&
                room_runtime_ != nullptr
            ? catro_room_runtime_snapshot(
                  room_runtime_)
            : CatroRoomRuntimeSnapshot{};
    const bool room_ready =
        !room_mode_active_ ||
        room.state == CATRO_ROOM_JOINED;
    const bool active =
        snapshot.state == CATRO_VOICE_STARTING ||
        snapshot.state == CATRO_VOICE_JOINED ||
        (room_mode_active_ &&
         room.state == CATRO_ROOM_CONNECTING);
    const bool joined =
        snapshot.state == CATRO_VOICE_JOINED &&
        room_ready;
    const bool another_participant_sharing =
        room_mode_active_ &&
        room.screen_owner[0] != '\0' &&
        std::string_view{room.screen_owner} !=
            room_peer_id_;

    bool share_active = false;
    if (screen_runtime_) {
        const auto share = screen_runtime_->snapshot();
        share_active =
            share.state == catro::screen::ScreenShareState::starting ||
            share.state == catro::screen::ScreenShareState::sharing;
    }
    const bool sharing_busy =
        workspace_state_.share_screen.availability ==
        catro::app::Availability::busy;
    const bool can_share =
        share_active ||
        (joined && !sharing_busy &&
         !another_participant_sharing);
    ShareScreenButton().IsEnabled(can_share);
    ShareScreenIconButton().IsEnabled(can_share);

    JoinVoiceButton().IsEnabled(
        workspace_state_.join_voice.availability !=
        catro::app::Availability::busy);
    const hstring join_label =
        workspace_state_.join_voice.availability ==
                catro::app::Availability::busy
            ? hstring{L"Joining"}
            : active ? hstring{L"Leave"}
               : (snapshot.state == CATRO_VOICE_FAILED ? hstring{L"Retry"} : hstring{L"Join"});
    JoinVoiceButton().Content(box_value(join_label));

    MuteVoiceButton().IsEnabled(joined && !deafened_);
    ProfileMuteButton().IsEnabled(joined && !deafened_);
    DeafenVoiceButton().IsEnabled(joined);
    ProfileDeafenButton().IsEnabled(joined);

    const bool effective_muted = muted_ || deafened_;
    const auto mute_tip = box_value(effective_muted ? hstring{L"Unmute"} : hstring{L"Mute"});
    const auto deafen_tip = box_value(deafened_ ? hstring{L"Undeafen"} : hstring{L"Deafen"});
    controls::ToolTipService::SetToolTip(MuteVoiceButton(), mute_tip);
    controls::ToolTipService::SetToolTip(ProfileMuteButton(), mute_tip);
    controls::ToolTipService::SetToolTip(DeafenVoiceButton(), deafen_tip);
    controls::ToolTipService::SetToolTip(ProfileDeafenButton(), deafen_tip);

    MuteVoiceButton().Opacity(effective_muted ? 1.0 : 0.72);
    ProfileMuteButton().Opacity(effective_muted ? 1.0 : 0.72);
    DeafenVoiceButton().Opacity(deafened_ ? 1.0 : 0.72);
    ProfileDeafenButton().Opacity(deafened_ ? 1.0 : 0.72);

    if (workspace_state_.join_voice.availability ==
        catro::app::Availability::busy) {
        VoiceStateText().Text(
            to_hstring(workspace_state_.join_voice.reason));
        return;
    }
    if (workspace_state_.join_voice.availability ==
        catro::app::Availability::failed) {
        VoiceStateText().Text(
            to_hstring(workspace_state_.join_voice.reason));
        return;
    }
    if (workspace_state_.share_screen.availability ==
        catro::app::Availability::failed) {
        VoiceStateText().Text(
            to_hstring(workspace_state_.share_screen.reason));
        return;
    }
    if (room_mode_active_ &&
        room.state == CATRO_ROOM_FAILED) {
        VoiceStateText().Text(L"Room connection error");
        if (room.error[0] != '\0') {
            controls::ToolTipService::SetToolTip(
                VoiceStateText(),
                box_value(
                    to_hstring(
                        std::string{room.error})));
        }
        return;
    }
    if (snapshot.state == CATRO_VOICE_FAILED) {
        VoiceStateText().Text(L"Voice error");
        if (snapshot.error[0] != '\0') {
            controls::ToolTipService::SetToolTip(VoiceStateText(), box_value(to_hstring(std::string(snapshot.error))));
        }
        if (voice_timer_) {
            voice_timer_.Stop();
        }
        return;
    }
    if (room_mode_active_ &&
        room.state == CATRO_ROOM_CONNECTING) {
        VoiceStateText().Text(L"Connecting room");
        return;
    }
    if (snapshot.state == CATRO_VOICE_STARTING) {
        VoiceStateText().Text(L"Joining");
        return;
    }
    if (!joined) {
        VoiceStateText().Text(L"Not joined");
        return;
    }

    if (deafened_) {
        VoiceStateText().Text(L"Deafened");
    } else if (muted_) {
        VoiceStateText().Text(L"Muted");
    } else if (
        room_mode_active_
            ? room.peer_count != 0
            : snapshot.peer_seen != 0) {
        VoiceStateText().Text(L"Connected");
    } else {
        VoiceStateText().Text(L"Waiting for peer");
    }
}


} // namespace winrt::Catro::implementation

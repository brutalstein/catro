#include "pch.h"

#include "Server/ServerView.xaml.h"
#include "Settings/Voice.hpp"

#include <cmath>

#include <mmsystem.h>

#include <algorithm>
#include <string>

#pragma comment(lib, "winmm.lib")

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

    ApplyActivityPolicy();
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
    const auto draft = Composer().Text();
    const bool has_text = std::wstring_view{draft}.find_first_not_of(L" \t\r\n") !=
                          std::wstring_view::npos;
    SendMessageButton().IsEnabled(text_active && configured && !sending && has_text);

    ApplyActivityPolicy();

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

void ServerView::ApplyVoicePreferences() {
    const auto version = catro::shell::voice_preferences_version();
    if (voice_runtime_ == nullptr || version == applied_voice_preferences_) {
        return;
    }
    applied_voice_preferences_ = version;
    const auto& preferences = catro::shell::voice_preferences();
    catro_voice_runtime_set_processing(voice_runtime_, preferences.echo_cancellation ? 1U : 0U,
                                       preferences.noise_suppression ? 1U : 0U,
                                       preferences.automatic_gain ? 1U : 0U);
    catro_voice_runtime_set_input_threshold(
        voice_runtime_, preferences.automatic_sensitivity ? NAN : preferences.sensitivity_db);
    catro::shell::sync_user_volumes(
        applied_user_volumes_, preferences.user_volumes, [this](const std::string& id, float volume) {
            catro_voice_runtime_set_user_volume(voice_runtime_, id.c_str(), volume);
        });
    if (preferences.input_device != applied_input_device_ ||
        preferences.output_device != applied_output_device_) {
        applied_input_device_ = preferences.input_device;
        applied_output_device_ = preferences.output_device;
        catro_voice_runtime_set_devices(
            voice_runtime_, applied_input_device_.c_str(), applied_output_device_.c_str());
    }
    ApplyStreamVolume();
}

void ServerView::PlayCue(wchar_t const* file) const {
    if (!catro::shell::voice_preferences().sounds) {
        return;
    }
    wchar_t windows[MAX_PATH]{};
    const auto length = GetWindowsDirectoryW(windows, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return;
    }
    const auto path = std::wstring{windows} + L"\\Media\\" + file;
    (void)PlaySoundW(path.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
}

void ServerView::ApplyStreamVolume() {
    const auto volume = catro::shell::voice_preferences().stream_volume;
    if (screen_runtime_) {
        screen_runtime_->set_stream_volume(
            deafened_ ? 0.0F : volume);
    }
    const auto percent = volume * 100.0;
    const hstring label{std::to_wstring(static_cast<int>(std::lround(percent))) + L"%"};
    StreamVolumePercent().Text(label);
    if (std::abs(StreamVolumeSlider().Value() - percent) > 0.01) {
        StreamVolumeSlider().Value(percent);
    }
    if (stream_window_volume_label_) {
        stream_window_volume_label_.Text(label);
    }
    if (stream_window_volume_slider_ && std::abs(stream_window_volume_slider_.Value() - percent) > 0.01) {
        stream_window_volume_slider_.Value(percent);
    }
}

void ServerView::UpdatePushToTalk(bool joined) {
    const auto& preferences = catro::shell::voice_preferences();
    const bool active = joined && preferences.push_to_talk && preferences.push_to_talk_key != 0;
    if (active == push_to_talk_timer_.IsRunning()) {
        return;
    }
    // Entering push-to-talk closes the microphone until the key goes down; leaving reopens it.
    push_to_talk_down_ = false;
    catro_voice_runtime_set_transmit(voice_runtime_, active ? 0U : 1U);
    if (active) {
        push_to_talk_timer_.Start();
    } else {
        push_to_talk_timer_.Stop();
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
    RenderMemberRows();
    ApplyVoicePreferences();

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
    UpdatePushToTalk(joined);
    // Discord-style cues with Windows' own sounds. A reconnect keeps the call, so it stays quiet.
    if (joined && !cue_joined_) {
        PlayCue(L"Speech On.wav");
        cue_joined_ = true;
        cue_peers_ = room.peer_count;
    } else if (!active && cue_joined_) {
        PlayCue(L"Speech Off.wav");
        cue_joined_ = false;
    } else if (joined && room_mode_active_ && room.peer_count != cue_peers_) {
        PlayCue(room.peer_count > cue_peers_ ? L"Windows Hardware Insert.wav"
                                             : L"Windows Hardware Remove.wav");
        cue_peers_ = room.peer_count;
    }
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

    // Active mute/deafen swaps to a crossed-out glyph and the rose style. Only touch the
    // properties when the state flips so the frequent snapshot refresh stays free.
    if (shown_muted_ != effective_muted || shown_deafened_ != deafened_) {
        shown_muted_ = effective_muted;
        shown_deafened_ = deafened_;
        const auto resources = xaml::Application::Current().Resources();
        const auto style = [&](wchar_t const* key) {
            return resources.Lookup(box_value(key)).as<xaml::Style>();
        };
        const hstring mic = effective_muted ? hstring{L"\xF781"} : hstring{L"\xE720"};
        const hstring speaker = deafened_ ? hstring{L"\xE74F"} : hstring{L"\xE7F6"};
        MuteVoiceIcon().Glyph(mic);
        ProfileMuteIcon().Glyph(mic);
        DeafenVoiceIcon().Glyph(speaker);
        ProfileDeafenIcon().Glyph(speaker);
        MuteVoiceButton().Style(style(effective_muted ? L"CatroDangerButtonStyle" : L"CatroSecondaryButtonStyle"));
        DeafenVoiceButton().Style(style(deafened_ ? L"CatroDangerButtonStyle" : L"CatroSecondaryButtonStyle"));
        ProfileMuteButton().Style(
            style(effective_muted ? L"CatroQuietDangerIconButtonStyle" : L"CatroQuietIconButtonStyle"));
        ProfileDeafenButton().Style(
            style(deafened_ ? L"CatroQuietDangerIconButtonStyle" : L"CatroQuietIconButtonStyle"));
    }

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
        VoiceStateText().Text(cue_joined_ ? L"Reconnecting…" : L"Connecting room");
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

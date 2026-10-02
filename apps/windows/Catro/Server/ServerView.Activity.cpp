#include "pch.h"
#include "Server/ServerView.xaml.h"
#include "Server/ServerView.RuntimeConfig.hpp"
#include "Settings/Performance.hpp"

namespace winrt::Catro::implementation {

namespace {

// Stopping a presentation timer never stops its underlying media session.
void schedule(
    Microsoft::UI::Dispatching::DispatcherQueueTimer const& timer,
    std::chrono::milliseconds interval, bool needed) {
    if (!timer) {
        return;
    }
    if (!needed || interval.count() == 0) {
        timer.Stop();
        return;
    }
    if (timer.Interval() != interval) {
        timer.Interval(interval);
    }
    if (!timer.IsRunning()) {
        timer.Start();
    }
}

} // namespace

void ServerView::SetWindowActivity(catro::shell::WindowActivity activity) {
    if (window_activity_ == activity) {
        return;
    }
    const auto previous = window_activity_;
    window_activity_ = activity;
    ApplyActivityPolicy();
    if (page_loaded_ && activity != catro::shell::WindowActivity::hidden) {
        UpdateVoiceUi();
        UpdateScreenShareUi();
        // Catch up immediately on return, rather than waiting for a background interval.
        if (activity == catro::shell::WindowActivity::foreground ||
            previous == catro::shell::WindowActivity::hidden) {
            BeginMessageRefresh();
            BeginMemberRefresh();
            BeginJoinRequestRefresh();
        }
    }
}

void ServerView::ApplyActivityPolicy() {
    const bool voice_channel =
        state_.active_channel_kind() == catro::community::ChannelKind::voice;
    const auto policy = catro::shell::ui_refresh_policy(
        window_activity_, page_loaded_, voice_channel, bool(stream_window_));
    const bool configured =
        directory_service_ && !directory_access_token_.empty() && directory_server_;

    schedule(message_timer_, policy.messages,
             configured && !voice_channel && !directory_server_->text_channel_id.empty());
    schedule(member_timer_, policy.roster, configured);
    schedule(access_timer_, policy.roster,
             configured && directory_server_->role == "owner" &&
                 !directory_server_->public_code.empty());

    const auto voice = voice_runtime_ ? catro_voice_runtime_snapshot(voice_runtime_)
                                      : CatroVoiceRuntimeSnapshot{};
    const auto room = room_mode_active_ && room_runtime_
        ? catro_room_runtime_snapshot(room_runtime_) : CatroRoomRuntimeSnapshot{};
    const bool voice_active =
        voice.state == CATRO_VOICE_STARTING || voice.state == CATRO_VOICE_JOINED ||
        room.state == CATRO_ROOM_CONNECTING;
    schedule(voice_timer_, policy.voice, voice_active);

    if (screen_runtime_) {
        const auto screen = screen_runtime_->snapshot();
        schedule(screen_timer_, policy.screen,
                 voice_active || screen.state != catro::screen::ScreenShareState::idle);
        const bool preview =
            policy.local_preview &&
            server_view_detail::environment("CATRO_LOCAL_PREVIEW").value_or(
                catro::shell::local_preview_preference() ? "1" : "0") != "0";
        local_preview_enabled_ = preview;
        screen_runtime_->set_local_preview_enabled(preview);
        if (!preview && attached_preview_swap_chain_) {
            DetachPreviewSwapChain();
        }
    }
}

} // namespace winrt::Catro::implementation

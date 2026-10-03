#include "pch.h"

#include "Server/ServerView.xaml.h"
#include "Server/ServerView.RuntimeConfig.hpp"

#include <chrono>
#include <string>
#include <utility>

namespace winrt::Catro::implementation {

namespace xaml = Microsoft::UI::Xaml;
namespace controls = Microsoft::UI::Xaml::Controls;
using namespace std::chrono_literals;
using server_view_detail::direct_video_config;
using server_view_detail::direct_voice_config;
using server_view_detail::environment;

void ServerView::OnJoinVoice(
    IInspectable const&,
    xaml::RoutedEventArgs const&) {
    if (voice_runtime_ == nullptr ||
        voice_join_pending_) {
        return;
    }
    const auto snapshot =
        catro_voice_runtime_snapshot(
            voice_runtime_);
    if (snapshot.state ==
            CATRO_VOICE_STARTING ||
        snapshot.state ==
            CATRO_VOICE_JOINED ||
        room_mode_active_) {
        StopVoice();
    } else {
        BeginVoiceJoin();
    }
}

void ServerView::OnMuteVoice(IInspectable const&, xaml::RoutedEventArgs const&) {
    if (voice_runtime_ == nullptr) {
        return;
    }
    muted_ = !muted_;
    catro_voice_runtime_set_muted(voice_runtime_, muted_ ? 1U : 0U);
    UpdateVoiceUi();
}

void ServerView::OnDeafenVoice(IInspectable const&, xaml::RoutedEventArgs const&) {
    if (voice_runtime_ == nullptr) {
        return;
    }
    deafened_ = !deafened_;
    catro_voice_runtime_set_deafened(voice_runtime_, deafened_ ? 1U : 0U);
    UpdateVoiceUi();
}

winrt::fire_and_forget
ServerView::BeginVoiceJoin() {
    auto lifetime = get_strong();
    if (voice_join_pending_ ||
        workspace_state_.join_voice.availability ==
            catro::app::Availability::busy ||
        voice_runtime_ == nullptr) {
        co_return;
    }

    if (!directory_service_ ||
        directory_access_token_.empty() ||
        !directory_server_) {
        const bool engineering_direct =
            environment("CATRO_ENGINEERING_DIRECT")
                    .value_or("") == "1" ||
            environment("CATRO_VOICE_SLOT")
                .has_value() ||
            environment("CATRO_VOICE_BIND")
                .has_value() ||
            environment("CATRO_VOICE_PEER")
                .has_value();
        if (engineering_direct) {
            workspace_state_.join_voice.enable();
            StartVoice(std::nullopt);
        } else {
            workspace_state_.join_voice.fail(
                "Online service unavailable");
            controls::ToolTipService::SetToolTip(
                VoiceStateText(),
                box_value(
                    hstring{
                        L"Catro could not establish its shared-server session. Check the packaged network configuration and service connectivity."}));
            UpdateVoiceUi();
        }
        co_return;
    }

    workspace_state_.join_voice.enable();
    (void)workspace_state_.join_voice.begin(
        "Authorizing room…");
    const auto generation = ++voice_join_generation_;
    voice_join_pending_ = true;
    UpdateVoiceUi();
    VoiceStateText().Text(
        L"Authorizing room…");

    const auto service =
        *directory_service_;
    const auto access_token =
        directory_access_token_;
    const auto server =
        *directory_server_;
    const auto requested_server_id =
        server.id;
    UiThread ui_thread;

    co_await winrt::resume_background();

    const auto result =
        catro::platform::windows::
            request_rtc_provisioning(
                service,
                access_token,
                server.id,
                server.voice_channel_id);

    if (const auto* failure =
            std::get_if<
                catro::platform::windows::
                    DirectoryError>(&result)) {
        const auto message =
            failure->message;
        co_await ui_thread;
        if (generation != lifetime->voice_join_generation_ ||
            !lifetime->directory_server_ ||
            lifetime->directory_server_->id !=
                requested_server_id) {
            co_return;
        }

        lifetime->voice_join_pending_ = false;
        lifetime->workspace_state_.join_voice.fail(
            "Room authorization failed: " + message);
        controls::ToolTipService::SetToolTip(
            lifetime->VoiceStateText(),
            box_value(to_hstring(message)));
        lifetime->UpdateVoiceUi();
        co_return;
    }

    auto provisioning =
        std::get<
            catro::platform::windows::
                RtcProvisioning>(result);

    co_await ui_thread;
    if (generation != lifetime->voice_join_generation_ ||
        !lifetime->directory_server_ ||
        lifetime->directory_server_->id !=
            requested_server_id) {
        co_return;
    }

    lifetime->voice_join_pending_ = false;
    lifetime->workspace_state_.join_voice.enable();
    lifetime->StartVoice(
        std::move(provisioning));
}

void ServerView::StartVoice(
    std::optional<
        catro::platform::windows::
            RtcProvisioning> provisioning) {
    if (voice_runtime_ == nullptr) {
        return;
    }

    const auto direct =
        direct_voice_config();
    const auto input =
        environment("CATRO_VOICE_INPUT");
    const auto output =
        environment("CATRO_VOICE_OUTPUT");

    room_mode_active_ = false;
    room_peer_id_.clear();
    if (room_runtime_ != nullptr) {
        catro_room_runtime_stop(
            room_runtime_);
    }

    if (provisioning) {
        if (room_runtime_ == nullptr ||
            provisioning->token.empty() ||
            provisioning->server_id.empty() ||
            provisioning->channel_id.empty() ||
            provisioning->peer_id.empty() ||
            provisioning->signaling_url.empty() ||
            provisioning->ice_servers.empty() ||
            provisioning->max_room_peers < 2 ||
            provisioning->max_room_peers > 5) {
            VoiceStateText().Text(
                L"RTC provisioning is incomplete");
            controls::ToolTipService::
                SetToolTip(
                    VoiceStateText(),
                    box_value(
                        hstring{
                            L"The Catro service returned incomplete RTC provisioning."}));
            UpdateVoiceUi();
            return;
        }

        std::vector<const char*>
            ice_urls;
        ice_urls.reserve(
            provisioning->
                ice_servers.size());
        for (const auto& url :
             provisioning->
                 ice_servers) {
            ice_urls.push_back(
                url.c_str());
        }

        const CatroRoomRuntimeConfig
            room_config{
                .signaling_url =
                    provisioning->
                        signaling_url
                        .c_str(),
                .access_token =
                    provisioning->
                        token.c_str(),
                .server_id =
                    provisioning->
                        server_id.c_str(),
                .channel_id =
                    provisioning->
                        channel_id.c_str(),
                .user_id =
                    provisioning->
                        peer_id.c_str(),
                .ice_server_urls =
                    ice_urls.data(),
                .ice_server_count =
                    ice_urls.size(),
                .max_remote_peers =
                    static_cast<std::uint32_t>(
                        provisioning->
                            max_room_peers - 1),
                .allow_insecure_signaling =
                    provisioning->
                            allow_insecure_signaling
                        ? 1U
                        : 0U,
                .allow_no_turn =
                    provisioning->
                            allow_no_turn
                        ? 1U
                        : 0U,
            };

        if (catro_room_runtime_start(
                room_runtime_,
                &room_config) != 0) {
            const auto room =
                catro_room_runtime_snapshot(
                    room_runtime_);
            VoiceStateText().Text(
                L"Room connection error");
            if (room.error[0] != '\0') {
                controls::ToolTipService::
                    SetToolTip(
                        VoiceStateText(),
                        box_value(
                            to_hstring(
                                std::string{
                                    room.error})));
            }
            UpdateVoiceUi();
            return;
        }
        room_peer_id_ =
            provisioning->peer_id;
        room_mode_active_ = true;
    }

    const CatroVoiceRuntimeConfig config{
        .room_runtime =
            room_mode_active_
                ? room_runtime_
                : nullptr,
        .bind_address =
            room_mode_active_
                ? nullptr
                : direct.bind.address.c_str(),
        .bind_port =
            room_mode_active_
                ? std::uint16_t{0}
                : static_cast<std::uint16_t>(
                      direct.bind.port),
        .peer_address =
            room_mode_active_
                ? nullptr
                : direct.peer.address.c_str(),
        .peer_port =
            room_mode_active_
                ? std::uint16_t{0}
                : static_cast<std::uint16_t>(
                      direct.peer.port),
        .stream_id = LocalStreamId(),
        .jitter_packets = 3,
        .bitrate = 48'000,
        .input_endpoint =
            input ? input->c_str() : nullptr,
        .output_endpoint =
            output ? output->c_str() : nullptr,
    };

    muted_ = false;
    deafened_ = false;
    ApplyVoicePreferences();
    if (catro_voice_runtime_start(
            voice_runtime_,
            &config) == 0) {
        ApplyActivityPolicy();

        if (screen_runtime_) {
            const auto video =
                direct_video_config();
            const catro::screen::
                ScreenTransportConfig
                transport{
                    .room_runtime =
                        room_mode_active_
                            ? room_runtime_
                            : nullptr,
                    .bind =
                        room_mode_active_
                            ? catro::transport::
                                  UdpEndpoint{}
                            : video.bind,
                    .peer =
                        room_mode_active_
                            ? catro::transport::
                                  UdpEndpoint{}
                            : video.peer,
                };
            if (const auto failure =
                    screen_runtime_->
                        start_listening(
                            transport)) {
                controls::ToolTipService::
                    SetToolTip(
                        ShareScreenButton(),
                        box_value(
                            to_hstring(
                                failure->
                                    message)));
            } else {
                ApplyActivityPolicy();
            }
        }
    } else if (room_mode_active_ &&
               room_runtime_ != nullptr) {
        catro_room_runtime_stop(
            room_runtime_);
        room_mode_active_ = false;
        room_peer_id_.clear();
    }

    UpdateVoiceUi();
    UpdateScreenShareUi();
}

void ServerView::StopVoice() {
    if (voice_runtime_ == nullptr) {
        return;
    }
    StopScreenShare();
    if (screen_runtime_) {
        screen_runtime_->set_remote_viewing_enabled(false);
        screen_runtime_->stop();
    }
    CloseStreamWindow();
    DetachRemoteSwapChain();
    RemoteShareHost().Visibility(xaml::Visibility::Collapsed);
    VoiceIdentityPanel().Visibility(xaml::Visibility::Visible);
    if (screen_timer_) {
        screen_timer_.Stop();
    }
    catro_voice_runtime_stop(voice_runtime_);
    if (room_runtime_ != nullptr) {
        catro_room_runtime_stop(room_runtime_);
    }
    room_mode_active_ = false;
    room_peer_id_.clear();
    workspace_state_.join_voice.enable();
    workspace_state_.share_screen.enable();
    muted_ = false;
    deafened_ = false;
    if (voice_timer_) {
        voice_timer_.Stop();
    }
    UpdateVoiceUi();
}


} // namespace winrt::Catro::implementation

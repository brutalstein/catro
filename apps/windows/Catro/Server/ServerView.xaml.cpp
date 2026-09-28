#include "pch.h"

#include "Server/ServerView.xaml.h"
#if __has_include("ServerView.g.cpp")
#include "ServerView.g.cpp"
#endif

#include <catro/platform/windows/screen_capture.hpp>
#include <catro/screen_runtime.hpp>
#include <catro/video/geometry.hpp>

#include <microsoft.ui.xaml.media.dxinterop.h>

#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Security.Authorization.AppCapabilityAccess.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Text.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace winrt::Catro::implementation {

namespace {

namespace xaml = Microsoft::UI::Xaml;
namespace controls = Microsoft::UI::Xaml::Controls;
using namespace std::chrono_literals;

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

    const auto horizontal =
        max_width / static_cast<double>(source_width);
    const auto vertical =
        max_height / static_cast<double>(source_height);
    // Presentation may scale both up and down; only the encoder is forbidden from upscaling.
    // This makes the viewing surface naturally fill the available window while preserving the
    // decoded source aspect ratio.
    const auto scale =
        std::min(horizontal, vertical);
    if (!std::isfinite(scale) || scale <= 0.0) {
        return {};
    }

    return ViewportSize{
        std::max(2.0, static_cast<double>(source_width) * scale),
        std::max(2.0, static_cast<double>(source_height) * scale),
    };
}

struct Endpoint {
    std::string address;
    std::uint16_t port = 0;
};

std::optional<std::string> environment(char const* name) {
    const auto required = GetEnvironmentVariableA(name, nullptr, 0);
    if (required == 0) {
        return std::nullopt;
    }

    std::string value(static_cast<std::size_t>(required), '\0');
    const auto written = GetEnvironmentVariableA(name, value.data(), required);
    if (written == 0 || written >= required) {
        return std::nullopt;
    }
    value.resize(static_cast<std::size_t>(written));
    return value;
}

struct ProductionRoomSettings {
    std::string signaling_url;
    std::string access_token;
    std::string server_id;
    std::string channel_id;
    std::string user_id;
    std::vector<std::string> ice_servers;
    bool allow_insecure_signaling = false;
    bool allow_no_turn = false;
};

[[nodiscard]] std::vector<std::string> split_ice_servers(
    std::string_view value) {
    std::vector<std::string> result;
    std::size_t begin = 0;
    while (begin < value.size()) {
        auto end = value.find_first_of(";,", begin);
        if (end == std::string_view::npos) {
            end = value.size();
        }
        auto item = value.substr(begin, end - begin);
        while (!item.empty() &&
               (item.front() == ' ' || item.front() == '\t')) {
            item.remove_prefix(1);
        }
        while (!item.empty() &&
               (item.back() == ' ' || item.back() == '\t')) {
            item.remove_suffix(1);
        }
        if (!item.empty()) {
            result.emplace_back(item);
        }
        begin = end + 1;
    }
    return result;
}

[[nodiscard]] std::optional<ProductionRoomSettings>
production_room_settings(
    const std::optional<catro::community::LocalState>& local_state) {
    const auto signaling =
        environment("CATRO_SIGNALING_URL");
    const auto token =
        environment("CATRO_ROOM_TOKEN");
    const auto ice =
        environment("CATRO_ICE_SERVERS");

    // No signaling URL means engineering direct-peer mode. Once production RTC is configured,
    // fail closed on incomplete credentials rather than silently falling back to localhost UDP.
    if (!signaling) {
        return std::nullopt;
    }
    if (!token || !ice || !local_state) {
        return ProductionRoomSettings{
            .signaling_url = *signaling,
        };
    }

    ProductionRoomSettings result;
    result.signaling_url = *signaling;
    result.access_token = *token;
    result.ice_servers =
        split_ice_servers(*ice);
    result.allow_insecure_signaling =
        environment("CATRO_ALLOW_INSECURE_RTC")
            .value_or("") == "1";
    result.allow_no_turn =
        environment("CATRO_ALLOW_NO_TURN")
            .value_or("") == "1";

    result.server_id =
        environment("CATRO_SERVER_ID")
            .value_or(
                catro::community::to_hex(
                    local_state->personal_server.id));
    result.user_id =
        environment("CATRO_USER_ID")
            .value_or(
                catro::community::to_hex(
                    local_state->identity.id));

    if (const auto override_channel =
            environment("CATRO_VOICE_CHANNEL_ID")) {
        result.channel_id = *override_channel;
    } else {
        for (const auto& channel :
             local_state->personal_server.channels) {
            if (channel.kind ==
                catro::community::ChannelKind::voice) {
                result.channel_id =
                    catro::community::to_hex(
                        channel.id);
                break;
            }
        }
    }
    return result;
}

std::optional<Endpoint> parse_endpoint(std::string_view value) {
    const auto separator = value.rfind(':');
    if (separator == std::string_view::npos || separator == 0 || separator + 1 >= value.size()) {
        return std::nullopt;
    }

    unsigned port = 0;
    const auto port_text = value.substr(separator + 1);
    const auto [end, error] =
        std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
    if (error != std::errc{} || end != port_text.data() + port_text.size() ||
        port == 0 || port > 65535) {
        return std::nullopt;
    }
    return Endpoint{std::string(value.substr(0, separator)), static_cast<std::uint16_t>(port)};
}

struct DirectVoiceConfig {
    Endpoint bind;
    Endpoint peer;
};

DirectVoiceConfig direct_voice_config() {
    // Temporary signaling boundary: product voice uses the proven direct UDP media path while the
    // invite/rendezvous service is still absent. Slot 1/2 makes two local app instances testable
    // without adding engineering fields to the Discord-like product UI.
    const bool slot_two = environment("CATRO_VOICE_SLOT").value_or("") == "2";
    DirectVoiceConfig config{
        .bind = {"127.0.0.1", static_cast<std::uint16_t>(slot_two ? 50001 : 50000)},
        .peer = {"127.0.0.1", static_cast<std::uint16_t>(slot_two ? 50000 : 50001)},
    };

    if (const auto value = environment("CATRO_VOICE_BIND")) {
        if (const auto parsed = parse_endpoint(*value)) {
            config.bind = *parsed;
        }
    }
    if (const auto value = environment("CATRO_VOICE_PEER")) {
        if (const auto parsed = parse_endpoint(*value)) {
            config.peer = *parsed;
        }
    }
    return config;
}

struct DirectVideoConfig {
    catro::transport::UdpEndpoint bind;
    catro::transport::UdpEndpoint peer;
};

DirectVideoConfig direct_video_config() {
    const auto explicit_slot = environment("CATRO_VIDEO_SLOT");
    const auto voice_slot = environment("CATRO_VOICE_SLOT");
    const bool slot_two =
        explicit_slot.value_or(voice_slot.value_or("")) == "2";

    DirectVideoConfig config{
        .bind = {"127.0.0.1", static_cast<std::uint16_t>(slot_two ? 55001 : 55000)},
        .peer = {"127.0.0.1", static_cast<std::uint16_t>(slot_two ? 55000 : 55001)},
    };

    if (const auto value = environment("CATRO_VIDEO_BIND")) {
        if (const auto parsed = parse_endpoint(*value)) {
            config.bind = {parsed->address, parsed->port};
        }
    }
    if (const auto value = environment("CATRO_VIDEO_PEER")) {
        if (const auto parsed = parse_endpoint(*value)) {
            config.peer = {parsed->address, parsed->port};
        }
    }
    return config;
}

std::wstring capture_source_label(
    const catro::platform::windows::CaptureSource& source) {
    std::wstring label =
        source.kind == catro::platform::windows::CaptureSourceKind::display
            ? L"Display — "
            : L"Window — ";
    if (source.fullscreen_like &&
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
    Loaded([this](auto&&, auto&&) {
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
    });
    Unloaded([this](auto&&, auto&&) {
        // Voice and screen share are room state, not page state. Navigating to Settings/System must
        // not disconnect either media worker. Suspend only presentation/polling work that cannot be
        // seen while this page is unloaded; transport and encode continue uninterrupted.
        if (voice_timer_) {
            voice_timer_.Stop();
        }
        if (screen_timer_) {
            screen_timer_.Stop();
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
    UpdateVoiceUi();
    UpdateScreenShareUi();
}

void ServerView::SetLocalState(const catro::community::LocalState& state) {
    local_state_ = state;
    ServerName().Text(to_hstring(state.personal_server.name));
    ProfileName().Text(to_hstring(state.identity.display_name));
    VoiceLocalName().Text(to_hstring(state.identity.display_name));
    MemberLocalName().Text(to_hstring(state.identity.display_name));
    std::wstring member_count = L"MEMBERS \u2014 ";
    member_count += std::to_wstring(state.personal_server.members.size());
    MemberCountLabel().Text(hstring{member_count});

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

void ServerView::OnJoinVoice(IInspectable const&, xaml::RoutedEventArgs const&) {
    if (voice_runtime_ == nullptr) {
        return;
    }
    const auto snapshot = catro_voice_runtime_snapshot(voice_runtime_);
    if (snapshot.state == CATRO_VOICE_STARTING || snapshot.state == CATRO_VOICE_JOINED) {
        StopVoice();
    } else {
        StartVoice();
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
        screen_timer_.Start();
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
    ChannelsColumn().Width(xaml::GridLengthHelper::FromPixels(width >= 760.0 ? 232.0 : 196.0));
    UpdateScreenShareUi();
}

void ServerView::StartVoice() {
    if (voice_runtime_ == nullptr) {
        return;
    }

    const auto direct = direct_voice_config();
    const auto input = environment("CATRO_VOICE_INPUT");
    const auto output = environment("CATRO_VOICE_OUTPUT");

    room_mode_active_ = false;
    if (room_runtime_ != nullptr) {
        catro_room_runtime_stop(room_runtime_);
    }

    const auto production =
        production_room_settings(local_state_);
    if (production) {
        if (room_runtime_ == nullptr ||
            production->access_token.empty() ||
            production->server_id.empty() ||
            production->channel_id.empty() ||
            production->user_id.empty() ||
            production->ice_servers.empty()) {
            VoiceStateText().Text(
                L"RTC configuration is incomplete");
            controls::ToolTipService::SetToolTip(
                VoiceStateText(),
                box_value(hstring{
                    L"CATRO_SIGNALING_URL, CATRO_ROOM_TOKEN and CATRO_ICE_SERVERS "
                    L"must be configured for production room voice."}));
            return;
        }

        std::vector<const char*> ice_urls;
        ice_urls.reserve(
            production->ice_servers.size());
        for (const auto& url :
             production->ice_servers) {
            ice_urls.push_back(url.c_str());
        }

        const CatroRoomRuntimeConfig room_config{
            .signaling_url =
                production->signaling_url.c_str(),
            .access_token =
                production->access_token.c_str(),
            .server_id =
                production->server_id.c_str(),
            .channel_id =
                production->channel_id.c_str(),
            .user_id =
                production->user_id.c_str(),
            .ice_server_urls = ice_urls.data(),
            .ice_server_count = ice_urls.size(),
            .allow_insecure_signaling =
                production->allow_insecure_signaling
                    ? 1U
                    : 0U,
            .allow_no_turn =
                production->allow_no_turn
                    ? 1U
                    : 0U,
        };

        if (catro_room_runtime_start(
                room_runtime_,
                &room_config) != 0) {
            const auto room =
                catro_room_runtime_snapshot(
                    room_runtime_);
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
                ? 0
                : direct.bind.port,
        .peer_address =
            room_mode_active_
                ? nullptr
                : direct.peer.address.c_str(),
        .peer_port =
            room_mode_active_
                ? 0
                : direct.peer.port,
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
    if (catro_voice_runtime_start(
            voice_runtime_, &config) == 0) {
        voice_timer_.Start();

        if (screen_runtime_) {
            const auto video =
                direct_video_config();
            const catro::screen::ScreenTransportConfig
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
                    screen_runtime_->start_listening(
                        transport)) {
                controls::ToolTipService::SetToolTip(
                    ShareScreenButton(),
                    box_value(
                        to_hstring(
                            failure->message)));
            } else {
                screen_timer_.Start();
            }
        }
    } else if (room_mode_active_ &&
               room_runtime_ != nullptr) {
        catro_room_runtime_stop(room_runtime_);
        room_mode_active_ = false;
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
    muted_ = false;
    deafened_ = false;
    if (voice_timer_) {
        voice_timer_.Stop();
    }
    UpdateVoiceUi();
}

void ServerView::UpdateVoiceUi() {
    if (voice_runtime_ == nullptr) {
        JoinVoiceButton().IsEnabled(false);
        ShareScreenButton().IsEnabled(false);
        ShareScreenIconButton().IsEnabled(false);
        VoiceStateText().Text(L"Unavailable");
        return;
    }

    const auto snapshot = catro_voice_runtime_snapshot(voice_runtime_);
    const bool active = snapshot.state == CATRO_VOICE_STARTING || snapshot.state == CATRO_VOICE_JOINED;
    const bool joined = snapshot.state == CATRO_VOICE_JOINED;

    bool share_active = false;
    if (screen_runtime_) {
        const auto share = screen_runtime_->snapshot();
        share_active =
            share.state == catro::screen::ScreenShareState::starting ||
            share.state == catro::screen::ScreenShareState::sharing;
    }
    const bool can_share =
        share_active || (joined && !share_dialog_open_);
    ShareScreenButton().IsEnabled(can_share);
    ShareScreenIconButton().IsEnabled(can_share);

    JoinVoiceButton().IsEnabled(true);
    const hstring join_label =
        active ? hstring{L"Leave"}
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
    } else if (snapshot.peer_seen != 0) {
        VoiceStateText().Text(L"Connected");
    } else {
        VoiceStateText().Text(L"Waiting for peer");
    }
}

winrt::fire_and_forget ServerView::BeginScreenShare() {
    [[maybe_unused]] auto lifetime = get_strong();
    share_dialog_open_ = true;
    UpdateVoiceUi();

    try {
        const auto sources =
            catro::platform::windows::enumerate_capture_sources();
        if (sources.empty()) {
            share_dialog_open_ = false;
            VoiceStateText().Text(L"No shareable windows or displays");
            UpdateVoiceUi();
            co_return;
        }

        controls::ContentDialog dialog;
        dialog.XamlRoot(XamlRoot());
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

        source_box.SelectionChanged(
            [&sources, browser_note, game_note](auto const& sender, auto const&) {
                const auto selected =
                    sender.as<controls::ComboBox>().SelectedIndex();
                const bool visible =
                    selected >= 0 &&
                    static_cast<std::size_t>(selected) < sources.size() &&
                    chromium_window(sources[static_cast<std::size_t>(selected)]);
                browser_note.Visibility(
                    visible ? xaml::Visibility::Visible
                            : xaml::Visibility::Collapsed);
                const bool game_visible =
                    selected >= 0 &&
                    static_cast<std::size_t>(selected) < sources.size() &&
                    catro::platform::windows::recommended_capture_backend(
                        sources[static_cast<std::size_t>(selected)]) ==
                        catro::platform::windows::ScreenCaptureBackend::desktop_duplication;
                game_note.Visibility(
                    game_visible ? xaml::Visibility::Visible
                                 : xaml::Visibility::Collapsed);
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
        fps_box.Value(30);
        form.Children().Append(fps_box);

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
            UpdateVoiceUi();
            co_return;
        }

        const auto selected = source_box.SelectedIndex();
        const auto width_value = width_box.Value();
        const auto height_value = height_box.Value();
        const auto fps_value = fps_box.Value();
        const auto bitrate_value = bitrate_box.Value();
        const bool request_borderless = borderless_box.IsOn();

        if (selected < 0 ||
            static_cast<std::size_t>(selected) >= sources.size() ||
            !std::isfinite(width_value) ||
            !std::isfinite(height_value) ||
            !std::isfinite(fps_value) ||
            !std::isfinite(bitrate_value)) {
            VoiceStateText().Text(L"Invalid screen-share settings");
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
            VoiceStateText().Text(L"Invalid screen-share settings");
            UpdateVoiceUi();
            co_return;
        }

        const auto& selected_source =
            sources[static_cast<std::size_t>(selected)];
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

        const auto direct = direct_video_config();
        catro::screen::ScreenShareConfig config;
        config.source = selected_source;
        config.borderless = borderless_allowed;
        config.bind = direct.bind;
        config.peer = direct.peer;
        config.max_width = width;
        config.max_height = height;
        config.fps = fps;
        config.bitrate = bitrate;
        config.ssrc = LocalStreamId() ^ 0x56494430U;
        if (config.ssrc == 0) {
            config.ssrc = 1;
        }

        if (const auto failure =
                screen_runtime_->start(config)) {
            VoiceStateText().Text(
                to_hstring(failure->message));
            UpdateVoiceUi();
            co_return;
        }

        SharePreviewHost().Visibility(xaml::Visibility::Visible);
        VoiceIdentityPanel().Visibility(xaml::Visibility::Collapsed);
        screen_timer_.Start();
        UpdateScreenShareUi();
        UpdateVoiceUi();
    } catch (const winrt::hresult_error& failure) {
        share_dialog_open_ = false;
        std::wstring message = L"Screen share UI error: ";
        message += failure.message().c_str();
        VoiceStateText().Text(hstring{message});
        UpdateVoiceUi();
    } catch (...) {
        share_dialog_open_ = false;
        VoiceStateText().Text(L"Screen share UI error");
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
                stream_window_ = nullptr;
                stream_window_fullscreen_ = false;
                UpdateScreenShareUi();
            });

        stream_window_.Content(stream_window_root_);
        stream_window_.Activate();

        if (!fullscreen) {
            const auto snapshot = screen_runtime_->snapshot();
            const auto source_width =
                snapshot.remote_width != 0
                    ? snapshot.remote_width
                    : 1280U;
            const auto source_height =
                snapshot.remote_height != 0
                    ? snapshot.remote_height
                    : 720U;
            const auto initial =
                fit_viewport(
                    source_width,
                    source_height,
                    1280.0,
                    760.0);
            stream_window_.AppWindow().Resize(
                {
                    static_cast<std::int32_t>(
                        std::max(640.0, initial.width)),
                    static_cast<std::int32_t>(
                        std::max(420.0, initial.height + 40.0)),
                });
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
        UpdateStreamWindowLayout();
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

    const auto source_width =
        snapshot.remote_width != 0
            ? snapshot.remote_width
            : 1280U;
    const auto source_height =
        snapshot.remote_height != 0
            ? snapshot.remote_height
            : 720U;
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
    stream_window_ = nullptr;
    stream_window_fullscreen_ = false;

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
    if (screen_runtime_) {
        screen_runtime_->stop_sharing();
    }
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

    if (stream_window_ && !remote_viewing) {
        CloseStreamWindow();
    }

    const auto panel_width = VoicePanel().ActualWidth();
    const auto panel_height = VoicePanel().ActualHeight();
    const auto max_stream_width =
        panel_width > 80.0
            ? std::min(960.0, std::max(160.0, panel_width - 44.0))
            : 720.0;
    const auto max_stream_height =
        panel_height > 140.0
            ? std::min(720.0, std::max(120.0, panel_height - 120.0))
            : 405.0;

    ShareScreenButton().Content(
        box_value(local_active ? hstring{L"Stop sharing"}
                               : hstring{L"Share screen"}));
    controls::ToolTipService::SetToolTip(
        ShareScreenIconButton(),
        box_value(local_active ? hstring{L"Stop sharing"}
                               : hstring{L"Share screen"}));

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
                snapshot.remote_width != 0
                    ? snapshot.remote_width
                    : 1280U,
                snapshot.remote_height != 0
                    ? snapshot.remote_height
                    : 720U,
                max_stream_width,
                max_stream_height);
        if (remote_size.width > 0.0 &&
            remote_size.height > 0.0) {
            RemoteShareViewport().Width(remote_size.width);
            RemoteShareViewport().Height(remote_size.height);
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

        const auto local_swap =
            screen_runtime_->preview_swap_chain();
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

        if (snapshot.encoded_width != 0 &&
            snapshot.encoded_height != 0) {
            std::wstring meta =
                std::to_wstring(snapshot.encoded_width);
            meta += L"×";
            meta += std::to_wstring(snapshot.encoded_height);
            meta += L"  ·  LIVE";
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
                    max_stream_width,
                    max_stream_height);
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
    const auto direct = direct_voice_config();
    value ^= static_cast<std::uint32_t>(direct.bind.port);
    return value == 0 ? 1U : value;
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
}

} // namespace winrt::Catro::implementation

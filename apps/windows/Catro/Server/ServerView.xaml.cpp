#include "pch.h"

#include "Server/ServerView.xaml.h"
#if __has_include("ServerView.g.cpp")
#include "ServerView.g.cpp"
#endif

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace winrt::Catro::implementation {
namespace {

namespace xaml = Microsoft::UI::Xaml;
namespace controls = Microsoft::UI::Xaml::Controls;
using namespace std::chrono_literals;

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

} // namespace

ServerView::~ServerView() {
    if (voice_timer_) {
        voice_timer_.Stop();
    }
    if (voice_runtime_ != nullptr) {
        catro_voice_runtime_destroy(voice_runtime_);
        voice_runtime_ = nullptr;
    }
}

void ServerView::InitializeComponent() {
    ServerViewT<ServerView>::InitializeComponent();
    voice_runtime_ = catro_voice_runtime_create();

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
        UpdateVoiceUi();
    });
    Unloaded([this](auto&&, auto&&) {
        // Voice is room state, not page state. Navigating to Settings/System must not disconnect.
        if (voice_timer_) {
            voice_timer_.Stop();
        }
    });

    ShowChannel("general");
    UpdateVoiceUi();
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

void ServerView::OnSizeChanged(IInspectable const&, xaml::SizeChangedEventArgs const& args) {
    const auto width = args.NewSize().Width;
    const bool show_members = width >= 920.0;
    MembersColumn().Width(xaml::GridLengthHelper::FromPixels(show_members ? 216.0 : 0.0));
    MembersPane().Visibility(show_members ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    ChannelsColumn().Width(xaml::GridLengthHelper::FromPixels(width >= 760.0 ? 232.0 : 196.0));
}

void ServerView::StartVoice() {
    if (voice_runtime_ == nullptr) {
        return;
    }

    const auto direct = direct_voice_config();
    const CatroVoiceRuntimeConfig config{
        .bind_address = direct.bind.address.c_str(),
        .bind_port = direct.bind.port,
        .peer_address = direct.peer.address.c_str(),
        .peer_port = direct.peer.port,
        .stream_id = LocalStreamId(),
        .jitter_packets = 3,
        .bitrate = 48'000,
    };

    muted_ = false;
    deafened_ = false;
    if (catro_voice_runtime_start(voice_runtime_, &config) == 0) {
        voice_timer_.Start();
    }
    UpdateVoiceUi();
}

void ServerView::StopVoice() {
    if (voice_runtime_ == nullptr) {
        return;
    }
    catro_voice_runtime_stop(voice_runtime_);
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
        VoiceStateText().Text(L"Unavailable");
        return;
    }

    const auto snapshot = catro_voice_runtime_snapshot(voice_runtime_);
    const bool active = snapshot.state == CATRO_VOICE_STARTING || snapshot.state == CATRO_VOICE_JOINED;
    const bool joined = snapshot.state == CATRO_VOICE_JOINED;

    JoinVoiceButton().IsEnabled(true);
    JoinVoiceButton().Content(box_value(active ? L"Leave" : (snapshot.state == CATRO_VOICE_FAILED ? L"Retry" : L"Join")));

    MuteVoiceButton().IsEnabled(joined && !deafened_);
    ProfileMuteButton().IsEnabled(joined && !deafened_);
    DeafenVoiceButton().IsEnabled(joined);
    ProfileDeafenButton().IsEnabled(joined);

    const bool effective_muted = muted_ || deafened_;
    controls::ToolTipService::SetToolTip(MuteVoiceButton(), box_value(effective_muted ? L"Unmute" : L"Mute"));
    controls::ToolTipService::SetToolTip(ProfileMuteButton(), box_value(effective_muted ? L"Unmute" : L"Mute"));
    controls::ToolTipService::SetToolTip(DeafenVoiceButton(), box_value(deafened_ ? L"Undeafen" : L"Deafen"));
    controls::ToolTipService::SetToolTip(ProfileDeafenButton(), box_value(deafened_ ? L"Undeafen" : L"Deafen"));

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

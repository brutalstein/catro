#include "pch.h"

#include "Server/ServerView.xaml.h"
#if __has_include("ServerView.g.cpp")
#include "ServerView.g.cpp"
#endif

namespace winrt::Catro::implementation {
namespace {

namespace xaml = Microsoft::UI::Xaml;

} // namespace

void ServerView::InitializeComponent() {
    ServerViewT<ServerView>::InitializeComponent();
    ShowChannel("general");
}

void ServerView::OnTextChannel(IInspectable const&, xaml::RoutedEventArgs const&) {
    ShowChannel("general");
}

void ServerView::OnVoiceChannel(IInspectable const&, xaml::RoutedEventArgs const&) {
    ShowChannel("voice");
}

void ServerView::OnSizeChanged(IInspectable const&, xaml::SizeChangedEventArgs const& args) {
    const auto width = args.NewSize().Width;
    const bool show_members = width >= 920.0;
    MembersColumn().Width(xaml::GridLengthHelper::FromPixels(show_members ? 216.0 : 0.0));
    MembersPane().Visibility(show_members ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    ChannelsColumn().Width(xaml::GridLengthHelper::FromPixels(width >= 760.0 ? 232.0 : 196.0));
}

void ServerView::ShowChannel(std::string_view id) {
    if (!catro::app::channel_kind(id)) {
        return;
    }
    (void)state_.select_channel(id);
    const bool voice = state_.active_channel_kind() == catro::app::ChannelKind::voice;

    TextSelection().Visibility(voice ? xaml::Visibility::Collapsed : xaml::Visibility::Visible);
    VoiceSelection().Visibility(voice ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    TextPanel().Visibility(voice ? xaml::Visibility::Collapsed : xaml::Visibility::Visible);
    VoicePanel().Visibility(voice ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    VoiceToolbar().Visibility(voice ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    TextChannelGlyph().Visibility(voice ? xaml::Visibility::Collapsed : xaml::Visibility::Visible);
    VoiceChannelGlyph().Visibility(voice ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    ChannelTitle().Text(voice ? L"Voice" : L"general");
}

} // namespace winrt::Catro::implementation

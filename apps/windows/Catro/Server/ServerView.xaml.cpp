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
    ChannelGlyph().Text(voice ? L"\xE720" : L"#");
    ChannelTitle().Text(voice ? L"Voice" : L"general");
}

} // namespace winrt::Catro::implementation

#pragma once

#include "ServerView.g.h"

#include <ShellModel.hpp>

namespace winrt::Catro::implementation {

struct ServerView : ServerViewT<ServerView> {
    ServerView() = default;

    void InitializeComponent();
    void OnTextChannel(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnVoiceChannel(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnSizeChanged(IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const&);

private:
    void ShowChannel(std::string_view id);

    catro::app::ShellState state_;
};

} // namespace winrt::Catro::implementation

namespace winrt::Catro::factory_implementation {
struct ServerView : ServerViewT<ServerView, implementation::ServerView> {};
} // namespace winrt::Catro::factory_implementation

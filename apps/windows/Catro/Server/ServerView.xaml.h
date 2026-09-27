#pragma once

#include "ServerView.g.h"

#include <ShellModel.hpp>

#include <catro/community/model.hpp>

#include <optional>

namespace winrt::Catro::implementation {

struct ServerView : ServerViewT<ServerView> {
    ServerView() = default;

    void InitializeComponent();
    void OnTextChannel(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnVoiceChannel(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnSizeChanged(IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const&);
    void SetLocalState(const catro::community::LocalState& state);

private:
    void ShowChannel(std::string_view id);

    catro::app::ShellState state_;
    std::optional<catro::community::LocalState> local_state_;
};

} // namespace winrt::Catro::implementation

namespace winrt::Catro::factory_implementation {
struct ServerView : ServerViewT<ServerView, implementation::ServerView> {};
} // namespace winrt::Catro::factory_implementation

#pragma once

#include "ServerView.g.h"

#include <ShellModel.hpp>

#include <catro/community/model.hpp>
#include <catro/screen_runtime.hpp>
#include <catro/voice_runtime.h>

#include <memory>
#include <optional>

namespace winrt::Catro::implementation {

struct ServerView : ServerViewT<ServerView> {
    ServerView() = default;
    ~ServerView();

    void InitializeComponent();
    void OnTextChannel(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnVoiceChannel(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnJoinVoice(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnMuteVoice(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnDeafenVoice(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnShareScreen(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnSizeChanged(IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const&);
    void SetLocalState(const catro::community::LocalState& state);

private:
    void ShowChannel(std::string_view id);
    void StartVoice();
    void StopVoice();
    void UpdateVoiceUi();
    winrt::fire_and_forget BeginScreenShare();
    void StopScreenShare() noexcept;
    void UpdateScreenShareUi();
    void DetachPreviewSwapChain() noexcept;
    [[nodiscard]] std::uint32_t LocalStreamId() const noexcept;

    catro::app::ShellState state_;
    std::optional<catro::community::LocalState> local_state_;
    CatroVoiceRuntimeHandle voice_runtime_ = nullptr;
    Microsoft::UI::Dispatching::DispatcherQueueTimer voice_timer_{nullptr};
    std::unique_ptr<catro::screen::WindowsScreenShareRuntime> screen_runtime_;
    Microsoft::UI::Dispatching::DispatcherQueueTimer screen_timer_{nullptr};
    Microsoft::WRL::ComPtr<IDXGISwapChain1> attached_preview_swap_chain_;
    bool share_dialog_open_ = false;
    bool muted_ = false;
    bool deafened_ = false;
};

} // namespace winrt::Catro::implementation

namespace winrt::Catro::factory_implementation {
struct ServerView : ServerViewT<ServerView, implementation::ServerView> {};
} // namespace winrt::Catro::factory_implementation

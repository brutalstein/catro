#pragma once

#include "ServerView.g.h"

#include <PresentationState.hpp>
#include <ShellModel.hpp>

#include <catro/community/model.hpp>
#include <catro/platform/windows/directory_client.hpp>
#include <catro/room_runtime.h>
#include <catro/screen_runtime.hpp>
#include <catro/voice_runtime.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace winrt::Catro::implementation {

struct ServerView : ServerViewT<ServerView> {
    ServerView() = default;
    ~ServerView();

    void InitializeComponent();
    void OnTextChannel(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnVoiceChannel(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnComposerKeyDown(
        IInspectable const&,
        Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);
    void OnSendMessage(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnJoinVoice(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnInvite(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnAccess(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnMuteVoice(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnDeafenVoice(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnShareScreen(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnWatchStream(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnLeaveStream(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnPopOutStream(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnFullScreenStream(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnSizeChanged(IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const&);
    void SetLocalState(const catro::community::LocalState& state);
    void SetDirectorySession(
        const catro::platform::windows::DirectoryServiceConfig& service,
        std::string access_token,
        const catro::platform::windows::DirectoryServer& server);

private:
    void ShowChannel(std::string_view id);
    void ResetMembers();
    void ShowLocalMemberFallback();
    void ApplyMemberRoster(
        const std::vector<catro::platform::windows::DirectoryMember>& members);
    winrt::fire_and_forget BeginMemberRefresh();
    void ResetAccessRequests();
    void UpdateAccessUi();
    winrt::fire_and_forget BeginJoinRequestRefresh();
    winrt::fire_and_forget ShowAccessDialog();
    winrt::fire_and_forget BeginJoinRequestDecision(
        std::string request_id,
        bool approve);
    void ResetMessages();
    void UpdateMessageUi();
    void AppendMessages(
        const std::vector<catro::platform::windows::DirectoryMessage>& messages);
    void AppendMessage(
        const catro::platform::windows::DirectoryMessage& message);
    winrt::fire_and_forget BeginMessageRefresh();
    winrt::fire_and_forget BeginSendMessage();
    winrt::fire_and_forget BeginVoiceJoin();
    winrt::fire_and_forget BeginInvite();
    winrt::fire_and_forget ShowInviteCode(std::string code);
    void StartVoice(
        std::optional<catro::platform::windows::RtcProvisioning> provisioning);
    void StopVoice();
    void UpdateVoiceUi();
    winrt::fire_and_forget BeginScreenShare();
    winrt::Windows::Foundation::IAsyncOperation<bool>
    ClaimScreenOwnership();
    void StopScreenShare();
    void UpdateScreenShareUi();
    void UpdateOnlineStatus();
    void OpenStreamWindow(bool fullscreen);
    void CloseStreamWindow() noexcept;
    void UpdateStreamWindowLayout();
    void SetStreamWindowFullscreen(bool fullscreen);
    void SetStreamWindowAlwaysOnTop(bool enabled);
    void DetachPreviewSwapChain() noexcept;
    void DetachRemoteSwapChain() noexcept;
    [[nodiscard]] std::uint32_t LocalStreamId() const noexcept;

    catro::app::ShellState state_;
    catro::app::WorkspaceSnapshot workspace_state_;
    std::optional<catro::community::LocalState> local_state_;
    std::optional<
        catro::platform::windows::DirectoryServiceConfig>
        directory_service_;
    std::string directory_access_token_;
    std::optional<
        catro::platform::windows::DirectoryServer>
        directory_server_;

    CatroRoomRuntimeHandle room_runtime_ = nullptr;
    std::string room_peer_id_;
    CatroVoiceRuntimeHandle voice_runtime_ = nullptr;
    Microsoft::UI::Dispatching::DispatcherQueueTimer voice_timer_{nullptr};
    Microsoft::UI::Dispatching::DispatcherQueueTimer message_timer_{nullptr};
    Microsoft::UI::Dispatching::DispatcherQueueTimer member_timer_{nullptr};
    Microsoft::UI::Dispatching::DispatcherQueueTimer access_timer_{nullptr};
    std::unique_ptr<catro::screen::WindowsScreenShareRuntime> screen_runtime_;
    Microsoft::UI::Dispatching::DispatcherQueueTimer screen_timer_{nullptr};
    ::Microsoft::WRL::ComPtr<IDXGISwapChain1> attached_preview_swap_chain_;
    ::Microsoft::WRL::ComPtr<IDXGISwapChain1> attached_remote_swap_chain_;

    Microsoft::UI::Xaml::Window stream_window_{nullptr};
    Microsoft::UI::Xaml::Controls::Grid stream_window_root_{nullptr};
    Microsoft::UI::Xaml::Controls::Border stream_window_viewport_{nullptr};
    Microsoft::UI::Xaml::Controls::SwapChainPanel stream_window_swap_chain_panel_{nullptr};
    Microsoft::UI::Xaml::Controls::Button stream_window_mode_button_{nullptr};
    Microsoft::UI::Xaml::Controls::Button stream_window_topmost_button_{nullptr};
    ::Microsoft::WRL::ComPtr<IDXGISwapChain1> stream_window_swap_chain_;
    bool stream_window_fullscreen_ = false;
    bool stream_window_topmost_ = false;

    std::uint64_t member_generation_ = 1;
    std::uint64_t member_refresh_generation_ = 0;
    std::uint64_t access_generation_ = 1;
    std::uint64_t access_refresh_generation_ = 0;
    std::vector<catro::platform::windows::DirectoryJoinRequest>
        pending_join_requests_;
    std::uint64_t message_cursor_ = 0;
    std::uint64_t message_generation_ = 1;
    std::uint64_t message_refresh_generation_ = 0;
    std::uint64_t message_send_generation_ = 0;
    std::uint64_t voice_join_generation_ = 0;
    std::uint64_t invite_generation_ = 0;
    bool page_loaded_ = false;
    bool member_refresh_pending_ = false;
    bool access_refresh_pending_ = false;
    bool access_decision_pending_ = false;
    bool access_dialog_open_ = false;
    bool message_refresh_pending_ = false;
    bool message_send_pending_ = false;
    bool room_mode_active_ = false;
    bool voice_join_pending_ = false;
    bool invite_pending_ = false;
    bool share_dialog_open_ = false;
    bool muted_ = false;
    bool deafened_ = false;
};

} // namespace winrt::Catro::implementation

namespace winrt::Catro::factory_implementation {
struct ServerView : ServerViewT<ServerView, implementation::ServerView> {};
} // namespace winrt::Catro::factory_implementation

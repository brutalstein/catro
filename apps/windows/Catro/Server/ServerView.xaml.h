#pragma once

#include "ServerView.g.h"

#include <PresentationState.hpp>
#include <ShellModel.hpp>
#include "UiActivityPolicy.hpp"
#include "Server/ChatTimeline.hpp"

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
    void OnComposerChanged(IInspectable const&, Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
    void OnJoinVoice(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnInvite(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnAccess(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnMuteVoice(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnDeafenVoice(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnShareScreen(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnLocalPreviewChanged(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnWatchStream(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnLeaveStream(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnPopOutStream(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnFullScreenStream(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnRemoteStreamDoubleTapped(IInspectable const&, Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const&);
    void OnLocalStreamDoubleTapped(IInspectable const&, Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const&);
    void OnLocalFullScreen(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnExitStage(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnStageEscape(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
                       Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const&);
    void OnSwallowDoubleTap(IInspectable const&, Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const&);
    void OnStagePointerMoved(IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
    void OnStreamVolumeChanged(
        IInspectable const&, Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&);
    void OnSizeChanged(IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const&);
    void OnMessageContainerChanging(
        Microsoft::UI::Xaml::Controls::ListViewBase const&,
        Microsoft::UI::Xaml::Controls::ContainerContentChangingEventArgs const&);
    void OnMemberContainerChanging(
        Microsoft::UI::Xaml::Controls::ListViewBase const&,
        Microsoft::UI::Xaml::Controls::ContainerContentChangingEventArgs const&);
    void OnMemberClick(IInspectable const&, Microsoft::UI::Xaml::Controls::ItemClickEventArgs const&);
    void SetLocalState(const catro::community::LocalState& state);
    void SetWindowActivity(catro::shell::WindowActivity activity);
    void SetDirectorySession(
        const catro::platform::windows::DirectoryServiceConfig& service,
        std::string access_token,
        const catro::platform::windows::DirectoryServer& server);

private:
    void ApplyActivityPolicy();
    void ShowChannel(std::string_view id);
    void ResetMembers();
    void ShowLocalMemberFallback();
    void ApplyMemberRoster(
        const std::vector<catro::platform::windows::DirectoryMember>& members);
    // Rebuilds member rows with live speaking state; unchanged rows are left alone.
    void RenderMemberRows();
    // Sends saved voice preferences to the runtime when they changed since the last call.
    void ApplyVoicePreferences();
    void UpdatePushToTalk(bool joined);
    // Plays a Windows sound from %WINDIR%\Media unless voice sounds are off.
    void PlayCue(wchar_t const* file) const;
    // Watched stream audio follows the saved volume, and deafen silences it.
    void ApplyStreamVolume();
    Microsoft::UI::Xaml::Controls::Flyout MemberVolumeFlyout();
    Microsoft::UI::Xaml::Controls::Flyout MessageAdminFlyout();
    winrt::fire_and_forget ConfirmRemoveMember(std::string user_id);
    winrt::fire_and_forget ConfirmDeleteMessage(std::string message_id);
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
    void ApplyServerLayout();
    // In-app full screen: the stream fills the server page; `local` shows your own stream.
    void SetStage(bool enabled, bool local);
    void ShowStageControls();
    void OpenStreamWindow();
    void CloseStreamWindow() noexcept;
    void ReleaseStreamWindow() noexcept;
    void UpdateStreamWindowLayout();
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
    // Push-to-talk reads the key every 15 ms only while joined in push-to-talk mode.
    Microsoft::UI::Dispatching::DispatcherQueueTimer push_to_talk_timer_{nullptr};
    std::uint32_t applied_voice_preferences_ = ~0U;
    std::map<std::string, float> applied_user_volumes_;
    bool push_to_talk_down_ = false;
    // Devices last sent to the voice runtime, so unrelated preference saves never reopen audio.
    std::string applied_input_device_;
    std::string applied_output_device_;
    // Call state behind join/leave sounds and the reconnecting label.
    bool cue_joined_ = false;
    std::uint32_t cue_peers_ = 0;
    Microsoft::UI::Dispatching::DispatcherQueueTimer message_timer_{nullptr};
    Microsoft::UI::Dispatching::DispatcherQueueTimer member_timer_{nullptr};
    Microsoft::UI::Dispatching::DispatcherQueueTimer access_timer_{nullptr};
    std::unique_ptr<catro::screen::WindowsScreenShareRuntime> screen_runtime_;
    Microsoft::UI::Dispatching::DispatcherQueueTimer screen_timer_{nullptr};
    bool shown_muted_ = false;
    bool shown_deafened_ = false;
    ::Microsoft::WRL::ComPtr<IDXGISwapChain1> attached_preview_swap_chain_;
    ::Microsoft::WRL::ComPtr<IDXGISwapChain1> attached_remote_swap_chain_;

    Microsoft::UI::Xaml::Window stream_window_{nullptr};
    Microsoft::UI::Xaml::Controls::Grid stream_window_root_{nullptr};
    Microsoft::UI::Xaml::Controls::Border stream_window_viewport_{nullptr};
    Microsoft::UI::Xaml::Controls::SwapChainPanel stream_window_swap_chain_panel_{nullptr};
    Microsoft::UI::Xaml::Controls::Button stream_window_topmost_button_{nullptr};
    Microsoft::UI::Xaml::Controls::Slider stream_window_volume_slider_{nullptr};
    Microsoft::UI::Xaml::Controls::TextBlock stream_window_volume_label_{nullptr};
    ::Microsoft::WRL::ComPtr<IDXGISwapChain1> stream_window_swap_chain_;
    bool stream_window_topmost_ = false;
    bool stage_active_ = false;
    bool stage_local_ = false;
    // Full screen hides the stream controls while the mouse rests, like Discord.
    Microsoft::UI::Dispatching::DispatcherQueueTimer stage_idle_timer_{nullptr};

    std::vector<catro::platform::windows::DirectoryMember> roster_;
    std::string member_volume_user_;
    Microsoft::UI::Xaml::Controls::Flyout member_volume_flyout_{nullptr};
    Microsoft::UI::Xaml::Controls::Button member_remove_button_{nullptr};
    std::string message_admin_id_;
    Microsoft::UI::Xaml::Controls::Flyout message_admin_flyout_{nullptr};
    std::uint64_t moderation_generation_ = 0;
    bool moderation_pending_ = false;
    std::uint64_t member_generation_ = 1;
    std::uint64_t member_refresh_generation_ = 0;
    std::uint64_t access_generation_ = 1;
    std::uint64_t access_refresh_generation_ = 0;
    std::vector<catro::platform::windows::DirectoryJoinRequest>
        pending_join_requests_;
    std::uint64_t message_cursor_ = 0;
    std::uint64_t message_revision_ = 0;
    std::optional<std::chrono::sys_days> message_display_day_;
    std::uint64_t message_generation_ = 1;
    std::uint64_t message_refresh_generation_ = 0;
    std::uint64_t message_send_generation_ = 0;
    std::uint64_t voice_join_generation_ = 0;
    std::uint64_t invite_generation_ = 0;
    bool page_loaded_ = false;
    bool local_preview_enabled_ = false;
    catro::shell::WindowActivity window_activity_ = catro::shell::WindowActivity::foreground;
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
    bool share_hides_catro_ = false;
    bool catro_hidden_from_capture_ = false;
    // Tallest frame this GPU's encoder accepted after refusing a larger one; 0 until then.
    std::uint32_t encoder_max_height_ = 0;
    bool muted_ = false;
    bool deafened_ = false;
};

} // namespace winrt::Catro::implementation

namespace winrt::Catro::factory_implementation {
struct ServerView : ServerViewT<ServerView, implementation::ServerView> {};
} // namespace winrt::Catro::factory_implementation

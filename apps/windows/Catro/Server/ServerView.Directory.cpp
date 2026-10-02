#include "pch.h"

#include "Server/ServerView.xaml.h"

#include <winrt/Windows.UI.Text.h>

#include <chrono>
#include <ctime>
#include <cwchar>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace winrt::Catro::implementation {

namespace {

namespace xaml = Microsoft::UI::Xaml;
namespace controls = Microsoft::UI::Xaml::Controls;
using namespace std::chrono_literals;

std::wstring message_time(std::int64_t unix_milliseconds) {
    if (unix_milliseconds <= 0) {
        return {};
    }
    const auto seconds = static_cast<std::time_t>(unix_milliseconds / 1000);
    std::tm local{};
    if (localtime_s(&local, &seconds) != 0) {
        return {};
    }
    wchar_t buffer[16]{};
    if (std::wcsftime(buffer, 16, L"%H:%M", &local) == 0) {
        return {};
    }
    return buffer;
}

// List rows are stored as line-separated fields so a recycled container can be refilled
// without bindings.
std::pair<std::wstring_view, std::wstring_view> split_line(std::wstring_view text) {
    const auto at = text.find(L'\n');
    if (at == std::wstring_view::npos) {
        return {text, {}};
    }
    return {text.substr(0, at), text.substr(at + 1)};
}

controls::TextBlock text_at(controls::Panel const& panel, uint32_t index) {
    return panel.Children().GetAt(index).as<controls::TextBlock>();
}

} // namespace

void ServerView::OnMessageContainerChanging(
    controls::ListViewBase const&,
    controls::ContainerContentChangingEventArgs const& args) {
    const auto root = args.ItemContainer().ContentTemplateRoot().try_as<controls::StackPanel>();
    if (args.InRecycleQueue() || !root) {
        return;
    }
    const auto row = unbox_value<hstring>(args.Item());
    const auto [author_id, content] = split_line(row);
    const auto [author, rest] = split_line(content);
    const auto [time, body] = split_line(rest);
    const auto header = root.Children().GetAt(0).as<controls::StackPanel>();
    text_at(header, 0).Text(hstring{author});
    constexpr wchar_t const* colors[] = {L"CatroChatCopperStyle", L"CatroChatSageStyle", L"CatroChatRoseStyle"};
    const auto color = author_id.empty() ? 0U : static_cast<unsigned>(author_id.back()) % 3U;
    text_at(header, 0).Style(xaml::Application::Current().Resources().Lookup(
        box_value(hstring{colors[color]})).as<xaml::Style>());
    text_at(header, 1).Text(hstring{time});
    text_at(root, 1).Text(hstring{body});
    // The row's internal author ID chooses a stable colour; Narrator reads the useful content only.
    std::wstring announcement{author};
    announcement += L" ";
    announcement += time;
    announcement += L" ";
    announcement += body;
    xaml::Automation::AutomationProperties::SetName(args.ItemContainer(), hstring{announcement});
    args.Handled(true);
}

void ServerView::OnMemberContainerChanging(
    controls::ListViewBase const&,
    controls::ContainerContentChangingEventArgs const& args) {
    const auto root = args.ItemContainer().ContentTemplateRoot().try_as<controls::StackPanel>();
    if (args.InRecycleQueue() || !root) {
        return;
    }
    const auto row = unbox_value<hstring>(args.Item());
    const auto [name, role] = split_line(row);
    const auto avatar = root.Children().GetAt(0).as<controls::Border>();
    avatar.Child().as<controls::TextBlock>().Text(
        name.empty() ? hstring{} : hstring{name.substr(0, 1)});
    const auto labels = root.Children().GetAt(1).as<controls::StackPanel>();
    text_at(labels, 0).Text(hstring{name});
    text_at(labels, 1).Text(hstring{role});
    args.Handled(true);
}

void ServerView::ResetMembers() {
    if (++member_generation_ == 0) {
        member_generation_ = 1;
    }
    MemberList().Items().Clear();
}

void ServerView::ShowLocalMemberFallback() {
    MemberList().Items().Clear();
    if (!local_state_) {
        MemberCountLabel().Text(L"MEMBERS — 0");
        return;
    }

    std::wstring row = to_hstring(
        local_state_->identity.display_name).c_str();
    row += L"\nOwner · You";
    MemberList().Items().Append(
        box_value(hstring{row}));
    MemberCountLabel().Text(L"MEMBERS — 1");
}

void ServerView::ApplyMemberRoster(
    const std::vector<
        catro::platform::windows::DirectoryMember>& members) {
    std::string local_id;
    if (local_state_) {
        local_id =
            catro::community::to_hex(
                local_state_->identity.id);
    }

    auto items = MemberList().Items();
    uint32_t index = 0;
    for (const auto& member : members) {
        std::wstring row = to_hstring(member.display_name).c_str();
        row += L"\n";
        row += member.role == "owner"
            ? L"Owner"
            : L"Member";
        if (!local_id.empty() &&
            member.user_id == local_id) {
            row += L" · You";
        }
        const hstring value{row};
        if (index >= items.Size()) {
            items.Append(box_value(value));
        } else if (unbox_value<hstring>(items.GetAt(index)) != value) {
            items.SetAt(index, box_value(value));
        }
        ++index;
    }
    while (items.Size() > index) {
        items.RemoveAtEnd();
    }

    std::wstring count = L"MEMBERS — ";
    count += std::to_wstring(members.size());
    MemberCountLabel().Text(hstring{count});
}

winrt::fire_and_forget
ServerView::BeginMemberRefresh() {
    auto lifetime = get_strong();
    if (!page_loaded_ || window_activity_ == catro::shell::WindowActivity::hidden ||
        !directory_service_ ||
        directory_access_token_.empty() ||
        !directory_server_) {
        co_return;
    }

    if (member_refresh_pending_) {
        co_return;
    }

    const auto generation = member_generation_;
    member_refresh_pending_ = true;
    member_refresh_generation_ = generation;

    const auto service = *directory_service_;
    const auto access_token = directory_access_token_;
    const auto server_id = directory_server_->id;
    UiThread ui_thread;

    co_await winrt::resume_background();
    auto result =
        catro::platform::windows::
            list_directory_members(
                service,
                access_token,
                server_id);

    co_await ui_thread;
    if (lifetime->member_refresh_generation_ ==
            generation) {
        lifetime->member_refresh_pending_ = false;
    }

    if (generation !=
            lifetime->member_generation_ ||
        !lifetime->directory_server_ ||
        lifetime->directory_server_->id !=
            server_id) {
        if (!lifetime->member_refresh_pending_) {
            lifetime->BeginMemberRefresh();
        }
        co_return;
    }

    if (std::holds_alternative<
            catro::platform::windows::
                DirectoryError>(result)) {
        // Keep the last valid roster visible. A later bounded timer tick
        // retries without turning a transient network failure into fake
        // membership state.
        co_return;
    }

    auto members =
        std::get<std::vector<
            catro::platform::windows::
                DirectoryMember>>(
                    std::move(result));
    lifetime->ApplyMemberRoster(members);
}

void ServerView::ResetAccessRequests() {
    if (++access_generation_ == 0) {
        access_generation_ = 1;
    }
    pending_join_requests_.clear();
    AccessRequestCount().Text(L"0");
}

winrt::fire_and_forget
ServerView::BeginJoinRequestRefresh() {
    auto lifetime = get_strong();
    if (!page_loaded_ || window_activity_ == catro::shell::WindowActivity::hidden ||
        access_refresh_pending_ ||
        !directory_service_ ||
        directory_access_token_.empty() ||
        !directory_server_ ||
        directory_server_->role != "owner") {
        co_return;
    }

    const auto generation = access_generation_;
    access_refresh_pending_ = true;
    access_refresh_generation_ = generation;

    const auto service = *directory_service_;
    const auto access_token =
        directory_access_token_;
    const auto server_id =
        directory_server_->id;
    UiThread ui_thread;

    co_await winrt::resume_background();
    auto result =
        catro::platform::windows::
            list_pending_directory_join_requests(
                service,
                access_token,
                server_id);
    co_await ui_thread;

    if (lifetime->access_refresh_generation_ ==
            generation) {
        lifetime->access_refresh_pending_ = false;
    }

    if (generation !=
            lifetime->access_generation_ ||
        !lifetime->directory_server_ ||
        lifetime->directory_server_->id !=
            server_id) {
        if (!lifetime->access_refresh_pending_) {
            lifetime->BeginJoinRequestRefresh();
        }
        co_return;
    }

    const auto* requests =
        std::get_if<std::vector<
            catro::platform::windows::
                DirectoryJoinRequest>>(&result);
    if (requests == nullptr) {
        co_return;
    }

    lifetime->pending_join_requests_ =
        *requests;
    lifetime->UpdateAccessUi();
}

winrt::fire_and_forget
ServerView::ShowAccessDialog() {
    auto lifetime = get_strong();
    if (access_dialog_open_ ||
        !directory_server_ ||
        directory_server_->role != "owner" ||
        directory_server_->public_code.empty()) {
        co_return;
    }

    access_dialog_open_ = true;
    UpdateAccessUi();

    const auto requests =
        pending_join_requests_;
    controls::ContentDialog dialog;
    dialog.XamlRoot(
        ServerLayout().XamlRoot());
    // Dialogs open in a popup outside the themed shell tree, so they copy its theme.
    dialog.RequestedTheme(ActualTheme());
    dialog.Title(
        box_value(hstring{L"Server access"}));
    dialog.PrimaryButtonText(L"Approve");
    dialog.SecondaryButtonText(L"Reject");
    dialog.CloseButtonText(L"Close");
    dialog.IsPrimaryButtonEnabled(
        !requests.empty());
    dialog.IsSecondaryButtonEnabled(
        !requests.empty());

    controls::StackPanel content;
    content.Spacing(8);

    controls::TextBlock label;
    label.Text(
        L"Share this Server Code when you want people to request access.");
    label.TextWrapping(
        xaml::TextWrapping::Wrap);
    content.Children().Append(label);

    controls::TextBox code_box;
    code_box.Header(
        box_value(hstring{L"Server Code"}));
    code_box.Text(
        to_hstring(
            directory_server_->public_code));
    code_box.IsReadOnly(true);
    content.Children().Append(code_box);

    controls::TextBlock queue_label;
    std::wstring queue_text =
        L"Pending requests — ";
    queue_text += std::to_wstring(
        requests.size());
    queue_label.Text(
        hstring{queue_text});
    queue_label.FontWeight(
        Windows::UI::Text::
            FontWeight{600});
    content.Children().Append(queue_label);

    controls::ListView request_list;
    request_list.Height(220);
    request_list.SelectionMode(
        controls::ListViewSelectionMode::Single);
    for (const auto& request : requests) {
        std::wstring row =
            to_hstring(
                request.requester_display_name)
                .c_str();
        const auto submitted =
            message_time(
                request.created_at * 1000);
        if (!submitted.empty()) {
            row += L"  ·  ";
            row += submitted;
        }
        if (!request.message.empty()) {
            row += L"\n";
            row +=
                to_hstring(
                    request.message).c_str();
        }
        request_list.Items().Append(
            box_value(hstring{row}));
    }
    if (!requests.empty()) {
        request_list.SelectedIndex(0);
    }
    content.Children().Append(request_list);

    if (requests.empty()) {
        controls::TextBlock empty;
        empty.Text(
            L"No pending access requests.");
        empty.Foreground(
            xaml::Application::Current()
                .Resources()
                .Lookup(
                    box_value(
                        hstring{
                            L"CatroTextTertiaryBrush"}))
                .as<
                    Microsoft::UI::Xaml::
                        Media::Brush>());
        content.Children().Append(empty);
    }

    dialog.Content(content);
    const auto result =
        co_await dialog.ShowAsync();

    lifetime->access_dialog_open_ = false;
    lifetime->UpdateAccessUi();

    if (result !=
            controls::ContentDialogResult::Primary &&
        result !=
            controls::ContentDialogResult::Secondary) {
        co_return;
    }

    const auto selected =
        request_list.SelectedIndex();
    if (selected < 0 ||
        static_cast<std::size_t>(selected) >=
            requests.size()) {
        co_return;
    }

    lifetime->BeginJoinRequestDecision(
        requests[static_cast<std::size_t>(
            selected)]
            .id,
        result ==
            controls::ContentDialogResult::
                Primary);
}

winrt::fire_and_forget
ServerView::BeginJoinRequestDecision(
    std::string request_id,
    bool approve) {
    auto lifetime = get_strong();
    if (access_decision_pending_ ||
        !directory_service_ ||
        directory_access_token_.empty() ||
        !directory_server_ ||
        directory_server_->role != "owner") {
        co_return;
    }

    const auto generation =
        access_generation_;
    const auto server_id =
        directory_server_->id;
    const auto service =
        *directory_service_;
    const auto access_token =
        directory_access_token_;
    access_decision_pending_ = true;
    UpdateAccessUi();
    OnlineStatusText().Text(
        approve
            ? L"Approving access request…"
            : L"Rejecting access request…");
    UiThread ui_thread;

    co_await winrt::resume_background();
    auto result =
        catro::platform::windows::
            decide_directory_join_request(
                service,
                access_token,
                request_id,
                approve);
    co_await ui_thread;

    lifetime->access_decision_pending_ =
        false;
    lifetime->UpdateAccessUi();

    if (generation !=
            lifetime->access_generation_ ||
        !lifetime->directory_server_ ||
        lifetime->directory_server_->id !=
            server_id) {
        co_return;
    }

    if (const auto* failure =
            std::get_if<
                catro::platform::windows::
                    DirectoryError>(&result)) {
        lifetime->OnlineStatusText().Text(
            to_hstring(failure->message));
        controls::ToolTipService::SetToolTip(
            lifetime->AccessButton(),
            box_value(
                to_hstring(
                    failure->message)));
        // A requester may have cancelled or another owner-side action may have resolved the
        // snapshot while the dialog was open. Refresh immediately so the stale row disappears
        // without waiting for the next five-second poll.
        lifetime->BeginJoinRequestRefresh();
        co_return;
    }

    lifetime->BeginJoinRequestRefresh();
    lifetime->OnlineStatusText().Text(
        approve
            ? L"Access request approved."
            : L"Access request rejected.");
    if (approve) {
        lifetime->BeginMemberRefresh();
    }
}

void ServerView::ResetMessages() {
    if (++message_generation_ == 0) {
        message_generation_ = 1;
    }
    message_cursor_ = 0;
    MessageList().Items().Clear();
    TextEmptyState().Visibility(
        xaml::Visibility::Visible);
    TextStatusText().Text(L"");
    TextStatusText().Visibility(
        xaml::Visibility::Collapsed);
}

void ServerView::AppendMessage(
    const catro::platform::windows::DirectoryMessage& message) {
    if (message.sequence <= message_cursor_) {
        return;
    }

    std::wstring display = to_hstring(message.author_id).c_str();
    display += L"\n";
    display += to_hstring(message.author_display_name).c_str();
    const auto timestamp =
        message_time(message.created_at);
    display += L"\n";
    display += timestamp;
    display += L"\n";
    display += to_hstring(message.content).c_str();

    MessageList().Items().Append(
        box_value(hstring{display}));

    message_cursor_ = message.sequence;
    while (MessageList().Items().Size() > 512) {
        MessageList().Items().RemoveAt(0);
    }
    TextEmptyState().Visibility(
        xaml::Visibility::Collapsed);

    const auto size = MessageList().Items().Size();
    if (size != 0) {
        MessageList().ScrollIntoView(
            MessageList().Items().GetAt(size - 1));
    }
}

void ServerView::AppendMessages(
    const std::vector<
        catro::platform::windows::DirectoryMessage>& messages) {
    for (const auto& message : messages) {
        AppendMessage(message);
    }
}

winrt::fire_and_forget
ServerView::BeginMessageRefresh() {
    auto lifetime = get_strong();
    if (!page_loaded_ || window_activity_ == catro::shell::WindowActivity::hidden ||
        state_.active_channel_kind() !=
            catro::community::ChannelKind::text ||
        !directory_service_ ||
        directory_access_token_.empty() ||
        !directory_server_ ||
        directory_server_->text_channel_id.empty()) {
        co_return;
    }

    const auto generation = message_generation_;
    if (message_refresh_pending_ &&
        message_refresh_generation_ == generation) {
        co_return;
    }
    message_refresh_pending_ = true;
    message_refresh_generation_ = generation;

    const auto service = *directory_service_;
    const auto access_token = directory_access_token_;
    const auto server = *directory_server_;
    const auto after = message_cursor_;
    UiThread ui_thread;

    co_await winrt::resume_background();
    auto result =
        catro::platform::windows::list_directory_messages(
            service,
            access_token,
            server.id,
            server.text_channel_id,
            after,
            100);

    co_await ui_thread;
    if (lifetime->message_refresh_generation_ == generation) {
        lifetime->message_refresh_pending_ = false;
    }
    if (generation != lifetime->message_generation_ ||
        !lifetime->directory_server_ ||
        lifetime->directory_server_->id != server.id ||
        lifetime->directory_server_->text_channel_id !=
            server.text_channel_id) {
        co_return;
    }

    if (const auto* failure =
            std::get_if<
                catro::platform::windows::DirectoryError>(&result)) {
        lifetime->TextStatusText().Text(
            to_hstring(failure->message));
        lifetime->TextStatusText().Visibility(
            xaml::Visibility::Visible);
        lifetime->UpdateMessageUi();
        co_return;
    }

    auto page =
        std::get<
            catro::platform::windows::DirectoryMessagePage>(
                std::move(result));
    lifetime->AppendMessages(page.messages);
    lifetime->TextStatusText().Text(L"");
    lifetime->TextStatusText().Visibility(
        xaml::Visibility::Collapsed);
    lifetime->UpdateMessageUi();
}

winrt::fire_and_forget
ServerView::BeginSendMessage() {
    auto lifetime = get_strong();
    if (state_.active_channel_kind() !=
        catro::community::ChannelKind::text) {
        co_return;
    }
    if (!directory_service_ ||
        directory_access_token_.empty() ||
        !directory_server_ ||
        directory_server_->text_channel_id.empty()) {
        workspace_state_.send_message.disable(
            "Online text is unavailable until this server is synchronized.");
        UpdateMessageUi();
        co_return;
    }
    if (workspace_state_.send_message.availability ==
        catro::app::Availability::busy) {
        co_return;
    }

    const auto generation = message_generation_;
    if (message_send_pending_ &&
        message_send_generation_ == generation) {
        co_return;
    }

    const auto original = Composer().Text();
    const auto content = winrt::to_string(original);
    const bool only_whitespace =
        std::all_of(
            content.begin(),
            content.end(),
            [](unsigned char value) {
                return value == ' ' ||
                       value == '\t' ||
                       value == '\r' ||
                       value == '\n';
            });
    if (content.empty() || only_whitespace) {
        co_return;
    }
    if (content.size() > 2000) {
        workspace_state_.send_message.fail(
            "Message is too long. The UTF-8 limit is 2,000 bytes.");
        UpdateMessageUi();
        co_return;
    }

    workspace_state_.send_message.enable();
    (void)workspace_state_.send_message.begin("Sending message…");
    message_send_pending_ = true;
    message_send_generation_ = generation;
    UpdateMessageUi();

    const auto service = *directory_service_;
    const auto access_token = directory_access_token_;
    const auto server = *directory_server_;
    UiThread ui_thread;

    co_await winrt::resume_background();
    auto result =
        catro::platform::windows::send_directory_message(
            service,
            access_token,
            server.id,
            server.text_channel_id,
            content);

    co_await ui_thread;
    if (lifetime->message_send_generation_ == generation) {
        lifetime->message_send_pending_ = false;
    }
    if (generation != lifetime->message_generation_ ||
        !lifetime->directory_server_ ||
        lifetime->directory_server_->id != server.id ||
        lifetime->directory_server_->text_channel_id !=
            server.text_channel_id) {
        co_return;
    }

    if (const auto* failure =
            std::get_if<
                catro::platform::windows::DirectoryError>(&result)) {
        lifetime->workspace_state_.send_message.fail(
            failure->message);
        lifetime->UpdateMessageUi();
        co_return;
    }

    // Do not advance the timeline cursor from the POST response. A history refresh that started
    // before this send may still contain lower sequences; letting only ordered GET pages advance
    // message_cursor_ prevents poll/send completion order from skipping retained messages.
    lifetime->workspace_state_.send_message.enable();
    lifetime->Composer().Text(L"");
    lifetime->TextStatusText().Text(L"");
    lifetime->TextStatusText().Visibility(
        xaml::Visibility::Collapsed);
    lifetime->UpdateMessageUi();
    lifetime->BeginMessageRefresh();
}

winrt::fire_and_forget
ServerView::BeginInvite() {
    auto lifetime = get_strong();
    if (invite_pending_ ||
        !directory_service_ ||
        directory_access_token_.empty() ||
        !directory_server_ ||
        directory_server_->role != "owner") {
        co_return;
    }

    const auto generation = ++invite_generation_;
    invite_pending_ = true;
    InviteButton().IsEnabled(false);
    OnlineStatusText().Text(L"Creating invite…");

    const auto service =
        *directory_service_;
    const auto access_token =
        directory_access_token_;
    const auto server_id =
        directory_server_->id;
    UiThread ui_thread;

    co_await winrt::resume_background();

    const auto result =
        catro::platform::windows::
            create_directory_invite(
                service,
                access_token,
                server_id);

    if (const auto* failure =
            std::get_if<
                catro::platform::windows::
                    DirectoryError>(&result)) {
        const auto message =
            failure->message;
        co_await ui_thread;
        if (generation != lifetime->invite_generation_ ||
            !lifetime->directory_server_ ||
            lifetime->directory_server_->id != server_id) {
            co_return;
        }

        lifetime->invite_pending_ = false;
        lifetime->InviteButton().IsEnabled(
            lifetime->directory_server_ &&
            lifetime->directory_server_->role ==
                "owner");
        lifetime->OnlineStatusText().Text(
            to_hstring(message));
        controls::ToolTipService::SetToolTip(
            lifetime->InviteButton(),
            box_value(to_hstring(message)));
        co_return;
    }

    auto invite =
        std::get<
            catro::platform::windows::
                DirectoryInvite>(result);

    co_await ui_thread;
    if (generation != lifetime->invite_generation_ ||
        !lifetime->directory_server_ ||
        lifetime->directory_server_->id != server_id) {
        co_return;
    }
    lifetime->invite_pending_ = false;
    lifetime->InviteButton().IsEnabled(true);
    lifetime->OnlineStatusText().Text(
        L"Invite created.");
    lifetime->ShowInviteCode(
        std::move(invite.code));
}

winrt::fire_and_forget
ServerView::ShowInviteCode(
    std::string code) {
    auto lifetime = get_strong();

    controls::ContentDialog dialog;
    dialog.XamlRoot(XamlRoot());
    dialog.RequestedTheme(ActualTheme());
    dialog.Title(
        box_value(
            hstring{L"Invite to server"}));
    dialog.CloseButtonText(L"Done");

    controls::StackPanel content;
    content.Spacing(8);

    controls::TextBlock hint;
    hint.Text(
        L"Send this one-use invite code to the person you want to add.");
    hint.TextWrapping(
        xaml::TextWrapping::Wrap);
    content.Children().Append(hint);

    controls::TextBox code_box;
    code_box.Text(to_hstring(code));
    code_box.IsReadOnly(true);
    code_box.SelectAll();
    content.Children().Append(code_box);

    dialog.Content(content);
    co_await dialog.ShowAsync();
}

void ServerView::SetDirectorySession(
    const catro::platform::windows::
        DirectoryServiceConfig& service,
    std::string access_token,
    const catro::platform::windows::
        DirectoryServer& server) {
    const bool server_changed =
        directory_server_ &&
        directory_server_->id !=
            server.id;
    const bool member_context_changed =
        !directory_server_ ||
        directory_server_->id !=
            server.id;
    const bool access_context_changed =
        !directory_server_ ||
        directory_server_->id !=
            server.id;
    const bool message_context_changed =
        !directory_server_ ||
        directory_server_->id != server.id ||
        directory_server_->text_channel_id !=
            server.text_channel_id;
    if (server_changed) {
        ++voice_join_generation_;
        ++invite_generation_;
        voice_join_pending_ = false;
        invite_pending_ = false;
        // Keep an in-flight access decision marked pending across server switches. Its generation
        // check will discard stale presentation state after completion, while this flag prevents a
        // second owner decision from overlapping the first request.
        access_dialog_open_ = false;
    }
    if (server_changed &&
        voice_runtime_ != nullptr) {
        const auto snapshot =
            catro_voice_runtime_snapshot(
                voice_runtime_);
        if (snapshot.state ==
                CATRO_VOICE_STARTING ||
            snapshot.state ==
                CATRO_VOICE_JOINED ||
            room_mode_active_) {
            StopVoice();
        }
    }

    directory_service_ = service;
    directory_access_token_ =
        std::move(access_token);
    directory_server_ = server;
    workspace_state_.connection =
        catro::app::ConnectionState::synchronized;
    workspace_state_.connection_message =
        "Online services connected.";
    workspace_state_.join_server.enable();
    if (message_context_changed) {
        workspace_state_.send_message.enable();
    }
    if (member_context_changed) {
        workspace_state_.join_voice.enable();
        workspace_state_.share_screen.enable();
    }
    UpdateOnlineStatus();
    if (member_context_changed) {
        ResetMembers();
    }
    if (access_context_changed) {
        ResetAccessRequests();
    }
    if (message_context_changed) {
        ResetMessages();
    }

    ServerName().Text(
        to_hstring(server.name));
    std::wstring count =
        L"MEMBERS — ";
    count += std::to_wstring(
        server.member_count);
    MemberCountLabel().Text(
        hstring{count});

    const bool owner =
        server.role == "owner";
    ServerOwnerIcon().Visibility(
        owner
            ? xaml::Visibility::Visible
            : xaml::Visibility::Collapsed);
    ProfileRoleText().Text(
        owner ? L"Owner" : L"Member");
    VoiceRoleText().Text(
        owner ? L"OWNER" : L"MEMBER");
    InviteButton().IsEnabled(
        owner && !invite_pending_);
    UpdateAccessUi();

    ShowChannel(state_.channel_id());
    UpdateMessageUi();
    ApplyActivityPolicy();
    BeginMemberRefresh();
    BeginJoinRequestRefresh();
    BeginMessageRefresh();
}


} // namespace winrt::Catro::implementation

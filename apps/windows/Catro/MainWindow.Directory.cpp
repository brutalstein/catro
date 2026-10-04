#include "pch.h"

#include "MainWindow.xaml.h"
#include "Server/ServerView.xaml.h"
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Networking.Connectivity.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace winrt::Catro::implementation {

namespace {

namespace xaml = Microsoft::UI::Xaml;
namespace automation = Microsoft::UI::Xaml::Automation;
namespace controls = Microsoft::UI::Xaml::Controls;

std::string trim_ascii(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

winrt::fire_and_forget paste_invite(controls::TextBox invite_box) {
    UiThread ui_thread;
    hstring text;
    bool failed = false;
    try {
        const auto clipboard = Windows::ApplicationModel::DataTransfer::Clipboard::GetContent();
        if (clipboard.Contains(Windows::ApplicationModel::DataTransfer::StandardDataFormats::Text())) {
            text = co_await clipboard.GetTextAsync();
        }
    } catch (winrt::hresult_error const&) {
        failed = true;
    }
    co_await ui_thread;
    if (failed) {
        invite_box.PlaceholderText(L"Clipboard unavailable. Paste with Ctrl+V.");
    } else if (!text.empty()) {
        invite_box.Text(text);
    }
}

// Registers this identity, syncs the personal server and lists servers.
// Any failure is reported so the caller can retry the whole sequence.
std::variant<DirectoryLink, catro::platform::windows::DirectoryError>
connect_directory(
    const catro::platform::windows::DirectoryServiceConfig& service,
    const catro::community::LocalState& state,
    const std::string& credential) {
    namespace directory = catro::platform::windows;
    auto registration = directory::register_directory_identity(
        service, state.identity, credential);
    if (auto* failure = std::get_if<directory::DirectoryError>(&registration)) {
        return std::move(*failure);
    }
    DirectoryLink link;
    link.access_token = std::get<std::string>(std::move(registration));

    auto synced = directory::sync_personal_server(
        service, link.access_token, state.personal_server);
    if (auto* failure = std::get_if<directory::DirectoryError>(&synced)) {
        return std::move(*failure);
    }
    link.personal = std::get<directory::DirectoryServer>(std::move(synced));

    auto listed = directory::list_directory_servers(service, link.access_token);
    if (auto* values = std::get_if<std::vector<directory::DirectoryServer>>(&listed)) {
        link.servers = std::move(*values);
    } else {
        // The personal server alone is enough to go online; the rail refreshes later.
        link.servers.push_back(link.personal);
    }
    return link;
}

std::string reconnect_message(
    const catro::platform::windows::DirectoryError& failure,
    std::chrono::seconds delay) {
    std::string detail;
    if (failure.http_status != 0) {
        detail = " (HTTP " + std::to_string(failure.http_status) + ")";
    } else if (failure.native_code != 0) {
        detail = " (error " + std::to_string(failure.native_code) + ")";
    }
    return "Can't reach Catro online" + detail + ". Retrying in " +
           std::to_string(delay.count()) + " s\u2026";
}

} // namespace

void MainWindow::ActivateDirectoryServer(
    std::string_view server_id) {
    const auto found =
        std::ranges::find(
            directory_servers_,
            server_id,
            &catro::platform::windows::
                DirectoryServer::id);
    if (found == directory_servers_.end()) {
        return;
    }

    active_directory_server_ = *found;
    ApplyDirectoryServerToPage();
    Activate(catro::app::AppDestination::server);
    RefreshDirectoryRail();
}

winrt::fire_and_forget
MainWindow::BeginDirectoryBootstrap() {
    auto lifetime = get_strong();
    if (!local_state_) {
        workspace_state_.fail("Local profile is unavailable.");
        UpdateConnectionUi();
        co_return;
    }

    const auto state = *local_state_;
    UiThread ui_thread;

    co_await winrt::resume_background();

    const auto config_result =
        catro::platform::windows::
            load_directory_service_config();
    if (const auto* failure =
            std::get_if<
                catro::platform::windows::
                    DirectoryError>(
                &config_result)) {
        const auto message =
            failure->code ==
                    catro::platform::windows::
                        DirectoryErrorCode::not_configured
                ? std::string{
                      "Online services are not configured. Local mode remains available."}
                : failure->message;
        co_await ui_thread;
        lifetime->workspace_state_.fail(message);
        lifetime->UpdateConnectionUi();
        controls::ToolTipService::SetToolTip(
            lifetime->JoinServerButton(),
            box_value(to_hstring(message)));
        co_return;
    }
    const auto service =
        std::get<
            catro::platform::windows::
                DirectoryServiceConfig>(
            config_result);

    const auto credential_result =
        catro::platform::windows::
            load_or_create_directory_credential();
    if (const auto* failure =
            std::get_if<
                catro::platform::windows::
                    DirectoryError>(
                &credential_result)) {
        const auto message = failure->message;
        co_await ui_thread;
        lifetime->workspace_state_.fail(message);
        lifetime->UpdateConnectionUi();
        controls::ToolTipService::SetToolTip(
            lifetime->JoinServerButton(),
            box_value(to_hstring(message)));
        co_return;
    }
    const auto credential =
        std::get<std::string>(
            credential_result);

    // Wake the retry wait as soon as Windows reports a network change.
    winrt::handle network_changed{
        CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    Windows::Networking::Connectivity::NetworkInformation::
        NetworkStatusChanged_revoker network_revoker;
    try {
        network_revoker =
            Windows::Networking::Connectivity::NetworkInformation::
                NetworkStatusChanged(
                    winrt::auto_revoke,
                    [event = network_changed.get()](auto&&) {
                        SetEvent(event);
                    });
    } catch (winrt::hresult_error const&) {
        // Without change notifications the backoff alone still recovers.
    }

    // Startup often races the network (Wi-Fi joining, VPN, sleep resume), so a
    // failed attempt is retried with backoff instead of leaving the session offline.
    for (unsigned attempt = 0;; ++attempt) {
        auto connected = connect_directory(
            service, state, credential);
        co_await ui_thread;
        if (lifetime->window_closed_) {
            co_return;
        }
        if (auto* link =
                std::get_if<DirectoryLink>(
                    &connected)) {
            lifetime->ApplyDirectoryLink(
                service, std::move(*link));
            co_return;
        }
        const auto delay =
            catro::app::reconnect_delay(attempt);
        const auto message = reconnect_message(
            std::get<catro::platform::windows::
                         DirectoryError>(connected),
            delay);
        lifetime->workspace_state_.reconnect(message);
        lifetime->UpdateConnectionUi();
        controls::ToolTipService::SetToolTip(
            lifetime->JoinServerButton(),
            box_value(to_hstring(message)));
        co_await winrt::resume_on_signal(
            network_changed.get(), delay);
    }
}

void MainWindow::ApplyDirectoryLink(
    catro::platform::windows::DirectoryServiceConfig service,
    DirectoryLink link) {
    directory_service_ = std::move(service);
    directory_access_token_ = std::move(link.access_token);
    directory_servers_ = std::move(link.servers);

    const auto found = std::ranges::find(
        directory_servers_,
        link.personal.id,
        &catro::platform::windows::DirectoryServer::id);
    active_directory_server_ =
        found != directory_servers_.end()
            ? *found
            : link.personal;

    workspace_state_.synchronize();
    UpdateConnectionUi();
    controls::ToolTipService::SetToolTip(
        JoinServerButton(),
        box_value(hstring{L"Add server"}));
    if (join_request_timer_) {
        UpdateWindowActivity();
    }
    BeginOutgoingJoinRequestRefresh();
    RefreshDirectoryRail();
    ApplyDirectoryServerToPage();
    if (shell_state_.destination() ==
        catro::app::AppDestination::server) {
        TitleContext().Text(
            to_hstring(active_directory_server_->name));
    }
}

winrt::fire_and_forget
MainWindow::BeginJoinServer() {
    auto lifetime = get_strong();
    if (!directory_service_ ||
        directory_access_token_.empty()) {
        workspace_state_.fail(
            "Online services are unavailable. Check the connection status above.");
        UpdateConnectionUi();
        co_return;
    }
    if (workspace_state_.join_server.availability ==
        catro::app::Availability::busy) {
        co_return;
    }
    workspace_state_.join_server.enable();
    UpdateConnectionUi();

    controls::ContentDialog dialog;
    dialog.XamlRoot(AppTitleBar().XamlRoot());
    // Dialogs open in a popup outside the themed shell tree, so they copy its theme.
    dialog.RequestedTheme(AppTitleBar().ActualTheme());
    dialog.Title(box_value(hstring{L"Add a Catro server"}));
    dialog.PrimaryButtonText(L"Find by Server Code");
    dialog.SecondaryButtonText(L"Use Invite Code");
    dialog.CloseButtonText(L"Cancel");
    dialog.DefaultButton(
        controls::ContentDialogButton::Primary);

    controls::StackPanel content;
    content.Spacing(10);

    controls::TextBlock code_help;
    code_help.Text(
        L"Server Code sends an access request. Invite Code joins directly.");
    code_help.TextWrapping(xaml::TextWrapping::Wrap);
    code_help.Foreground(
        xaml::Application::Current().Resources()
            .Lookup(box_value(hstring{L"CatroTextSecondaryBrush"}))
            .as<Microsoft::UI::Xaml::Media::Brush>());
    content.Children().Append(code_help);

    controls::TextBox server_code_box;
    server_code_box.Header(
        box_value(hstring{L"Server Code"}));
    server_code_box.PlaceholderText(
        L"CAT-XXXX-XXXX-XXXX-XXXX-XXXX");
    server_code_box.MaxLength(28);
    content.Children().Append(server_code_box);

    controls::TextBox invite_box;
    invite_box.Header(
        box_value(hstring{L"Invite Code"}));
    invite_box.PlaceholderText(
        L"Paste an owner-provided invite code");
    invite_box.MaxLength(128);
    content.Children().Append(invite_box);
    controls::Button paste;
    paste.Content(box_value(hstring{L"Paste invite from clipboard"}));
    paste.Click([invite_box](auto&&, auto&&) { paste_invite(invite_box); });
    content.Children().Append(paste);
    dialog.IsPrimaryButtonEnabled(false);
    dialog.IsSecondaryButtonEnabled(false);
    server_code_box.TextChanged([weak = make_weak(dialog)](IInspectable const& sender, auto&&) {
        if (const auto owner = weak.get()) {
            owner.IsPrimaryButtonEnabled(!trim_ascii(to_string(sender.as<controls::TextBox>().Text())).empty());
        }
    });
    invite_box.TextChanged([weak = make_weak(dialog)](IInspectable const& sender, auto&&) {
        if (const auto owner = weak.get()) {
            owner.IsSecondaryButtonEnabled(!trim_ascii(to_string(sender.as<controls::TextBox>().Text())).empty());
        }
    });
    dialog.Content(content);

    const auto result = co_await dialog.ShowAsync();
    if (result == controls::ContentDialogResult::Primary) {
        const auto code = trim_ascii(
            winrt::to_string(server_code_box.Text()));
        if (!code.empty()) {
            BeginServerCodeLookup(code);
        }
        co_return;
    }
    if (result == controls::ContentDialogResult::Secondary) {
        const auto invite = trim_ascii(
            winrt::to_string(invite_box.Text()));
        if (!invite.empty()) {
            BeginInviteJoin(invite);
        }
    }
}

winrt::fire_and_forget
MainWindow::BeginServerCodeLookup(
    std::string server_code) {
    auto lifetime = get_strong();
    if (!directory_service_ ||
        directory_access_token_.empty()) {
        workspace_state_.join_server.fail(
            "Server lookup is unavailable while online services are disconnected.");
        UpdateConnectionUi();
        co_return;
    }
    if (workspace_state_.join_server.availability ==
        catro::app::Availability::busy) {
        co_return;
    }
    workspace_state_.join_server.enable();
    (void)workspace_state_.join_server.begin(
        "Looking up Server Code…");
    UpdateConnectionUi();

    const auto service = *directory_service_;
    const auto access_token =
        directory_access_token_;
    UiThread ui_thread;

    co_await winrt::resume_background();
    auto lookup =
        catro::platform::windows::
            lookup_directory_server(
                service,
                access_token,
                server_code);
    co_await ui_thread;

    if (const auto* failure =
            std::get_if<
                catro::platform::windows::
                    DirectoryError>(&lookup)) {
        lifetime->workspace_state_.join_server.fail(
            failure->message);
        lifetime->UpdateConnectionUi();
        controls::ToolTipService::SetToolTip(
            lifetime->JoinServerButton(),
            box_value(to_hstring(failure->message)));
        co_return;
    }
    lifetime->workspace_state_.join_server.enable();
    lifetime->UpdateConnectionUi();

    const auto preview =
        std::get<
            catro::platform::windows::
                DirectoryServerLookup>(
                    std::move(lookup));

    if (preview.relationship != "none") {
        controls::ContentDialog status;
        status.XamlRoot(lifetime->AppTitleBar().XamlRoot());
        status.RequestedTheme(lifetime->AppTitleBar().ActualTheme());
        status.Title(
            box_value(to_hstring(preview.name)));
        status.CloseButtonText(L"Close");

        std::wstring message;
        if (preview.relationship == "pending") {
            message =
                L"Your access request is pending owner approval.";
            status.PrimaryButtonText(L"Cancel request");
        } else if (preview.relationship == "owner") {
            message =
                L"You own this server.";
        } else {
            message =
                L"You are already a member of this server.";
        }
        controls::TextBlock text;
        text.Text(hstring{message});
        text.TextWrapping(xaml::TextWrapping::Wrap);
        status.Content(text);

        const auto status_result =
            co_await status.ShowAsync();
        if (preview.relationship != "pending" ||
            status_result !=
                controls::ContentDialogResult::Primary) {
            co_return;
        }

        (void)lifetime->workspace_state_.join_server.begin(
            "Cancelling access request…");
        lifetime->UpdateConnectionUi();
        co_await winrt::resume_background();
        auto cancelled =
            catro::platform::windows::
                cancel_directory_join_request(
                    service,
                    access_token,
                    preview.request_id);
        co_await ui_thread;

        if (const auto* failure =
                std::get_if<
                    catro::platform::windows::
                        DirectoryError>(&cancelled)) {
            lifetime->workspace_state_.join_server.fail(
                failure->message);
            lifetime->UpdateConnectionUi();
            controls::ToolTipService::SetToolTip(
                lifetime->JoinServerButton(),
                box_value(
                    to_hstring(
                        failure->message)));
            co_return;
        }
        lifetime->workspace_state_.join_server.enable();
        lifetime->UpdateConnectionUi();

        const auto request =
            std::get<
                catro::platform::windows::
                    DirectoryJoinRequest>(
                        std::move(cancelled));
        lifetime->observed_join_request_ids_
            .insert(request.id);
        std::wstring notice =
            L"Access request cancelled: ";
        notice +=
            to_hstring(request.server_name).c_str();
        controls::ToolTipService::SetToolTip(
            lifetime->JoinServerButton(),
            box_value(hstring{notice}));
        co_return;
    }

    controls::ContentDialog request_dialog;
    request_dialog.XamlRoot(
        lifetime->AppTitleBar().XamlRoot());
    request_dialog.RequestedTheme(lifetime->AppTitleBar().ActualTheme());
    request_dialog.Title(
        box_value(to_hstring(preview.name)));
    request_dialog.PrimaryButtonText(L"Request access");
    request_dialog.CloseButtonText(L"Cancel");
    request_dialog.DefaultButton(
        controls::ContentDialogButton::Primary);

    controls::StackPanel request_content;
    request_content.Spacing(8);

    controls::TextBlock details;
    std::wstring detail_text =
        L"Server Code: ";
    detail_text += to_hstring(preview.public_code).c_str();
    detail_text += L"\nMembers: ";
    detail_text += std::to_wstring(preview.member_count);
    details.Text(hstring{detail_text});
    details.TextWrapping(xaml::TextWrapping::Wrap);
    request_content.Children().Append(details);

    controls::TextBox note_box;
    note_box.Header(
        box_value(hstring{L"Request note (optional)"}));
    note_box.PlaceholderText(
        L"Tell the owner why you want to join");
    note_box.MaxLength(280);
    request_content.Children().Append(note_box);
    request_dialog.Content(request_content);

    if (co_await request_dialog.ShowAsync() !=
        controls::ContentDialogResult::Primary) {
        co_return;
    }

    const auto note =
        winrt::to_string(note_box.Text());
    if (note.size() > 280) {
        lifetime->workspace_state_.join_server.fail(
            "Request note exceeds the 280-byte UTF-8 limit.");
        lifetime->UpdateConnectionUi();
        controls::ToolTipService::SetToolTip(
            lifetime->JoinServerButton(),
            box_value(hstring{
                L"Request note exceeds the 280-byte UTF-8 limit."}));
        co_return;
    }

    (void)lifetime->workspace_state_.join_server.begin(
        "Sending access request…");
    lifetime->UpdateConnectionUi();
    co_await winrt::resume_background();
    auto created =
        catro::platform::windows::
            create_directory_join_request(
                service,
                access_token,
                preview.public_code,
                note);
    co_await ui_thread;

    if (const auto* failure =
            std::get_if<
                catro::platform::windows::
                    DirectoryError>(&created)) {
        lifetime->workspace_state_.join_server.fail(
            failure->message);
        lifetime->UpdateConnectionUi();
        controls::ToolTipService::SetToolTip(
            lifetime->JoinServerButton(),
            box_value(to_hstring(failure->message)));
        co_return;
    }
    lifetime->workspace_state_.join_server.enable();
    lifetime->UpdateConnectionUi();

    const auto request =
        std::get<
            catro::platform::windows::
                DirectoryJoinRequest>(
                    std::move(created));
    std::wstring sent =
        L"Access request sent to ";
    sent += to_hstring(request.server_name).c_str();
    controls::ToolTipService::SetToolTip(
        lifetime->JoinServerButton(),
        box_value(hstring{sent}));
    lifetime->BeginOutgoingJoinRequestRefresh();
}

winrt::fire_and_forget
MainWindow::BeginInviteJoin(
    std::string invite) {
    auto lifetime = get_strong();
    if (!directory_service_ ||
        directory_access_token_.empty() ||
        invite.empty()) {
        workspace_state_.join_server.fail(
            "Invite join is unavailable. Check the invite and online connection.");
        UpdateConnectionUi();
        co_return;
    }
    if (workspace_state_.join_server.availability ==
        catro::app::Availability::busy) {
        co_return;
    }
    workspace_state_.join_server.enable();
    (void)workspace_state_.join_server.begin(
        "Joining server…");
    UpdateConnectionUi();

    const auto service =
        *directory_service_;
    const auto access_token =
        directory_access_token_;
    UiThread ui_thread;

    co_await winrt::resume_background();
    const auto accepted =
        catro::platform::windows::
            accept_directory_invite(
                service,
                access_token,
                invite);

    if (const auto* failure =
            std::get_if<
                catro::platform::windows::
                    DirectoryError>(&accepted)) {
        const auto message = failure->message;
        co_await ui_thread;
        lifetime->workspace_state_.join_server.fail(
            message);
        lifetime->UpdateConnectionUi();
        controls::ToolTipService::SetToolTip(
            lifetime->JoinServerButton(),
            box_value(to_hstring(message)));
        co_return;
    }

    const auto server =
        std::get<
            catro::platform::windows::
                DirectoryServer>(accepted);
    co_await ui_thread;

    auto found =
        std::ranges::find(
            lifetime->directory_servers_,
            server.id,
            &catro::platform::windows::
                DirectoryServer::id);
    if (found ==
        lifetime->directory_servers_.end()) {
        lifetime->directory_servers_
            .push_back(server);
    } else {
        *found = server;
    }
    lifetime->active_directory_server_ =
        server;
    lifetime->workspace_state_.join_server.enable();
    lifetime->UpdateConnectionUi();
    lifetime->RefreshDirectoryRail();
    lifetime->ApplyDirectoryServerToPage();
    lifetime->Activate(
        catro::app::AppDestination::server);
}

winrt::fire_and_forget
MainWindow::BeginOutgoingJoinRequestRefresh() {
    auto lifetime = get_strong();
    if (join_request_refresh_pending_ ||
        !directory_service_ ||
        directory_access_token_.empty()) {
        co_return;
    }

    join_request_refresh_pending_ = true;
    const auto service = *directory_service_;
    const auto access_token =
        directory_access_token_;
    UiThread ui_thread;

    co_await winrt::resume_background();
    auto result =
        catro::platform::windows::
            list_outgoing_directory_join_requests(
                service,
                access_token);
    co_await ui_thread;
    lifetime->join_request_refresh_pending_ = false;

    const auto* requests =
        std::get_if<std::vector<
            catro::platform::windows::
                DirectoryJoinRequest>>(&result);
    if (requests == nullptr) {
        co_return;
    }

    bool refresh_servers = false;
    for (const auto& request : *requests) {
        if (request.status == "pending" ||
            lifetime->observed_join_request_ids_
                .contains(request.id)) {
            continue;
        }

        if (request.status == "approved") {
            // Approval is not consumed until the authoritative server list has refreshed
            // successfully. A transient list failure therefore retries on the next bounded poll.
            lifetime->
                approved_join_requests_waiting_refresh_
                .insert(request.id);
            refresh_servers = true;
            continue;
        }

        if (!lifetime->
                observed_join_request_ids_
                .insert(request.id)
                .second) {
            continue;
        }
        std::wstring notice =
            L"Access request ";
        notice += request.status == "rejected"
            ? L"rejected: "
            : L"cancelled: ";
        notice +=
            to_hstring(request.server_name).c_str();
        std::string visible_notice =
            request.status == "rejected"
                ? "Access request rejected: "
                : "Access request cancelled: ";
        visible_notice += request.server_name;
        lifetime->workspace_state_.join_server.fail(
            std::move(visible_notice));
        lifetime->UpdateConnectionUi();
        controls::ToolTipService::SetToolTip(
            lifetime->JoinServerButton(),
            box_value(hstring{notice}));
    }

    if (refresh_servers) {
        lifetime->BeginDirectoryServerRefresh();
    }
}

winrt::fire_and_forget
MainWindow::BeginDirectoryServerRefresh() {
    auto lifetime = get_strong();
    if (directory_server_refresh_pending_ ||
        !directory_service_ ||
        directory_access_token_.empty()) {
        co_return;
    }

    directory_server_refresh_pending_ = true;
    const auto service = *directory_service_;
    const auto access_token =
        directory_access_token_;
    const auto active_id =
        active_directory_server_
            ? active_directory_server_->id
            : std::string{};
    UiThread ui_thread;

    co_await winrt::resume_background();
    auto result =
        catro::platform::windows::
            list_directory_servers(
                service,
                access_token);
    co_await ui_thread;
    lifetime->directory_server_refresh_pending_ =
        false;

    const auto* servers =
        std::get_if<std::vector<
            catro::platform::windows::
                DirectoryServer>>(&result);
    if (servers == nullptr) {
        co_return;
    }

    lifetime->directory_servers_ = *servers;
    for (const auto& request_id :
         lifetime->
             approved_join_requests_waiting_refresh_) {
        lifetime->observed_join_request_ids_
            .insert(request_id);
    }
    lifetime->
        approved_join_requests_waiting_refresh_
        .clear();

    auto active =
        std::ranges::find(
            lifetime->directory_servers_,
            active_id,
            &catro::platform::windows::
                DirectoryServer::id);
    if (active !=
        lifetime->directory_servers_.end()) {
        lifetime->active_directory_server_ =
            *active;
    } else if (lifetime->local_state_) {
        const auto personal_id =
            catro::community::to_hex(
                lifetime->local_state_->
                    personal_server.id);
        const auto personal =
            std::ranges::find(
                lifetime->directory_servers_,
                personal_id,
                &catro::platform::windows::
                    DirectoryServer::id);
        if (personal !=
            lifetime->directory_servers_.end()) {
            lifetime->active_directory_server_ =
                *personal;
        } else {
            lifetime->active_directory_server_.reset();
        }
    }

    lifetime->RefreshDirectoryRail();
    lifetime->ApplyDirectoryServerToPage();
    if (lifetime->shell_state_.destination() ==
            catro::app::AppDestination::server &&
        lifetime->active_directory_server_) {
        lifetime->TitleContext().Text(
            to_hstring(
                lifetime->active_directory_server_->
                    name));
    }
}

void MainWindow::RefreshDirectoryRail() {
    JoinedServersPanel().Children().Clear();
    if (!local_state_) {
        return;
    }

    const auto personal_id =
        catro::community::to_hex(
            local_state_->
                personal_server.id);
    for (const auto& server :
         directory_servers_) {
        if (server.id == personal_id) {
            continue;
        }

        controls::Button button;
        button.Width(40);
        button.Height(40);
        button.Padding(xaml::Thickness{0.0});
        button.CornerRadius(
            xaml::CornerRadius{14.0});
        button.BorderThickness(
            xaml::Thickness{0.0});
        button.Opacity(
            active_directory_server_ &&
                    active_directory_server_
                        ->id == server.id
                ? 1.0
                : 0.72);

        controls::TextBlock label;
        const auto name =
            to_hstring(server.name);
        if (!name.empty()) {
            label.Text(
                hstring{
                    std::wstring{
                        name.c_str(), 1}});
        } else {
            label.Text(L"S");
        }
        // Avoid the generated FontWeights factory here: some Windows SDK projection orders expose
        // the auto-return declaration before its inline definition in this translation unit.
        label.FontWeight(
            Windows::UI::Text::
                FontWeight{600});
        button.Content(label);
        automation::AutomationProperties::SetName(
            button,
            name.empty() ? hstring{L"Server"} : name);

        controls::ToolTipService::
            SetToolTip(
                button,
                box_value(name));
        const auto id = server.id;
        button.Click(
            [this, id](
                auto const&,
                auto const&) {
                ActivateDirectoryServer(id);
            });
        JoinedServersPanel()
            .Children()
            .Append(button);
    }

    UpdateRail();
}

void MainWindow::ApplyDirectoryServerToPage() {
    if (!server_page_ ||
        !directory_service_ ||
        directory_access_token_.empty() ||
        !active_directory_server_) {
        return;
    }

    const auto page =
        server_page_.try_as<
            Catro::ServerView>();
    if (!page) {
        return;
    }
    get_self<
        winrt::Catro::implementation::
            ServerView>(page)
        ->SetDirectorySession(
            *directory_service_,
            directory_access_token_,
            *active_directory_server_);
}


} // namespace winrt::Catro::implementation

#include "pch.h"

#include "Server/ServerView.xaml.h"
#include "Settings/Voice.hpp"

#include <winrt/Windows.UI.Text.h>

#include <chrono>
#include <ctime>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace winrt::Catro::implementation {

namespace {

namespace xaml = Microsoft::UI::Xaml;
namespace controls = Microsoft::UI::Xaml::Controls;
using namespace std::chrono_literals;

std::tm message_local_time(std::int64_t unix_milliseconds) {
    const auto seconds = static_cast<std::time_t>(unix_milliseconds / 1000);
    std::tm local{};
    (void)localtime_s(&local, &seconds);
    return local;
}

std::wstring message_date(const std::tm& local, bool exact = false) {
    const SYSTEMTIME value{
        static_cast<WORD>(local.tm_year + 1900), static_cast<WORD>(local.tm_mon + 1),
        static_cast<WORD>(local.tm_wday), static_cast<WORD>(local.tm_mday),
        static_cast<WORD>(local.tm_hour), static_cast<WORD>(local.tm_min),
        static_cast<WORD>(local.tm_sec), 0};
    wchar_t buffer[128]{};
    (void)GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, exact ? DATE_LONGDATE : DATE_SHORTDATE,
                         &value, nullptr, buffer, 128, nullptr);
    return buffer;
}

std::wstring message_time(const std::tm& local) {
    SYSTEMTIME value{};
    value.wHour = static_cast<WORD>(local.tm_hour);
    value.wMinute = static_cast<WORD>(local.tm_min);
    value.wSecond = static_cast<WORD>(local.tm_sec);
    wchar_t buffer[64]{};
    (void)GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &value, nullptr, buffer, 64);
    return buffer;
}

std::wstring message_day_label(const std::tm& local) {
    const auto now = message_local_time(
        static_cast<std::int64_t>(std::time(nullptr)) * 1000);
    const auto age = catro::shell::message_day_age(local, now);
    wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
    (void)GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
    const bool turkish = std::wstring_view{locale}.starts_with(L"tr");
    if (age == 0) {
        return turkish ? L"Bugün" : L"Today";
    }
    if (age == 1) {
        return turkish ? L"Dün" : L"Yesterday";
    }
    return message_date(local);
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
    controls::ListViewBase const& list,
    controls::ContainerContentChangingEventArgs const& args) {
    const auto root = args.ItemContainer().ContentTemplateRoot().try_as<controls::StackPanel>();
    if (args.InRecycleQueue() || !root) {
        return;
    }
    const auto row = unbox_value<hstring>(args.Item());
    const auto [message_id, message_content] = split_line(row);
    const auto [author_id, content] = split_line(message_content);
    const auto [author, rest] = split_line(content);
    const auto [timestamp, body] = split_line(rest);
    const auto milliseconds = std::stoll(std::wstring{timestamp});
    const auto local = message_local_time(milliseconds);
    const auto day_label = message_day_label(local);
    const auto time = day_label + L" · " + message_time(local);
    const auto exact = message_date(local, true) + L" · " + message_time(local);
    bool first_of_day = args.ItemIndex() == 0;
    if (!first_of_day && args.ItemIndex() < list.Items().Size()) {
        const auto previous = unbox_value<hstring>(list.Items().GetAt(args.ItemIndex() - 1));
        const auto previous_author_id = split_line(previous).second;
        const auto previous_content = split_line(previous_author_id).second;
        const auto previous_rest = split_line(previous_content).second;
        const auto previous_timestamp = split_line(previous_rest).first;
        first_of_day = catro::shell::message_day(local) != catro::shell::message_day(
            message_local_time(std::stoll(std::wstring{previous_timestamp})));
    }
    const auto divider = root.Children().GetAt(0).as<controls::Border>();
    divider.Child().as<controls::TextBlock>().Text(hstring{day_label});
    divider.Visibility(first_of_day ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    const auto header = root.Children().GetAt(1).as<controls::Grid>();
    text_at(header, 0).Text(hstring{author});
    constexpr wchar_t const* colors[] = {L"CatroChatCopperStyle", L"CatroChatSageStyle", L"CatroChatRoseStyle"};
    const auto color = author_id.empty() ? 0U : static_cast<unsigned>(author_id.back()) % 3U;
    text_at(header, 0).Style(xaml::Application::Current().Resources().Lookup(
        box_value(hstring{colors[color]})).as<xaml::Style>());
    text_at(header, 1).Text(hstring{time});
    controls::ToolTipService::SetToolTip(text_at(header, 1), box_value(hstring{exact}));
    text_at(root, 2).Text(hstring{body});
    // The row's internal author ID chooses a stable colour; Narrator reads the useful content only.
    std::wstring announcement{author};
    announcement += L" ";
    announcement += time;
    announcement += L" ";
    announcement += body;
    xaml::Automation::AutomationProperties::SetName(args.ItemContainer(), hstring{announcement});
    const auto container = args.ItemContainer();
    container.Tag(box_value(hstring{message_id}));
    if (directory_server_ && directory_server_->role == "owner" && !message_id.empty()) {
        container.ContextFlyout(MessageAdminFlyout());
    } else {
        container.ClearValue(xaml::UIElement::ContextFlyoutProperty());
    }
    args.Handled(true);
}

void ServerView::OnMemberContainerChanging(
    controls::ListViewBase const&,
    controls::ContainerContentChangingEventArgs const& args) {
    const auto root = args.ItemContainer().ContentTemplateRoot().try_as<controls::Grid>();
    if (args.InRecycleQueue() || !root) {
        return;
    }
    // Row: user id, flags (s = speaking, y = you), display name, role.
    const auto row = unbox_value<hstring>(args.Item());
    const auto [user_id, rest] = split_line(row);
    const auto [flags, labels_text] = split_line(rest);
    const auto [name, role] = split_line(labels_text);
    const bool speaking = flags.find(L's') != std::wstring_view::npos;
    const bool self = flags.find(L'y') != std::wstring_view::npos;
    const auto avatar = root.Children().GetAt(0).as<controls::Border>();
    avatar.Child().as<controls::TextBlock>().Text(
        name.empty() ? hstring{} : hstring{name.substr(0, 1)});
    if (speaking) {
        avatar.BorderBrush(xaml::Application::Current().Resources().Lookup(
            box_value(L"CatroSageBrush")).as<Microsoft::UI::Xaml::Media::Brush>());
    } else {
        avatar.ClearValue(controls::Border::BorderBrushProperty());
    }
    const auto labels = root.Children().GetAt(1).as<controls::StackPanel>();
    text_at(labels, 0).Text(hstring{name});
    controls::ToolTipService::SetToolTip(text_at(labels, 0), box_value(hstring{name}));
    std::wstring detail{role};
    if (speaking) {
        detail += L" · Speaking";
    }
    text_at(labels, 1).Text(hstring{detail});
    xaml::Automation::AutomationProperties::SetName(args.ItemContainer(),
        hstring{std::wstring{name} + L" · " + detail});
    // Click or right-click a participant to change only their voice in your headphones.
    const auto container = args.ItemContainer();
    container.Tag(box_value(hstring{user_id}));
    if (self || user_id.empty()) {
        container.ClearValue(xaml::UIElement::ContextFlyoutProperty());
    } else {
        container.ContextFlyout(MemberVolumeFlyout());
    }
    args.Handled(true);
}

void ServerView::OnMemberClick(IInspectable const& sender, controls::ItemClickEventArgs const& args) {
    const auto list = sender.as<controls::ListView>();
    const auto container = list.ContainerFromItem(args.ClickedItem()).try_as<controls::ListViewItem>();
    if (container && container.ContextFlyout()) {
        MemberVolumeFlyout().ShowAt(container);
    }
}

controls::Flyout ServerView::MemberVolumeFlyout() {
    if (member_volume_flyout_) {
        return member_volume_flyout_;
    }

    controls::StackPanel panel;
    panel.Width(260);
    panel.Spacing(10);

    controls::TextBlock title;
    title.Text(L"Member controls");
    title.FontSize(14);
    title.FontWeight(Windows::UI::Text::FontWeight{600});
    panel.Children().Append(title);

    controls::TextBlock hint;
    hint.Text(L"Personal voice volume");
    hint.FontSize(11);
    hint.Foreground(xaml::Application::Current().Resources().Lookup(
        box_value(L"CatroTextTertiaryBrush")).as<Microsoft::UI::Xaml::Media::Brush>());
    panel.Children().Append(hint);

    controls::Slider slider;
    slider.Header(box_value(L"Voice volume — 100%"));
    slider.Minimum(0);
    slider.Maximum(200);
    slider.StepFrequency(1);
    slider.ValueChanged([this](IInspectable const& sender,
                               controls::Primitives::RangeBaseValueChangedEventArgs const& args) {
        sender.as<controls::Slider>().Header(box_value(
            hstring{L"Voice volume — " +
                    std::to_wstring(static_cast<int>(args.NewValue())) + L"%"}));
        if (member_volume_user_.empty()) {
            return;
        }
        auto preferences = catro::shell::voice_preferences();
        preferences.user_volumes[member_volume_user_] =
            static_cast<float>(args.NewValue() / 100.0);
        catro::shell::save_voice_preferences(preferences);
        if (voice_runtime_ != nullptr) {
            catro_voice_runtime_set_user_volume(
                voice_runtime_, member_volume_user_.c_str(),
                static_cast<float>(args.NewValue() / 100.0));
        }
    });
    panel.Children().Append(slider);

    controls::Border divider;
    divider.Height(1);
    divider.Background(xaml::Application::Current().Resources().Lookup(
        box_value(L"CatroStrokeBrush")).as<Microsoft::UI::Xaml::Media::Brush>());
    panel.Children().Append(divider);

    controls::Button remove;
    remove.Content(box_value(L"Remove from server"));
    remove.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
    remove.HorizontalContentAlignment(xaml::HorizontalAlignment::Center);
    remove.Style(xaml::Application::Current().Resources().Lookup(
        box_value(L"CatroDangerButtonStyle")).as<xaml::Style>());
    remove.Visibility(xaml::Visibility::Collapsed);
    remove.Click([this](auto&&, auto&&) {
        const auto target = member_volume_user_;
        if (!target.empty()) {
            ConfirmRemoveMember(target);
        }
    });
    panel.Children().Append(remove);
    member_remove_button_ = remove;

    controls::Flyout flyout;
    flyout.Content(panel);
    flyout.Opening([this](IInspectable const& sender, auto&&) {
        const auto opened = sender.as<controls::Flyout>();
        const auto target = opened.Target();
        member_volume_user_.clear();
        const auto id = target
            ? to_string(unbox_value_or<hstring>(target.Tag(), hstring{}))
            : std::string{};
        auto content = opened.Content().as<controls::StackPanel>();
        content.Children().GetAt(2).as<controls::Slider>().Value(
            catro::shell::user_volume(
                catro::shell::voice_preferences().user_volumes, id) * 100.0);
        member_volume_user_ = id;

        bool removable = false;
        if (directory_server_ && directory_server_->role == "owner" &&
            !moderation_pending_ && !id.empty()) {
            const auto found = std::find_if(
                roster_.begin(), roster_.end(),
                [&](const auto& member) { return member.user_id == id; });
            removable = found != roster_.end() && found->role != "owner";
        }
        member_remove_button_.Visibility(
            removable ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
        member_remove_button_.IsEnabled(removable);
    });
    flyout.Closed([this](auto&&, auto&&) { member_volume_user_.clear(); });
    member_volume_flyout_ = flyout;
    return flyout;
}

controls::Flyout ServerView::MessageAdminFlyout() {
    if (message_admin_flyout_) {
        return message_admin_flyout_;
    }

    controls::StackPanel panel;
    panel.Width(220);
    panel.Spacing(8);

    controls::TextBlock label;
    label.Text(L"OWNER ACTIONS");
    label.FontSize(11);
    label.FontWeight(Windows::UI::Text::FontWeight{600});
    label.CharacterSpacing(70);
    label.Foreground(xaml::Application::Current().Resources().Lookup(
        box_value(L"CatroTextTertiaryBrush")).as<Microsoft::UI::Xaml::Media::Brush>());
    panel.Children().Append(label);

    controls::Button remove;
    remove.Content(box_value(L"Delete message"));
    remove.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
    remove.HorizontalContentAlignment(xaml::HorizontalAlignment::Center);
    remove.Style(xaml::Application::Current().Resources().Lookup(
        box_value(L"CatroDangerButtonStyle")).as<xaml::Style>());
    remove.Click([this](auto&&, auto&&) {
        const auto message_id = message_admin_id_;
        if (!message_id.empty()) {
            ConfirmDeleteMessage(message_id);
        }
    });
    panel.Children().Append(remove);

    controls::Flyout flyout;
    flyout.Content(panel);
    flyout.Opening([this](IInspectable const& sender, auto&&) {
        const auto opened = sender.as<controls::Flyout>();
        const auto target = opened.Target();
        message_admin_id_ = target
            ? to_string(unbox_value_or<hstring>(target.Tag(), hstring{}))
            : std::string{};
        const bool owner = directory_server_ && directory_server_->role == "owner";
        opened.Content().as<controls::StackPanel>().Children().GetAt(1)
            .as<controls::Button>().IsEnabled(
                owner && !moderation_pending_ && !message_admin_id_.empty());
    });
    flyout.Closed([this](auto&&, auto&&) { message_admin_id_.clear(); });
    message_admin_flyout_ = flyout;
    return flyout;
}

winrt::fire_and_forget ServerView::ConfirmRemoveMember(std::string user_id) {
    auto lifetime = get_strong();
    if (moderation_pending_ || !directory_service_ || directory_access_token_.empty() ||
        !directory_server_ || directory_server_->role != "owner" || user_id.empty()) {
        co_return;
    }

    const auto found = std::find_if(
        roster_.begin(), roster_.end(),
        [&](const auto& member) { return member.user_id == user_id; });
    if (found == roster_.end() || found->role == "owner") {
        co_return;
    }
    const auto display_name = found->display_name;

    controls::ContentDialog dialog;
    dialog.XamlRoot(ServerLayout().XamlRoot());
    dialog.RequestedTheme(ActualTheme());
    dialog.Title(box_value(hstring{L"Remove " + to_hstring(display_name) + L"?"}));
    dialog.PrimaryButtonText(L"Remove member");
    dialog.CloseButtonText(L"Cancel");

    controls::TextBlock body;
    body.Text(L"They will lose text and voice access immediately. They can request access again later.");
    body.TextWrapping(xaml::TextWrapping::Wrap);
    dialog.Content(body);

    if (co_await dialog.ShowAsync() != controls::ContentDialogResult::Primary) {
        co_return;
    }
    if (!directory_server_ || directory_server_->role != "owner" ||
        directory_server_->id.empty()) {
        co_return;
    }

    const auto generation = ++moderation_generation_;
    moderation_pending_ = true;
    const auto service = *directory_service_;
    const auto access_token = directory_access_token_;
    const auto server_id = directory_server_->id;
    UiThread ui_thread;

    co_await winrt::resume_background();
    auto result = catro::platform::windows::remove_directory_member(
        service, access_token, server_id, user_id);

    co_await ui_thread;
    if (generation != lifetime->moderation_generation_ ||
        !lifetime->directory_server_ ||
        lifetime->directory_server_->id != server_id) {
        co_return;
    }
    lifetime->moderation_pending_ = false;

    if (const auto* failure =
            std::get_if<catro::platform::windows::DirectoryError>(&result)) {
        lifetime->OnlineStatusText().Text(to_hstring(failure->message));
        co_return;
    }

    lifetime->directory_server_ =
        std::get<catro::platform::windows::DirectoryServer>(std::move(result));
    std::wstring count = L"MEMBERS — ";
    count += std::to_wstring(lifetime->directory_server_->member_count);
    lifetime->MemberCountLabel().Text(hstring{count});
    lifetime->OnlineStatusText().Text(L"Member removed.");
    lifetime->ResetMembers();
    lifetime->BeginMemberRefresh();
}

winrt::fire_and_forget ServerView::ConfirmDeleteMessage(std::string message_id) {
    auto lifetime = get_strong();
    if (moderation_pending_ || !directory_service_ || directory_access_token_.empty() ||
        !directory_server_ || directory_server_->role != "owner" ||
        directory_server_->text_channel_id.empty() || message_id.empty()) {
        co_return;
    }

    controls::ContentDialog dialog;
    dialog.XamlRoot(ServerLayout().XamlRoot());
    dialog.RequestedTheme(ActualTheme());
    dialog.Title(box_value(hstring{L"Delete this message?"}));
    dialog.PrimaryButtonText(L"Delete");
    dialog.CloseButtonText(L"Cancel");

    controls::TextBlock body;
    body.Text(L"This removes the message for everyone in this server.");
    body.TextWrapping(xaml::TextWrapping::Wrap);
    dialog.Content(body);

    if (co_await dialog.ShowAsync() != controls::ContentDialogResult::Primary) {
        co_return;
    }

    const auto generation = ++moderation_generation_;
    moderation_pending_ = true;
    const auto service = *directory_service_;
    const auto access_token = directory_access_token_;
    const auto server_id = directory_server_->id;
    const auto channel_id = directory_server_->text_channel_id;
    UiThread ui_thread;

    co_await winrt::resume_background();
    auto result = catro::platform::windows::delete_directory_message(
        service, access_token, server_id, channel_id, message_id);

    co_await ui_thread;
    if (generation != lifetime->moderation_generation_ ||
        !lifetime->directory_server_ ||
        lifetime->directory_server_->id != server_id ||
        lifetime->directory_server_->text_channel_id != channel_id) {
        co_return;
    }
    lifetime->moderation_pending_ = false;

    if (const auto* failure =
            std::get_if<catro::platform::windows::DirectoryError>(&result)) {
        lifetime->TextStatusText().Text(to_hstring(failure->message));
        lifetime->TextStatusText().Visibility(xaml::Visibility::Visible);
        co_return;
    }

    lifetime->OnlineStatusText().Text(L"Message deleted.");
    lifetime->ResetMessages();
    lifetime->BeginMessageRefresh();
}

void ServerView::ResetMembers() {
    if (++member_generation_ == 0) {
        member_generation_ = 1;
    }
    MemberList().Items().Clear();
    VoiceParticipantList().Items().Clear();
    roster_.clear();
}

void ServerView::ShowLocalMemberFallback() {
    MemberList().Items().Clear();
    if (!local_state_) {
        MemberCountLabel().Text(L"MEMBERS — 0");
        return;
    }

    std::wstring row = L"\ny\n";
    row += to_hstring(local_state_->identity.display_name).c_str();
    row += L"\nOwner · You";
    MemberList().Items().Append(
        box_value(hstring{row}));
    MemberCountLabel().Text(L"MEMBERS — 1");
}

void ServerView::ApplyMemberRoster(
    const std::vector<
        catro::platform::windows::DirectoryMember>& members) {
    roster_ = members;
    RenderMemberRows();

    std::wstring count = L"MEMBERS — ";
    count += std::to_wstring(members.size());
    MemberCountLabel().Text(hstring{count});
}

void ServerView::RenderMemberRows() {
    const auto voice = voice_runtime_ != nullptr ? catro_voice_runtime_snapshot(voice_runtime_)
                                                 : CatroVoiceRuntimeSnapshot{};
    const auto room = room_mode_active_ && room_runtime_ != nullptr
        ? catro_room_runtime_snapshot(room_runtime_) : CatroRoomRuntimeSnapshot{};
    const bool in_voice = room_mode_active_ && voice.state == CATRO_VOICE_JOINED &&
        room.state == CATRO_ROOM_JOINED;
    std::string local_id;
    if (local_state_) {
        local_id =
            catro::community::to_hex(
                local_state_->identity.id);
    }

    auto items = MemberList().Items();
    auto participants = VoiceParticipantList().Items();
    uint32_t index = 0;
    uint32_t participant_index = 0;
    bool found_self = false;
    const auto put_row = [](auto const& rows, uint32_t at, const hstring& value) {
        if (at >= rows.Size()) {
            rows.Append(box_value(value));
        } else if (unbox_value<hstring>(rows.GetAt(at)) != value) {
            rows.SetAt(at, box_value(value));
        }
    };
    for (const auto& member : roster_) {
        const bool self = !local_id.empty() && member.user_id == local_id;
        found_self = found_self || self;
        const bool speaking = in_voice &&
            (self ? voice.speaking != 0
                  : catro_voice_runtime_user_speaking(voice_runtime_, member.user_id.c_str()) != 0);
        std::wstring row = to_hstring(member.user_id).c_str();
        row += L"\n";
        if (speaking) {
            row += L"s";
        }
        if (self) {
            row += L"y";
        }
        row += L"\n";
        row += to_hstring(member.display_name).c_str();
        row += L"\n";
        row += member.role == "owner"
            ? L"Owner"
            : L"Member";
        if (self) {
            row += L" · You";
        }
        const hstring value{row};
        put_row(items, index, value);
        ++index;
        if (directory_server_ && catro::shell::voice_member_visible(
                self, in_voice, member.voice_channel_id, directory_server_->voice_channel_id)) {
            put_row(participants, participant_index++, value);
        }
    }
    if (!found_self && in_voice && local_state_) {
        std::wstring row = to_hstring(local_id).c_str();
        row += voice.speaking != 0 ? L"\nsy\n" : L"\ny\n";
        row += to_hstring(local_state_->identity.display_name).c_str();
        row += L"\nYou";
        put_row(participants, participant_index++, hstring{row});
    }
    while (!roster_.empty() && items.Size() > index) {
        items.RemoveAtEnd();
    }
    while (participants.Size() > participant_index) {
        participants.RemoveAtEnd();
    }
    VoiceParticipantList().Visibility(participant_index == 0
        ? xaml::Visibility::Collapsed : xaml::Visibility::Visible);
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
            message_time(message_local_time(
                request.created_at * 1000));
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
    message_revision_ = 0;
    message_display_day_.reset();
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

    std::wstring display = to_hstring(message.id).c_str();
    display += L"\n";
    display += to_hstring(message.author_id).c_str();
    display += L"\n";
    display += to_hstring(message.author_display_name).c_str();
    display += L"\n";
    display += std::to_wstring(message.created_at);
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

    const auto today = catro::shell::message_day(message_local_time(
        static_cast<std::int64_t>(std::time(nullptr)) * 1000));
    if (message_display_day_ != today) {
        message_display_day_ = today;
        auto rows = MessageList().Items();
        for (uint32_t index = 0; index < rows.Size(); ++index) {
            rows.SetAt(index, rows.GetAt(index));
        }
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

    // A channel deletion epoch changes only when retained history was moderated. Cursor polling
    // cannot represent a removed older row, so rebuild the bounded timeline from the server once.
    if (after != 0 && page.revision != lifetime->message_revision_) {
        lifetime->ResetMessages();
        lifetime->message_revision_ = page.revision;
        lifetime->BeginMessageRefresh();
        co_return;
    }
    lifetime->message_revision_ = page.revision;
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
    OwnerControlsBadge().Visibility(
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

#include "pch.h"

#include "MainWindow.xaml.h"
#include "resource.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "Diagnostics/DiagnosticsView.xaml.h"
#include "Server/ServerView.xaml.h"
#include "Settings/SettingsView.xaml.h"

#include <catro/platform/windows/local_state.hpp>

#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.h>

#include <chrono>
#include <filesystem>

namespace winrt::Catro::implementation {

namespace {

namespace xaml = Microsoft::UI::Xaml;
namespace controls = Microsoft::UI::Xaml::Controls;

} // namespace

void MainWindow::InitializeComponent() {
    MainWindowT<MainWindow>::InitializeComponent();

    ExtendsContentIntoTitleBar(true);
    SetTitleBar(AppTitleBar());

    // Branding animation stays entirely on the compositor thread. No per-frame DispatcherQueue
    // callback is created, so the mascot is effectively free while media/game workloads are active.
    {
        namespace composition = Microsoft::UI::Composition;
        namespace hosting = Microsoft::UI::Xaml::Hosting;
        auto visual = hosting::ElementCompositionPreview::GetElementVisual(CatroMascot());
        auto pulse = visual.Compositor().CreateScalarKeyFrameAnimation();
        pulse.InsertKeyFrame(0.0f, 0.76f);
        pulse.InsertKeyFrame(0.5f, 1.0f);
        pulse.InsertKeyFrame(1.0f, 0.76f);
        pulse.Duration(std::chrono::milliseconds{2800});
        pulse.IterationBehavior(composition::AnimationIterationBehavior::Forever);
        visual.StartAnimation(L"Opacity", pulse);
    }

    const auto transparent = Windows::UI::Color{0, 0, 0, 0};
    auto title_bar = AppWindow().TitleBar();
    title_bar.ButtonBackgroundColor(transparent);
    title_bar.ButtonInactiveBackgroundColor(transparent);

    const auto hwnd =
        Microsoft::UI::GetWindowFromWindowId(AppWindow().Id());

    std::wstring module_path(32768, L'\0');
    const auto module_length = GetModuleFileNameW(
        nullptr, module_path.data(), static_cast<DWORD>(module_path.size()));
    if (module_length > 0 && module_length < module_path.size()) {
        module_path.resize(module_length);
        const auto icon_path =
            std::filesystem::path{module_path}.parent_path() /
            L"Assets" / L"Catro.ico";
        std::error_code icon_error;
        if (std::filesystem::exists(icon_path, icon_error) &&
            !icon_error) {
            try {
                const hstring resolved_icon{icon_path.wstring()};
                AppWindow().SetIcon(resolved_icon);
                AppWindow().SetTaskbarIcon(resolved_icon);
                AppWindow().SetTitleBarIcon(resolved_icon);
            } catch (const winrt::hresult_error&) {
            }
        }
    }

    const auto icon = reinterpret_cast<HICON>(
        LoadImageW(
            GetModuleHandleW(nullptr),
            MAKEINTRESOURCEW(IDI_CATRO_APP),
            IMAGE_ICON,
            0,
            0,
            LR_DEFAULTSIZE | LR_SHARED));
    if (icon != nullptr && hwnd != nullptr) {
        (void)SendMessageW(
            hwnd, WM_SETICON, ICON_BIG,
            reinterpret_cast<LPARAM>(icon));
        (void)SendMessageW(
            hwnd, WM_SETICON, ICON_SMALL,
            reinterpret_cast<LPARAM>(icon));
    }

    const auto scale = GetDpiForWindow(hwnd) / 96.0;
    AppWindow().Resize({static_cast<int32_t>(1280 * scale), static_cast<int32_t>(820 * scale)});

    const auto local = catro::platform::windows::load_or_create_default_local_state();
    if (const auto* state = std::get_if<catro::community::LocalState>(&local)) {
        local_state_ = *state;
        TitleContext().Text(to_hstring(state->personal_server.name));
    } else {
        TitleContext().Text(L"State unavailable");
    }

    Activate(catro::app::AppDestination::server);
    BeginDirectoryBootstrap();
}

void MainWindow::OnServer(IInspectable const&, xaml::RoutedEventArgs const&) {
    if (local_state_) {
        const auto personal_id =
            catro::community::to_hex(
                local_state_->personal_server.id);
        const auto found =
            std::ranges::find(
                directory_servers_,
                personal_id,
                &catro::platform::windows::
                    DirectoryServer::id);
        if (found != directory_servers_.end()) {
            ActivateDirectoryServer(found->id);
            return;
        }
    }
    active_directory_server_.reset();
    Activate(catro::app::AppDestination::server);
}

void MainWindow::OnJoinServer(
    IInspectable const&, xaml::RoutedEventArgs const&) {
    BeginJoinServer();
}

void MainWindow::OnDiagnostics(IInspectable const&, xaml::RoutedEventArgs const&) {
    Activate(catro::app::AppDestination::diagnostics);
}

void MainWindow::OnSettings(IInspectable const&, xaml::RoutedEventArgs const&) {
    Activate(catro::app::AppDestination::settings);
}

xaml::UIElement MainWindow::PageFor(catro::app::AppDestination destination) {
    switch (destination) {
    case catro::app::AppDestination::server:
        if (!server_page_) {
            auto page = Catro::ServerView{};
            if (local_state_) {
                get_self<winrt::Catro::implementation::ServerView>(page)->SetLocalState(*local_state_);
            }
            server_page_ = page;
            ApplyDirectoryServerToPage();
        }
        return server_page_;
    case catro::app::AppDestination::diagnostics:
        if (!diagnostics_page_) {
            diagnostics_page_ = Catro::DiagnosticsView{};
        }
        return diagnostics_page_;
    case catro::app::AppDestination::settings:
        if (!settings_page_) {
            settings_page_ = Catro::SettingsView{};
        }
        return settings_page_;
    }
    return nullptr;
}

void MainWindow::Activate(catro::app::AppDestination destination) {
    const auto previous = shell_state_.destination();
    switch (destination) {
    case catro::app::AppDestination::server:
        (void)shell_state_.open_server();
        if (active_directory_server_) {
            TitleContext().Text(
                to_hstring(
                    active_directory_server_->name));
        } else {
            TitleContext().Text(
                local_state_
                    ? to_hstring(
                          local_state_->
                              personal_server.name)
                    : hstring{L"State unavailable"});
        }
        break;
    case catro::app::AppDestination::diagnostics:
        (void)shell_state_.open_diagnostics();
        TitleContext().Text(L"System");
        break;
    case catro::app::AppDestination::settings:
        (void)shell_state_.open_settings();
        TitleContext().Text(L"Settings");
        break;
    }

    WorkspaceHost().Content(PageFor(destination));

    // Diagnostics can materialize hundreds of evidence rows. Release it when leaving so the normal
    // chat/voice shell remains close to a static tree while a game is running.
    if (previous == catro::app::AppDestination::diagnostics &&
        destination != catro::app::AppDestination::diagnostics) {
        diagnostics_page_ = nullptr;
    }

    UpdateRail();
}

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
        co_return;
    }

    const auto state = *local_state_;
    const auto queue = DispatcherQueue();

    co_await winrt::resume_background();

    const auto config_result =
        catro::platform::windows::
            load_directory_service_config();
    if (const auto* failure =
            std::get_if<
                catro::platform::windows::
                    DirectoryError>(
                &config_result)) {
        if (failure->code !=
            catro::platform::windows::
                DirectoryErrorCode::
                    not_configured) {
            const auto message = failure->message;
            (void)queue.TryEnqueue(
                [lifetime, message] {
                    controls::ToolTipService::
                        SetToolTip(
                            lifetime->
                                JoinServerButton(),
                            box_value(
                                to_hstring(
                                    message)));
                });
        }
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
        (void)queue.TryEnqueue(
            [lifetime, message] {
                controls::ToolTipService::
                    SetToolTip(
                        lifetime->
                            JoinServerButton(),
                        box_value(
                            to_hstring(message)));
            });
        co_return;
    }
    const auto credential =
        std::get<std::string>(
            credential_result);

    const auto registration =
        catro::platform::windows::
            register_directory_identity(
                service,
                state.identity,
                credential);
    if (const auto* failure =
            std::get_if<
                catro::platform::windows::
                    DirectoryError>(
                &registration)) {
        const auto message = failure->message;
        (void)queue.TryEnqueue(
            [lifetime, message] {
                controls::ToolTipService::
                    SetToolTip(
                        lifetime->
                            JoinServerButton(),
                        box_value(
                            to_hstring(message)));
            });
        co_return;
    }
    const auto access_token =
        std::get<std::string>(
            registration);

    const auto synced =
        catro::platform::windows::
            sync_personal_server(
                service,
                access_token,
                state.personal_server);
    if (const auto* failure =
            std::get_if<
                catro::platform::windows::
                    DirectoryError>(
                &synced)) {
        const auto message = failure->message;
        (void)queue.TryEnqueue(
            [lifetime, message] {
                controls::ToolTipService::
                    SetToolTip(
                        lifetime->
                            JoinServerButton(),
                        box_value(
                            to_hstring(message)));
            });
        co_return;
    }
    const auto personal =
        std::get<
            catro::platform::windows::
                DirectoryServer>(synced);

    std::vector<
        catro::platform::windows::
            DirectoryServer>
        servers;
    const auto listed =
        catro::platform::windows::
            list_directory_servers(
                service,
                access_token);
    if (const auto* values =
            std::get_if<
                std::vector<
                    catro::platform::windows::
                        DirectoryServer>>(
                &listed)) {
        servers = *values;
    } else {
        servers.push_back(personal);
    }

    (void)queue.TryEnqueue(
        [lifetime,
         service,
         access_token,
         servers = std::move(servers),
         personal]() mutable {
            lifetime->directory_service_ =
                service;
            lifetime->
                directory_access_token_ =
                access_token;
            lifetime->directory_servers_ =
                std::move(servers);

            const auto found =
                std::ranges::find(
                    lifetime->
                        directory_servers_,
                    personal.id,
                    &catro::platform::windows::
                        DirectoryServer::id);
            lifetime->
                active_directory_server_ =
                found !=
                        lifetime->
                            directory_servers_
                                .end()
                    ? std::optional{
                          *found}
                    : std::optional{
                          personal};

            lifetime->JoinServerButton()
                .IsEnabled(true);
            controls::ToolTipService::
                SetToolTip(
                    lifetime->
                        JoinServerButton(),
                    box_value(
                        hstring{
                            L"Join with invite code"}));
            lifetime->
                RefreshDirectoryRail();
            lifetime->
                ApplyDirectoryServerToPage();
            if (lifetime->
                    shell_state_.destination() ==
                catro::app::
                    AppDestination::server) {
                lifetime->TitleContext().Text(
                    to_hstring(
                        lifetime->
                            active_directory_server_
                            ->name));
            }
        });
}

winrt::fire_and_forget
MainWindow::BeginJoinServer() {
    auto lifetime = get_strong();
    if (!directory_service_ ||
        directory_access_token_.empty()) {
        co_return;
    }

    controls::ContentDialog dialog;
    dialog.XamlRoot(AppTitleBar().XamlRoot());
    dialog.Title(
        box_value(
            hstring{L"Join a Catro server"}));
    dialog.PrimaryButtonText(L"Join");
    dialog.CloseButtonText(L"Cancel");
    dialog.DefaultButton(
        controls::
            ContentDialogButton::Primary);

    controls::TextBox invite_box;
    invite_box.Header(
        box_value(hstring{L"Invite code"}));
    invite_box.PlaceholderText(
        L"Paste invite code");
    dialog.Content(invite_box);

    const auto result =
        co_await dialog.ShowAsync();
    if (result !=
        controls::
            ContentDialogResult::Primary) {
        co_return;
    }

    const auto invite =
        winrt::to_string(
            invite_box.Text());
    if (invite.empty()) {
        co_return;
    }

    const auto service =
        *directory_service_;
    const auto access_token =
        directory_access_token_;
    const auto queue = DispatcherQueue();

    JoinServerButton().IsEnabled(false);
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
                    DirectoryError>(
                &accepted)) {
        const auto message = failure->message;
        (void)queue.TryEnqueue(
            [lifetime, message] {
                lifetime->
                    JoinServerButton()
                    .IsEnabled(true);
                controls::ToolTipService::
                    SetToolTip(
                        lifetime->
                            JoinServerButton(),
                        box_value(
                            to_hstring(
                                message)));
            });
        co_return;
    }

    const auto server =
        std::get<
            catro::platform::windows::
                DirectoryServer>(accepted);
    (void)queue.TryEnqueue(
        [lifetime, server] {
            auto found =
                std::ranges::find(
                    lifetime->
                        directory_servers_,
                    server.id,
                    &catro::platform::windows::
                        DirectoryServer::id);
            if (found ==
                lifetime->
                    directory_servers_
                        .end()) {
                lifetime->
                    directory_servers_
                    .push_back(server);
            } else {
                *found = server;
            }
            lifetime->
                active_directory_server_ =
                server;
            lifetime->
                JoinServerButton()
                .IsEnabled(true);
            lifetime->
                RefreshDirectoryRail();
            lifetime->
                ApplyDirectoryServerToPage();
            lifetime->Activate(
                catro::app::
                    AppDestination::server);
        });
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
        label.FontWeight(
            Windows::UI::Text::
                FontWeights::SemiBold());
        button.Content(label);

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

void MainWindow::UpdateRail() {
    const auto current = shell_state_.destination();
    bool personal_selected =
        current ==
            catro::app::AppDestination::server;
    if (personal_selected &&
        local_state_ &&
        active_directory_server_) {
        personal_selected =
            active_directory_server_->id ==
            catro::community::to_hex(
                local_state_->
                    personal_server.id);
    }
    ServerSelection().Visibility(
        personal_selected
            ? xaml::Visibility::Visible
            : xaml::Visibility::Collapsed);
    DiagnosticsSelection().Visibility(current == catro::app::AppDestination::diagnostics
                                          ? xaml::Visibility::Visible
                                          : xaml::Visibility::Collapsed);
    SettingsSelection().Visibility(current == catro::app::AppDestination::settings
                                       ? xaml::Visibility::Visible
                                       : xaml::Visibility::Collapsed);
}

} // namespace winrt::Catro::implementation

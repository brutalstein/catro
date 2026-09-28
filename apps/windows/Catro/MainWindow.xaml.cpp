#include "pch.h"

#include "MainWindow.xaml.h"
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
        if (std::filesystem::exists(icon_path)) {
            try {
                AppWindow().SetIcon(hstring{icon_path.wstring()});
            } catch (const winrt::hresult_error&) {
            }
        }
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
}

void MainWindow::OnServer(IInspectable const&, xaml::RoutedEventArgs const&) {
    Activate(catro::app::AppDestination::server);
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
        TitleContext().Text(local_state_ ? to_hstring(local_state_->personal_server.name)
                                        : hstring{L"State unavailable"});
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

void MainWindow::UpdateRail() {
    const auto current = shell_state_.destination();
    ServerSelection().Visibility(current == catro::app::AppDestination::server
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

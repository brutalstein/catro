#include "pch.h"

#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "Diagnostics/DiagnosticsView.xaml.h"
#include "Home/HomeView.xaml.h"
#include "Settings/SettingsView.xaml.h"
#include "Share/ShareView.xaml.h"
#include "Voice/VoiceView.xaml.h"

#include <array>
#include <winrt/Windows.UI.h>

namespace winrt::Catro::implementation {
namespace {

namespace xaml = Microsoft::UI::Xaml;
namespace controls = Microsoft::UI::Xaml::Controls;
namespace automation = Microsoft::UI::Xaml::Automation;

} // namespace

void MainWindow::InitializeComponent() {
    MainWindowT<MainWindow>::InitializeComponent();

    // Solid-color chrome is deliberate: no Mica/Acrylic/blur means no persistent backdrop pass
    // competing with the game's GPU queue.
    ExtendsContentIntoTitleBar(true);
    SetTitleBar(AppTitleBar());

    const auto transparent = Windows::UI::Color{0, 0, 0, 0};
    auto title_bar = AppWindow().TitleBar();
    title_bar.ButtonBackgroundColor(transparent);
    title_bar.ButtonInactiveBackgroundColor(transparent);

    // Resize takes physical pixels: scale the logical launch size by the window's effective DPI.
    const auto hwnd = Microsoft::UI::GetWindowFromWindowId(AppWindow().Id());
    const auto scale = GetDpiForWindow(hwnd) / 96.0;
    AppWindow().Resize({static_cast<int32_t>(1280 * scale), static_cast<int32_t>(820 * scale)});

    ShellGrid().SizeChanged([this](auto&&, xaml::SizeChangedEventArgs const& args) {
        UpdateResponsiveLayout(args.NewSize().Width);
    });
    Activate(catro::app::ShellSection::home);
}

void MainWindow::OnNavigationClick(IInspectable const& sender, xaml::RoutedEventArgs const&) {
    const auto button = sender.try_as<controls::Button>();
    if (!button || !button.Tag()) {
        return;
    }
    const auto id = to_string(unbox_value<hstring>(button.Tag()));
    const auto section = catro::app::shell_section_from_id(id);
    if (section) {
        Activate(*section);
    }
}

xaml::UIElement MainWindow::PageFor(catro::app::ShellSection section) {
    switch (section) {
    case catro::app::ShellSection::home:
        if (!home_page_) {
            home_page_ = Catro::HomeView{};
        }
        return home_page_;
    case catro::app::ShellSection::voice:
        if (!voice_page_) {
            voice_page_ = Catro::VoiceView{};
        }
        return voice_page_;
    case catro::app::ShellSection::share:
        if (!share_page_) {
            share_page_ = Catro::ShareView{};
        }
        return share_page_;
    case catro::app::ShellSection::diagnostics:
        if (!diagnostics_page_) {
            diagnostics_page_ = Catro::DiagnosticsView{};
        }
        return diagnostics_page_;
    case catro::app::ShellSection::settings:
        if (!settings_page_) {
            settings_page_ = Catro::SettingsView{};
        }
        return settings_page_;
    }
    return nullptr;
}

void MainWindow::Activate(catro::app::ShellSection section) {
    (void)shell_state_.activate(section);
    const auto& spec = shell_state_.active_spec();

    TitleSection().Text(to_hstring(spec.title));
    ContextEyebrow().Text(to_hstring(spec.eyebrow));
    ContextTitle().Text(to_hstring(spec.title));
    ContextSummary().Text(to_hstring(spec.summary));
    ContextEmptyTitle().Text(to_hstring(spec.empty_title));
    ContextEmptyDetail().Text(to_hstring(spec.empty_detail));

    // ContentControl holds exactly one live subtree. Pages are allocated on first visit and reused,
    // so startup avoids constructing diagnostics/probe controls or future heavy media surfaces.
    WorkspaceHost().Content(PageFor(section));
    UpdateNavigationVisuals(section);
    UpdateResponsiveLayout(ShellGrid().ActualWidth());
}

void MainWindow::UpdateResponsiveLayout(double width) {
    const bool diagnostics = shell_state_.active() == catro::app::ShellSection::diagnostics;
    const double context_width = diagnostics ? 0.0 : (width >= 1040.0 ? 248.0 : (width >= 800.0 ? 220.0 : 0.0));
    ContextColumn().Width(xaml::GridLengthHelper::FromPixels(context_width));
    ContextPane().Visibility(context_width > 0.0 ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
}

void MainWindow::UpdateNavigationVisuals(catro::app::ShellSection section) {
    struct Item {
        catro::app::ShellSection section;
        controls::Button button;
        controls::Border selection;
    };
    const std::array items{
        Item{catro::app::ShellSection::home, HomeButton(), HomeSelection()},
        Item{catro::app::ShellSection::voice, VoiceButton(), VoiceSelection()},
        Item{catro::app::ShellSection::share, ShareButton(), ShareSelection()},
        Item{catro::app::ShellSection::diagnostics, DiagnosticsButton(), DiagnosticsSelection()},
        Item{catro::app::ShellSection::settings, SettingsButton(), SettingsSelection()},
    };

    for (const auto& item : items) {
        const bool active = item.section == section;
        item.selection.Visibility(active ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
        item.button.Opacity(active ? 1.0 : 0.78);
        automation::AutomationProperties::SetHelpText(
            item.button, active ? L"Current section" : L"Open section");
    }
}

} // namespace winrt::Catro::implementation

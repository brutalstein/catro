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

namespace winrt::Catro::implementation {
namespace {

namespace xaml = Microsoft::UI::Xaml;
namespace controls = Microsoft::UI::Xaml::Controls;
namespace media = Microsoft::UI::Xaml::Media;

media::Brush brush(wchar_t const* key) {
    return xaml::Application::Current().Resources().Lookup(box_value(key)).as<media::Brush>();
}

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
}

void MainWindow::UpdateNavigationVisuals(catro::app::ShellSection section) {
    const auto selected_background = brush(L"CatroAccentSoftBrush");
    const auto selected_foreground = brush(L"CatroAccentBrush");
    const auto idle_background = brush(L"CatroRailBrush");
    const auto idle_foreground = brush(L"CatroTextSecondaryBrush");

    const std::array items{
        std::pair{catro::app::ShellSection::home, HomeButton()},
        std::pair{catro::app::ShellSection::voice, VoiceButton()},
        std::pair{catro::app::ShellSection::share, ShareButton()},
        std::pair{catro::app::ShellSection::diagnostics, DiagnosticsButton()},
        std::pair{catro::app::ShellSection::settings, SettingsButton()},
    };

    for (const auto& [candidate, button] : items) {
        const bool active = candidate == section;
        button.Background(active ? selected_background : idle_background);
        button.Foreground(active ? selected_foreground : idle_foreground);
        automation::AutomationProperties::SetHelpText(
            button, active ? L"Current section" : L"Open section");
    }
}

} // namespace winrt::Catro::implementation

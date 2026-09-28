#include "pch.h"

#include "Diagnostics/DiagnosticsView.xaml.h"
#if __has_include("DiagnosticsView.g.cpp")
#include "DiagnosticsView.g.cpp"
#endif

#include "Audio/AudioView.xaml.h"

#include <ShObjIdl_core.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <utility>

namespace winrt::Catro::implementation {

DiagnosticsView::DiagnosticsView() {
    InitializeComponent();
}

namespace {

namespace xaml = Microsoft::UI::Xaml;
namespace controls = Microsoft::UI::Xaml::Controls;
namespace automation = Microsoft::UI::Xaml::Automation;
using catro::app::Tone;
using catro::reporting::FactState;

struct NavigationEntry {
    std::string_view id;
    wchar_t const* glyph;
};

// Segoe Fluent Icons glyphs, in view-model section order after the overview.
constexpr std::array kNavigation{
    NavigationEntry{"overview", L"\xE80F"},  NavigationEntry{"plan", L"\xE768"},
    NavigationEntry{"fallbacks", L"\xE8FD"}, NavigationEntry{"decisions", L"\xE71C"},
    NavigationEntry{"profile", L"\xE713"},   NavigationEntry{"probes", L"\xE9D9"},
    NavigationEntry{"devices", L"\xE7F4"},   NavigationEntry{"system", L"\xE770"},
    NavigationEntry{"runtime", L"\xE823"},   NavigationEntry{"snapshot", L"\xE8A5"},
};

std::filesystem::path application_directory() {
    std::wstring buffer(MAX_PATH, L'\0');
    while (true) {
        const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            return std::filesystem::path(buffer).parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
}

wchar_t const* badge_style(Tone tone) {
    switch (tone) {
    case Tone::positive:
        return L"PositiveBadge";
    case Tone::caution:
        return L"CautionBadge";
    case Tone::critical:
        return L"CriticalBadge";
    case Tone::neutral:
        break;
    }
    return L"NeutralBadge";
}

// Known facts carry no badge; anything short of known is marked so it is never mistaken for one.
std::optional<std::pair<Tone, std::wstring_view>> fact_badge(FactState state) {
    switch (state) {
    case FactState::degraded:
        return std::pair{Tone::caution, std::wstring_view{L"degraded"}};
    case FactState::unknown:
        return std::pair{Tone::neutral, std::wstring_view{L"unknown"}};
    case FactState::unavailable:
        return std::pair{Tone::neutral, std::wstring_view{L"unavailable"}};
    case FactState::plain:
    case FactState::known:
        break;
    }
    return std::nullopt;
}

std::wstring_view tone_name(Tone tone) {
    switch (tone) {
    case Tone::positive:
        return L"healthy";
    case Tone::caution:
        return L"attention";
    case Tone::critical:
        return L"problem";
    case Tone::neutral:
        break;
    }
    return L"information";
}

xaml::Style style(xaml::FrameworkElement const& owner, wchar_t const* key) {
    return owner.Resources().Lookup(box_value(key)).as<xaml::Style>();
}

controls::TextBlock text_block(xaml::FrameworkElement const& owner, wchar_t const* key, hstring const& text) {
    controls::TextBlock block;
    block.Style(style(owner, key));
    block.Text(text);
    return block;
}

xaml::FrameworkElement badge(xaml::FrameworkElement const& owner, Tone tone, std::wstring_view text) {
    controls::Border border;
    border.Style(style(owner, badge_style(tone)));
    border.Child(text_block(owner, L"BadgeText", hstring(text)));
    return border;
}

// Label, value, and an optional badge; the whole row is one accessible item.
xaml::UIElement fact_row(xaml::FrameworkElement const& owner, catro::app::DiagnosticsRow const& row) {
    controls::Grid grid;
    grid.ColumnSpacing(16);
    grid.Padding(xaml::ThicknessHelper::FromLengths(8.0 + 20.0 * row.depth, 4, 8, 4));
    const auto label = to_hstring(row.list_item ? "• " + row.label : row.label);
    if (row.value.empty()) {
        grid.Children().Append(text_block(owner, L"HeadingText", label));
        automation::AutomationProperties::SetName(grid, label);
        return grid;
    }
    controls::ColumnDefinition label_column;
    label_column.Width(xaml::GridLengthHelper::FromPixels(std::max(120.0, 260.0 - 20.0 * row.depth)));
    controls::ColumnDefinition value_column;
    value_column.Width(xaml::GridLengthHelper::FromValueAndType(1, xaml::GridUnitType::Star));
    controls::ColumnDefinition badge_column;
    badge_column.Width(xaml::GridLengthHelper::Auto());
    grid.ColumnDefinitions().Append(label_column);
    grid.ColumnDefinitions().Append(value_column);
    grid.ColumnDefinitions().Append(badge_column);

    const auto value = to_hstring(row.value);
    grid.Children().Append(text_block(owner, L"LabelText", label));
    auto value_block = text_block(owner, L"ValueText", value);
    controls::Grid::SetColumn(value_block, 1);
    grid.Children().Append(value_block);
    std::wstring name = std::wstring(label) + L": " + std::wstring(value);
    if (const auto marker = fact_badge(row.state)) {
        auto element = badge(owner, marker->first, marker->second);
        controls::Grid::SetColumn(element, 2);
        grid.Children().Append(element);
        name += L", " + std::wstring(marker->second);
    }
    automation::AutomationProperties::SetName(grid, hstring(name));
    return grid;
}

xaml::UIElement group_heading(xaml::FrameworkElement const& owner, hstring const& text) {
    auto heading = text_block(owner, L"GroupHeadingText", text);
    heading.Margin(xaml::ThicknessHelper::FromLengths(8, 20, 8, 6));
    automation::AutomationProperties::SetHeadingLevel(heading, automation::Peers::AutomationHeadingLevel::Level2);
    return heading;
}

} // namespace

DiagnosticsView::~DiagnosticsView() {
    Stop();
}

void DiagnosticsView::InitializeComponent() {
    DiagnosticsViewT<DiagnosticsView>::InitializeComponent();
    Loaded([this](auto&&, auto&&) { Start(); });
    Unloaded([this](auto&&, auto&&) { Stop(); });
}

void DiagnosticsView::Start() {
    if (service_) {
        return;
    }
    service_ = std::make_unique<catro::platform::windows::CapabilityService>(application_directory() /
                                                                           L"catro-capability-probe.exe");
    // Planning runs on the service thread; only the finished model crosses to the UI thread.
    service_->start([weak = get_weak(), dispatcher = DispatcherQueue()](catro::capabilities::SnapshotUpdate update) {
        auto report =
            catro::reporting::make_report(std::move(update.snapshot), catro::reporting::representative_request());
        auto model = catro::app::build_diagnostics(report, update.changes);
        dispatcher.TryEnqueue([weak, report = std::move(report), model = std::move(model)] {
            if (auto self = weak.get()) {
                self->Apply(report, model);
            }
        });
    });
}

void DiagnosticsView::Stop() {
    if (service_) {
        service_->stop();
        service_.reset();
    }
}

void DiagnosticsView::Apply(catro::reporting::CapabilityReport report, catro::app::DiagnosticsModel model) {
    report_ = std::move(report);
    model_ = std::move(model);
    get_self<AudioView>(AudioPanel())->SetEndpoints(report_->snapshot);
    if (!navigation_built_) {
        BuildNavigation();
    }
    std::size_t attention = 0;
    for (const auto& probe : model_.probes) {
        attention += probe.tone == Tone::positive ? 0 : 1;
    }
    std::wstring status = std::wstring(to_hstring(model_.headline)) + L" · generation " +
                          std::to_wstring(model_.generation) + L" · " + std::to_wstring(model_.probes.size()) +
                          L" probes";
    if (attention > 0) {
        status += L", " + std::to_wstring(attention) + L" need attention";
    }
    StatusLine().Text(hstring(status));
    ExportButton().IsEnabled(true);
    if (model_.generation > 1 && !model_.changes.empty()) {
        std::wstring message;
        for (std::size_t index = 0; index < model_.changes.size() && index < 6; ++index) {
            message += (index == 0 ? L"" : L", ") + std::wstring(to_hstring(model_.changes[index]));
        }
        if (model_.changes.size() > 6) {
            message += L", +" + std::to_wstring(model_.changes.size() - 6) + L" more";
        }
        Announce(controls::InfoBarSeverity::Informational, L"Capabilities changed", hstring(message));
    }
    Show(selected_);
}

void DiagnosticsView::BuildNavigation() {
    auto items = Navigation().MenuItems();
    controls::NavigationViewItem first{nullptr};
    for (const auto& entry : kNavigation) {
        hstring title = L"Overview";
        if (entry.id != "overview") {
            const auto found = std::ranges::find(model_.sections, entry.id, &catro::app::DiagnosticsSection::id);
            if (found == model_.sections.end()) {
                continue;
            }
            title = to_hstring(found->title);
        }
        controls::NavigationViewItem item;
        item.Content(box_value(title));
        item.Tag(box_value(to_hstring(entry.id)));
        controls::FontIcon icon;
        icon.Glyph(entry.glyph);
        item.Icon(icon);
        items.Append(item);
        if (!first) {
            first = item;
        }
    }
    navigation_built_ = true;
    Navigation().SelectedItem(first);
}

void DiagnosticsView::OnSelectionChanged(controls::NavigationView const&,
                                         controls::NavigationViewSelectionChangedEventArgs const& args) {
    if (const auto item = args.SelectedItem().try_as<controls::NavigationViewItem>()) {
        selected_ = to_string(unbox_value<hstring>(item.Tag()));
        Show(selected_);
    }
}

void DiagnosticsView::Show(std::string const& section) {
    // The audio page stops its session when hidden, so no device stays open out of view.
    const bool audio = section == "audio";
    AudioPanel().Visibility(audio ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    Rows().Visibility(audio ? xaml::Visibility::Collapsed : xaml::Visibility::Visible);
    if (audio) {
        SectionTitle().Text(L"Audio test");
        return;
    }
    get_self<AudioView>(AudioPanel())->StopSession();
    auto rows = Rows().Items();
    rows.Clear();
    if (!report_) {
        return;
    }
    if (section == "overview") {
        ShowOverview();
        return;
    }
    const auto found = std::ranges::find(model_.sections, section, &catro::app::DiagnosticsSection::id);
    if (found == model_.sections.end()) {
        return;
    }
    SectionTitle().Text(to_hstring(found->title));
    for (const auto& row : found->rows) {
        rows.Append(fact_row(*this, row));
    }
    if (found->omitted_rows > 0) {
        rows.Append(text_block(*this, L"LabelText",
                               hstring(std::to_wstring(found->omitted_rows) + L" more rows are in the exported report.")));
    }
}

void DiagnosticsView::ShowOverview() {
    auto rows = Rows().Items();
    SectionTitle().Text(L"Overview");

    controls::StackPanel summary;
    summary.Spacing(8);
    summary.Padding(xaml::ThicknessHelper::FromLengths(8, 8, 8, 8));
    controls::StackPanel headline;
    headline.Orientation(controls::Orientation::Horizontal);
    headline.Spacing(12);
    headline.Children().Append(text_block(*this, L"GroupHeadingText", to_hstring(model_.headline)));
    headline.Children().Append(badge(*this, model_.tone, tone_name(model_.tone)));
    summary.Children().Append(headline);
    summary.Children().Append(text_block(*this, L"ValueText", to_hstring(model_.detail)));
    automation::AutomationProperties::SetName(summary, to_hstring(model_.headline + ". " + model_.detail));
    rows.Append(summary);

    rows.Append(group_heading(*this, L"Probe health"));
    for (const auto& probe : model_.probes) {
        controls::Grid grid;
        grid.ColumnSpacing(16);
        grid.Padding(xaml::ThicknessHelper::FromLengths(8, 4, 8, 4));
        for (const auto width : {260.0, 160.0, 0.0}) {
            controls::ColumnDefinition column;
            column.Width(width > 0 ? xaml::GridLengthHelper::FromPixels(width)
                                   : xaml::GridLengthHelper::FromValueAndType(1, xaml::GridUnitType::Star));
            grid.ColumnDefinitions().Append(column);
        }
        grid.Children().Append(text_block(*this, L"ValueText", to_hstring(probe.probe_id)));
        auto outcome = badge(*this, probe.tone, std::wstring(to_hstring(probe.outcome)));
        outcome.HorizontalAlignment(xaml::HorizontalAlignment::Left);
        controls::Grid::SetColumn(outcome, 1);
        grid.Children().Append(outcome);
        const auto timing = std::wstring(to_hstring(probe.duration)) + L" · " + std::to_wstring(probe.facts) + L" facts";
        auto timing_block = text_block(*this, L"LabelText", hstring(timing));
        controls::Grid::SetColumn(timing_block, 2);
        grid.Children().Append(timing_block);
        automation::AutomationProperties::SetName(
            grid, hstring(std::wstring(to_hstring(probe.probe_id + ": " + probe.outcome)) + L", " + timing));
        rows.Append(grid);
    }

    rows.Append(group_heading(*this, L"Latest refresh"));
    if (model_.generation <= 1) {
        rows.Append(text_block(*this, L"LabelText", L"First publication; no earlier generation to compare."));
    } else if (model_.changes.empty()) {
        rows.Append(text_block(*this, L"LabelText", L"No capability changes since the previous generation."));
    }
    for (const auto& change : model_.changes) {
        rows.Append(text_block(*this, L"ValueText", to_hstring(change)));
    }
}

void DiagnosticsView::OnRefresh(IInspectable const&, xaml::RoutedEventArgs const&) {
    if (service_) {
        service_->refresh(catro::capabilities::RefreshReason::diagnostics);
        StatusLine().Text(L"Refreshing passive capability evidence...");
    }
}

void DiagnosticsView::OnCopyReport(IInspectable const&, xaml::RoutedEventArgs const&) {
    if (!report_) {
        return;
    }
    Windows::ApplicationModel::DataTransfer::DataPackage package;
    package.SetText(to_hstring(catro::app::export_report(*report_, catro::app::ExportFormat::human)));
    Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(package);
    Announce(controls::InfoBarSeverity::Success, L"Report copied",
             L"Device names are redacted; identifiers remain, so the report is not anonymous.");
}

void DiagnosticsView::OnSaveText(IInspectable const&, xaml::RoutedEventArgs const&) {
    Save(catro::app::ExportFormat::human);
}

void DiagnosticsView::OnSaveJson(IInspectable const&, xaml::RoutedEventArgs const&) {
    Save(catro::app::ExportFormat::json);
}

// The save dialog confirms replacing an existing file; the write goes through a temporary
// sibling so the target is never left partially written.
void DiagnosticsView::Save(catro::app::ExportFormat format) {
    if (!report_) {
        return;
    }
    const bool json = format == catro::app::ExportFormat::json;
    const auto owner = Microsoft::UI::GetWindowFromWindowId(XamlRoot().ContentIslandEnvironment().AppWindowId());
    auto dialog = create_instance<IFileSaveDialog>(CLSID_FileSaveDialog);
    const COMDLG_FILTERSPEC filter{json ? L"Canonical JSON" : L"Text report", json ? L"*.json" : L"*.txt"};
    check_hresult(dialog->SetFileTypes(1, &filter));
    check_hresult(dialog->SetDefaultExtension(json ? L"json" : L"txt"));
    check_hresult(dialog->SetFileName(json ? L"catro-capabilities.json" : L"catro-capabilities.txt"));
    if (FAILED(dialog->Show(owner))) {
        return;
    }
    com_ptr<IShellItem> item;
    wchar_t* chosen = nullptr;
    if (FAILED(dialog->GetResult(item.put())) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &chosen))) {
        Announce(controls::InfoBarSeverity::Error, L"Export failed", L"The chosen location is not a file path.");
        return;
    }
    const std::filesystem::path target(chosen);
    CoTaskMemFree(chosen);

    const auto text = catro::app::export_report(*report_, format);
    auto temporary = target;
    temporary += L".partial";
    bool written = false;
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        written = static_cast<bool>(file.flush());
    }
    if (!written ||
        !MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        Announce(controls::InfoBarSeverity::Error, L"Export failed", hstring(L"Could not write " + target.wstring()));
        return;
    }
    Announce(controls::InfoBarSeverity::Success, L"Report saved", hstring(target.wstring()));
}

void DiagnosticsView::Announce(controls::InfoBarSeverity severity, hstring const& title, hstring const& message) {
    Notice().Severity(severity);
    Notice().Title(title);
    Notice().Message(message);
    Notice().IsOpen(true);
}

} // namespace winrt::Catro::implementation

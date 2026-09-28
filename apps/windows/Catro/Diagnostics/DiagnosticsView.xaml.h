#pragma once

#include "DiagnosticsView.g.h"

#include <DiagnosticsViewModel.hpp>

#include <catro/platform/windows/capability_service.hpp>

#include <memory>
#include <optional>
#include <string>

namespace winrt::Catro::implementation {

// The diagnostics workspace: an overview plus one navigation entry per view-model section.
// Evidence arrives on the capability service thread and is applied on the UI thread.
struct DiagnosticsView : DiagnosticsViewT<DiagnosticsView> {
    DiagnosticsView();
    ~DiagnosticsView();

    void InitializeComponent();

    void OnSelectionChanged(Microsoft::UI::Xaml::Controls::NavigationView const&,
                            Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const&);
    void OnRefresh(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnCopyReport(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnSaveText(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnSaveJson(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

private:
    void Start();
    void Stop();
    void Apply(catro::reporting::CapabilityReport report, catro::app::DiagnosticsModel model);
    void BuildNavigation();
    void Show(std::string const& section);
    void ShowOverview();
    void Save(catro::app::ExportFormat format);
    void Announce(Microsoft::UI::Xaml::Controls::InfoBarSeverity severity, hstring const& title,
                  hstring const& message);

    std::unique_ptr<catro::platform::windows::CapabilityService> service_;
    std::optional<catro::reporting::CapabilityReport> report_;
    catro::app::DiagnosticsModel model_;
    std::string selected_{"overview"};
    bool navigation_built_ = false;
};

} // namespace winrt::Catro::implementation

namespace winrt::Catro::factory_implementation {

struct DiagnosticsView : DiagnosticsViewT<DiagnosticsView, implementation::DiagnosticsView> {};

} // namespace winrt::Catro::factory_implementation

#include "pch.h"

#include "Settings/SettingsView.xaml.h"
#if __has_include("SettingsView.g.cpp")
#include "SettingsView.g.cpp"
#endif

namespace winrt::Catro::implementation {

SettingsView::SettingsView() {
    InitializeComponent();
}


void SettingsView::InitializeComponent() {
    SettingsViewT<SettingsView>::InitializeComponent();
}

} // namespace winrt::Catro::implementation

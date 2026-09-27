#pragma once

#include "MainWindow.g.h"

namespace winrt::Catro::implementation {

struct MainWindow : MainWindowT<MainWindow> {
    MainWindow() = default;

    // Named elements exist only after the generated initialization; the title bar needs them.
    void InitializeComponent();
};

} // namespace winrt::Catro::implementation

namespace winrt::Catro::factory_implementation {

struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};

} // namespace winrt::Catro::factory_implementation

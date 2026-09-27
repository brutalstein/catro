#include "pch.h"

#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

namespace winrt::Catro::implementation {

void MainWindow::InitializeComponent() {
    MainWindowT<MainWindow>::InitializeComponent();
    ExtendsContentIntoTitleBar(true);
    SetTitleBar(AppTitleBar());
    // Resize takes physical pixels: scale the default size by the window's effective DPI.
    const auto hwnd = Microsoft::UI::GetWindowFromWindowId(AppWindow().Id());
    const auto scale = GetDpiForWindow(hwnd) / 96.0;
    AppWindow().Resize({static_cast<int32_t>(1280 * scale), static_cast<int32_t>(840 * scale)});
}

} // namespace winrt::Catro::implementation

#include "pch.h"

#include "App.xaml.h"
#include "MainWindow.xaml.h"

namespace winrt::Catro::implementation {

App::App() {
#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
    UnhandledException([](IInspectable const&, Microsoft::UI::Xaml::UnhandledExceptionEventArgs const&) {
        if (IsDebuggerPresent()) {
            __debugbreak();
        }
    });
#endif
}

void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&) {
    window_ = make<MainWindow>();
    window_.Activate();
}

} // namespace winrt::Catro::implementation

// Replaces the XAML-generated entry point (DISABLE_XAML_GENERATED_MAIN), which initializes the UI
// thread in the MTA. In the MTA, cross-process UI Automation clients (Narrator, screen readers,
// background accessibility tools) cannot walk the XAML island and could crash the process.
int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    winrt::Microsoft::UI::Xaml::Application::Start([](auto&&) {
        winrt::make<winrt::Catro::implementation::App>();
    });
    return 0;
}

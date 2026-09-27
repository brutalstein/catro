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

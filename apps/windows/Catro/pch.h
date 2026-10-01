#pragma once

#include <Windows.h>
#include <unknwn.h>
#include <restrictederrorinfo.h>
#include <hstring.h>

// Undefine GetCurrentTime macro to prevent conflict with Storyboard::GetCurrentTime.
#undef GetCurrentTime

#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>
#include <winrt/Microsoft.UI.h>
#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Navigation.h>

#include <coroutine>

// Captures the calling UI thread and resumes a coroutine on it. WinUI 3's generated wWinMain puts
// the UI thread in the MTA, so winrt::apartment_context cannot marshal back to it and the next XAML
// call would fail with RPC_E_WRONG_THREAD; the thread's DispatcherQueue always can.
class UiThread {
public:
    UiThread() : queue_(winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread()) {}

    bool await_ready() const noexcept { return queue_.HasThreadAccess(); }

    void await_suspend(std::coroutine_handle<> resume) const {
        // A refused enqueue means the window is shutting down; the continuation is dropped rather
        // than resumed on a thread that cannot touch XAML.
        (void)queue_.TryEnqueue([resume] { resume(); });
    }

    void await_resume() const noexcept {}

private:
    winrt::Microsoft::UI::Dispatching::DispatcherQueue queue_;
};

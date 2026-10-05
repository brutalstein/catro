#include "pch.h"

#include "MainWindow.xaml.h"

#include <AppUpdate.hpp>

#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Filters.h>
#include <winrt/Windows.Web.Http.Headers.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace winrt::Catro::implementation {

namespace {

namespace xaml = Microsoft::UI::Xaml;
namespace controls = Microsoft::UI::Xaml::Controls;
namespace http = Windows::Web::Http;
using namespace std::chrono_literals;

std::filesystem::path app_directory() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) {
            return {};
        }
        if (length < path.size()) {
            path.resize(length);
            return std::filesystem::path{path}.parent_path();
        }
        path.resize(path.size() * 2);
    }
}

// Packaged installs carry the installer the update runs; development builds have none and never
// offer updates.
std::filesystem::path bundled_installer() {
    return app_directory() / L"install-windows.ps1";
}

hstring display_version(std::string_view tag) {
    return to_hstring(tag.starts_with('v') ? tag.substr(1) : tag);
}

std::string read_status(const std::filesystem::path& file) {
    std::ifstream input(file);
    std::string status;
    std::getline(input, status);
    // Windows PowerShell writes UTF-8 with a byte order mark and CRLF line ends.
    if (status.starts_with("\xEF\xBB\xBF")) {
        status.erase(0, 3);
    }
    while (!status.empty() && (status.back() == '\r' || status.back() == ' ')) {
        status.pop_back();
    }
    return status;
}

controls::ContentDialog make_dialog(xaml::XamlRoot const& root, hstring const& title, hstring const& text) {
    controls::ContentDialog dialog;
    dialog.XamlRoot(root);
    dialog.Title(box_value(title));
    controls::TextBlock body;
    body.Text(text);
    body.TextWrapping(xaml::TextWrapping::Wrap);
    dialog.Content(body);
    return dialog;
}

} // namespace

void MainWindow::StartUpdateChecks() {
    std::error_code error;
    if (!std::filesystem::exists(bundled_installer(), error)) {
        return;
    }
    update_timer_ = DispatcherQueue().CreateTimer();
    update_timer_.Interval(6h);
    update_timer_.Tick([this](auto&&, auto&&) { BeginUpdateCheck(); });
    update_timer_.Start();
    BeginUpdateCheck();
}

winrt::fire_and_forget MainWindow::BeginUpdateCheck() {
    auto lifetime = get_strong();
    UiThread ui_thread;
    std::string tag;
    co_await winrt::resume_background();
    try {
        // The latest-release page redirects to /releases/tag/<tag>; reading the redirect instead of
        // following it costs one small request and no GitHub API quota.
        http::Filters::HttpBaseProtocolFilter filter;
        filter.AllowAutoRedirect(false);
        filter.CacheControl().ReadBehavior(http::Filters::HttpCacheReadBehavior::MostRecent);
        http::HttpClient client{filter};
        const auto response = co_await client.GetAsync(
            Windows::Foundation::Uri{to_hstring(catro::app::kLatestReleaseUrl)},
            http::HttpCompletionOption::ResponseHeadersRead);
        if (const auto location = response.Headers().Location()) {
            tag = catro::app::release_tag(to_string(location.AbsoluteUri())).value_or("");
        }
    } catch (...) {
        // Offline or GitHub unreachable: the next check tries again.
    }
    co_await ui_thread;
    if (window_closed_ || installing_update_ ||
        !catro::app::is_newer_release(tag, catro::app::app_version())) {
        co_return;
    }
    available_update_ = tag;
    const auto label = L"Update to Catro " + display_version(tag);
    controls::ToolTipService::SetToolTip(UpdateButton(), box_value(label));
    xaml::Automation::AutomationProperties::SetName(UpdateButton(), label);
    UpdateButton().Visibility(xaml::Visibility::Visible);
}

void MainWindow::OnInstallUpdate(IInspectable const&, xaml::RoutedEventArgs const&) {
    if (available_update_.empty() || installing_update_) {
        return;
    }
    auto dialog = make_dialog(Content().XamlRoot(),
        L"Catro " + display_version(available_update_) + L" is ready",
        L"Catro downloads the update, closes, and opens again on the new version in a few seconds. "
        L"Calls and streams stop during the restart.");
    dialog.PrimaryButtonText(L"Install update");
    dialog.CloseButtonText(L"Later");
    dialog.DefaultButton(controls::ContentDialogButton::Primary);
    dialog.PrimaryButtonClick([this](auto&&, auto&&) { InstallUpdate(); });
    (void)dialog.ShowAsync();
}

winrt::fire_and_forget MainWindow::InstallUpdate() {
    auto lifetime = get_strong();
    UiThread ui_thread;
    installing_update_ = true;

    // Like Discord, the startup screen covers the app while the update downloads.
    StartupOfflineButton().Visibility(xaml::Visibility::Collapsed);
    StartupStatusText().Text(L"Downloading Catro " + display_version(available_update_) + L"…");
    StartupRing().IsActive(true);
    StartupSplash().Opacity(1.0);
    StartupSplash().IsHitTestVisible(true);
    StartupSplash().Visibility(xaml::Visibility::Visible);

    const auto fail = [this](std::string const& reason) {
        installing_update_ = false;
        StartupRing().IsActive(false);
        StartupSplash().Visibility(xaml::Visibility::Collapsed);
        auto dialog = make_dialog(Content().XamlRoot(), L"Update failed",
            L"Catro is unchanged. " + to_hstring(reason) + L" Try again from the update button.");
        dialog.CloseButtonText(L"OK");
        (void)dialog.ShowAsync();
    };

    // The installer replaces this app's folder, so it runs from a copy in the temp directory.
    std::error_code error;
    const auto directory = std::filesystem::temp_directory_path(error) / L"catro-update";
    const auto script = directory / L"install-windows.ps1";
    const auto status = directory / L"status.txt";
    std::filesystem::create_directories(directory, error);
    std::filesystem::remove(status, error);
    std::filesystem::copy_file(bundled_installer(), script,
                               std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        fail("The installer could not be prepared.");
        co_return;
    }

    // The installer inherits these through this process's environment.
    SetEnvironmentVariableW(L"CATRO_VERSION", to_hstring(available_update_).c_str());
    SetEnvironmentVariableW(L"CATRO_INSTALL_ROOT", app_directory().c_str());
    SetEnvironmentVariableW(L"CATRO_WAIT_PID", std::to_wstring(GetCurrentProcessId()).c_str());
    SetEnvironmentVariableW(L"CATRO_STATUS_FILE", status.c_str());
    SetEnvironmentVariableW(L"CATRO_RELAUNCH", L"1");
    wchar_t system[MAX_PATH]{};
    GetSystemDirectoryW(system, MAX_PATH);
    std::wstring command = L"\"" + std::wstring{system} +
        L"\\WindowsPowerShell\\v1.0\\powershell.exe\" -NoProfile -NonInteractive "
        L"-ExecutionPolicy Bypass -File \"" + script.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    // The temp working directory keeps the app folder free to be moved.
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        directory.c_str(), &startup, &process)) {
        fail("The installer could not be started.");
        co_return;
    }
    CloseHandle(process.hThread);
    const winrt::handle installer{process.hProcess};

    // The installer writes 'ready' once the new build is downloaded and verified, then waits for this
    // process to exit before it swaps the files and reopens Catro.
    for (;;) {
        co_await winrt::resume_after(400ms);
        co_await ui_thread;
        if (window_closed_) {
            co_return;
        }
        const bool exited = WaitForSingleObject(installer.get(), 0) == WAIT_OBJECT_0;
        const auto state = read_status(status);
        if (state == "ready") {
            StartupStatusText().Text(L"Restarting Catro…");
            Close();
            xaml::Application::Current().Exit();
            co_return;
        }
        if (state.starts_with("failed")) {
            fail(state.size() > 8 ? state.substr(8) : std::string{});
            co_return;
        }
        if (exited) {
            fail("The download did not finish.");
            co_return;
        }
    }
}

} // namespace winrt::Catro::implementation

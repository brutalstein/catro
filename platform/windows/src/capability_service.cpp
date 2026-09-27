#include <catro/platform/windows/capability_service.hpp>

#include <Windows.h>
#include <WtsApi32.h>
#include <dbt.h>
#include <powersetting.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <utility>

namespace catro::platform::windows {
namespace {

namespace caps = catro::capabilities;

constexpr UINT kRefreshMessage = WM_APP + 1;
constexpr UINT_PTR kDebounceTimer = 1;
constexpr UINT kDebounceMilliseconds = 100;
constexpr wchar_t kWindowClass[] = L"CatroCapabilityServiceWindow";

} // namespace

struct CapabilityService::Impl {
    explicit Impl(std::filesystem::path helper_path) : executor(std::move(helper_path)) {}

    void start(UpdateCallback next_callback) {
        std::unique_lock lock(lifecycle_mutex);
        if (running) {
            return;
        }
        callback = std::move(next_callback);
        ready = false;
        running = true;
        worker = std::thread([this] { run(); });
        ready_changed.wait(lock, [this] { return ready; });
    }

    void request(caps::RefreshReason reason) {
        std::scoped_lock lock(lifecycle_mutex);
        if (running && window != nullptr) {
            PostMessageW(window, kRefreshMessage, static_cast<WPARAM>(reason), 0);
        }
    }

    void stop() {
        std::thread stopping;
        {
            std::scoped_lock lock(lifecycle_mutex);
            if (!worker.joinable()) {
                running = false;
                callback = {};
                return;
            }
            if (window != nullptr) {
                PostMessageW(window, WM_CLOSE, 0, 0);
            }
            stopping = std::move(worker);
        }
        stopping.join();
        std::scoped_lock lock(lifecycle_mutex);
        running = false;
        window = nullptr;
        callback = {};
    }

    static LRESULT CALLBACK window_procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
            self = static_cast<Impl*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self == nullptr) {
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }

        switch (message) {
        case kRefreshMessage:
            self->queue(static_cast<caps::RefreshReason>(wparam));
            return 0;
        case WM_POWERBROADCAST:
            if (wparam == PBT_APMPOWERSTATUSCHANGE || wparam == PBT_POWERSETTINGCHANGE) {
                self->queue(caps::RefreshReason::power);
            }
            return TRUE;
        case WM_DISPLAYCHANGE:
            self->queue(caps::RefreshReason::display);
            return 0;
        case WM_DEVICECHANGE:
            // Adapter arrival and removal surface only as a device-tree change.
            if (wparam == DBT_DEVNODES_CHANGED) {
                self->queue(caps::RefreshReason::display);
            }
            return TRUE;
        case WM_WTSSESSION_CHANGE:
            self->queue(caps::RefreshReason::session);
            return 0;
        case WM_TIMER:
            if (wparam == kDebounceTimer) {
                KillTimer(hwnd, kDebounceTimer);
                auto reasons = std::exchange(self->pending, {});
                self->publish(reasons);
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }
    }

    void run() {
        const auto instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW type{
            .cbSize = sizeof(WNDCLASSEXW),
            .lpfnWndProc = window_procedure,
            .hInstance = instance,
            .lpszClassName = kWindowClass,
        };
        if (RegisterClassExW(&type) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            signal_ready(nullptr, false);
            return;
        }

        // A never-shown top-level window: message-only windows miss the WM_DISPLAYCHANGE and
        // WM_DEVICECHANGE broadcasts.
        const auto hwnd = CreateWindowExW(0, kWindowClass, L"", 0, 0, 0, 0, 0, nullptr, nullptr, instance, this);
        if (hwnd == nullptr) {
            signal_ready(nullptr, false);
            return;
        }
        const auto ac_notification = RegisterPowerSettingNotification(hwnd, &GUID_ACDC_POWER_SOURCE,
                                                                      DEVICE_NOTIFY_WINDOW_HANDLE);
        const auto saver_notification = RegisterPowerSettingNotification(hwnd, &GUID_POWER_SAVING_STATUS,
                                                                         DEVICE_NOTIFY_WINDOW_HANDLE);
        const auto session_registered = WTSRegisterSessionNotification(hwnd, NOTIFY_FOR_THIS_SESSION) != FALSE;
        signal_ready(hwnd, true);
        queue(caps::RefreshReason::startup);

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        if (session_registered) {
            WTSUnRegisterSessionNotification(hwnd);
        }
        if (ac_notification != nullptr) {
            UnregisterPowerSettingNotification(ac_notification);
        }
        if (saver_notification != nullptr) {
            UnregisterPowerSettingNotification(saver_notification);
        }
        std::scoped_lock lock(lifecycle_mutex);
        window = nullptr;
        running = false;
    }

    void signal_ready(HWND hwnd, bool started) {
        {
            std::scoped_lock lock(lifecycle_mutex);
            window = hwnd;
            running = started;
            ready = true;
        }
        ready_changed.notify_all();
    }

    void queue(caps::RefreshReason reason) {
        pending.insert(reason);
        SetTimer(window, kDebounceTimer, kDebounceMilliseconds, nullptr);
    }

    void publish(const std::set<caps::RefreshReason>& reasons) {
        if (reasons.empty()) {
            return;
        }
        const auto generation = current ? current->header.generation + 1 : 1;
        const auto captured_at = std::chrono::time_point_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now());
        auto schedule = caps::make_initial_probe_schedule(caps::OperatingSystem::windows, generation, captured_at);
        schedule.reason = *reasons.begin();
        if (current) {
            schedule.previous = *current;
        }

        if (current) {
            std::erase_if(schedule.probes, [&reasons](const caps::ProbeSpec& spec) {
                return std::ranges::none_of(
                    reasons, [&spec](caps::RefreshReason reason) { return caps::refreshes(reason, spec.family); });
            });
        }

        auto publication = caps::collect_snapshot(schedule, executor);
        if (!publication.snapshot) {
            return;
        }
        auto update = caps::make_snapshot_update(current ? &*current : nullptr, std::move(*publication.snapshot));
        if (!update.update) {
            return;
        }
        auto published = std::move(*update.update);
        current = published.snapshot;
        try {
            callback(std::move(published));
        } catch (...) {
            // Client diagnostics must not tear down the notification thread.
        }
    }

    ProcessProbeExecutor executor;
    std::mutex lifecycle_mutex;
    std::condition_variable ready_changed;
    std::thread worker;
    HWND window = nullptr;
    bool ready = false;
    bool running = false;
    UpdateCallback callback;
    std::optional<caps::CapabilitySnapshot> current;
    std::set<caps::RefreshReason> pending;
};

CapabilityService::CapabilityService(std::filesystem::path helper_path)
    : impl_(std::make_unique<Impl>(std::move(helper_path))) {}

CapabilityService::~CapabilityService() {
    stop();
}

void CapabilityService::start(UpdateCallback callback) {
    impl_->start(std::move(callback));
}

void CapabilityService::refresh(caps::RefreshReason reason) {
    impl_->request(reason);
}

void CapabilityService::stop() {
    impl_->stop();
}

} // namespace catro::platform::windows

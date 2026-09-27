#include <catro/platform/macos/capability_service.hpp>

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreGraphics/CoreGraphics.h>
#include <IOKit/ps/IOPSKeys.h>
#include <dispatch/dispatch.h>
#include <notify.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace catro::platform::macos {
namespace {

namespace caps = catro::capabilities;

constexpr std::int64_t kDebounceNanoseconds = 100 * NSEC_PER_MSEC;
// Identifies the service queue, so stop() called from an update callback does not wait on itself.
constexpr char kQueueKey = 0;

// Endpoint list and default device changes; a reconfigured device reappears in the list.
constexpr std::array<AudioObjectPropertyAddress, 4> kAudioAddresses{{
    {kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain},
    {kAudioHardwarePropertyDefaultInputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain},
    {kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain},
    {kAudioHardwarePropertyDefaultSystemOutputDevice, kAudioObjectPropertyScopeGlobal,
     kAudioObjectPropertyElementMain},
}};

// Shared with every notification block, so a notification that races stop() only finds a
// stopped core instead of freed memory. Fields below `queue` are touched only on `queue`.
struct Core : std::enable_shared_from_this<Core> {
    explicit Core(std::filesystem::path helper_path) : executor(std::move(helper_path)) {
        queue = dispatch_queue_create("com.catro.capabilities", DISPATCH_QUEUE_SERIAL);
        dispatch_queue_set_specific(queue, &kQueueKey, this, nullptr);
    }

    [[nodiscard]] bool on_queue() const { return dispatch_get_specific(&kQueueKey) == this; }

    // Safe from any thread.
    void post(caps::RefreshReason reason) {
        auto core = shared_from_this();
        dispatch_async(queue, ^{
          core->schedule(reason);
        });
    }

    // Trailing debounce: a burst of notifications becomes one refresh 100 ms after the last.
    void schedule(caps::RefreshReason reason) {
        if (!running) {
            return;
        }
        pending.insert(reason);
        const auto token = ++debounce_token;
        auto core = shared_from_this();
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, kDebounceNanoseconds), queue, ^{
          if (core->running && core->debounce_token == token) {
              core->publish(std::exchange(core->pending, {}));
          }
        });
    }

    void publish(const std::set<caps::RefreshReason>& reasons) {
        if (reasons.empty()) {
            return;
        }
        const auto generation = current ? current->header.generation + 1 : 1;
        const auto captured_at =
            std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::system_clock::now());
        auto schedule = caps::make_initial_probe_schedule(caps::OperatingSystem::macos, generation, captured_at);
        schedule.reason = *reasons.begin();
        if (current) {
            schedule.previous = *current;
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
            // Client diagnostics must not tear down the service queue.
        }
    }

    ProcessProbeExecutor executor;
    dispatch_queue_t queue;
    bool running = false;
    CapabilityService::UpdateCallback callback;
    std::optional<caps::CapabilitySnapshot> current;
    std::set<caps::RefreshReason> pending;
    std::uint64_t debounce_token = 0;
};

void display_changed(CGDirectDisplayID, CGDisplayChangeSummaryFlags flags, void* context) {
    // Every reconfiguration is announced twice; refresh once it has completed.
    if ((flags & kCGDisplayBeginConfigurationFlag) != 0) {
        return;
    }
    if (const auto core = static_cast<std::weak_ptr<Core>*>(context)->lock()) {
        core->post(caps::RefreshReason::display);
    }
}

} // namespace

struct CapabilityService::Impl {
    explicit Impl(std::filesystem::path helper_path) : core(std::make_shared<Core>(std::move(helper_path))) {}

    void start(UpdateCallback next_callback) {
        std::scoped_lock lock(lifecycle_mutex);
        if (started) {
            return;
        }
        started = true;
        const auto shared = core;
        const auto activate = ^{
          shared->callback = next_callback;
          shared->running = true;
          shared->current.reset();
          shared->pending.clear();
        };
        if (core->on_queue()) {
            activate();
        } else {
            dispatch_sync(core->queue, activate);
        }
        observe();
        core->post(caps::RefreshReason::startup);
    }

    void refresh(caps::RefreshReason reason) {
        std::scoped_lock lock(lifecycle_mutex);
        if (started) {
            core->post(reason);
        }
    }

    void stop() {
        std::scoped_lock lock(lifecycle_mutex);
        if (!started) {
            return;
        }
        started = false;
        unobserve();
        // Waits for a refresh in progress; late notifications then find the core stopped.
        const auto shared = core;
        const auto deactivate = ^{
          shared->running = false;
          shared->callback = {};
          shared->pending.clear();
          ++shared->debounce_token;
        };
        if (core->on_queue()) {
            deactivate();
        } else {
            dispatch_sync(core->queue, deactivate);
        }
    }

    // Passive subscriptions only: none of these start capture, open audio streams, or prompt.
    void observe() {
        const auto shared = core;
        const auto on = [shared](caps::RefreshReason reason) {
            return ^(NSNotification*) {
              shared->post(reason);
            };
        };
        NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
        center_tokens.push_back([center addObserverForName:NSProcessInfoThermalStateDidChangeNotification
                                                    object:nil
                                                     queue:nil
                                                usingBlock:on(caps::RefreshReason::thermal)]);
        center_tokens.push_back([center addObserverForName:NSProcessInfoPowerStateDidChangeNotification
                                                    object:nil
                                                     queue:nil
                                                usingBlock:on(caps::RefreshReason::power)]);
        NSNotificationCenter* workspace = NSWorkspace.sharedWorkspace.notificationCenter;
        workspace_tokens.push_back([workspace addObserverForName:NSWorkspaceSessionDidBecomeActiveNotification
                                                          object:nil
                                                           queue:nil
                                                      usingBlock:on(caps::RefreshReason::session)]);
        workspace_tokens.push_back([workspace addObserverForName:NSWorkspaceSessionDidResignActiveNotification
                                                          object:nil
                                                           queue:nil
                                                      usingBlock:on(caps::RefreshReason::session)]);

        memory_source = dispatch_source_create(DISPATCH_SOURCE_TYPE_MEMORYPRESSURE, 0,
                                               DISPATCH_MEMORYPRESSURE_NORMAL | DISPATCH_MEMORYPRESSURE_WARN |
                                                   DISPATCH_MEMORYPRESSURE_CRITICAL,
                                               core->queue);
        if (memory_source != nil) {
            dispatch_source_set_event_handler(memory_source, ^{
              shared->schedule(caps::RefreshReason::memory_pressure);
            });
            dispatch_resume(memory_source);
        }

        power_registered = notify_register_dispatch(kIOPSNotifyPowerSource, &power_token, core->queue, ^(int) {
                             shared->schedule(caps::RefreshReason::power);
                           }) == NOTIFY_STATUS_OK;

        audio_listener = ^(UInt32, const AudioObjectPropertyAddress*) {
          shared->schedule(caps::RefreshReason::audio);
        };
        for (const auto& address : kAudioAddresses) {
            if (AudioObjectAddPropertyListenerBlock(kAudioObjectSystemObject, &address, core->queue, audio_listener) ==
                noErr) {
                audio_addresses.push_back(address);
            }
        }

        display_context = new std::weak_ptr<Core>(core);
        if (CGDisplayRegisterReconfigurationCallback(display_changed, display_context) != kCGErrorSuccess) {
            delete display_context;
            display_context = nullptr;
        }
    }

    void unobserve() {
        for (id token : center_tokens) {
            [NSNotificationCenter.defaultCenter removeObserver:token];
        }
        center_tokens.clear();
        for (id token : workspace_tokens) {
            [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:token];
        }
        workspace_tokens.clear();
        if (memory_source != nil) {
            dispatch_source_cancel(memory_source);
            memory_source = nil;
        }
        if (power_registered) {
            notify_cancel(power_token);
            power_registered = false;
        }
        for (const auto& address : audio_addresses) {
            AudioObjectRemovePropertyListenerBlock(kAudioObjectSystemObject, &address, core->queue, audio_listener);
        }
        audio_addresses.clear();
        audio_listener = nil;
        if (display_context != nullptr) {
            CGDisplayRemoveReconfigurationCallback(display_changed, display_context);
            // Display callbacks run on the main thread; free the context after any in flight.
            auto* context = std::exchange(display_context, nullptr);
            dispatch_async(dispatch_get_main_queue(), ^{
              delete context;
            });
        }
    }

    std::shared_ptr<Core> core;
    std::mutex lifecycle_mutex;
    bool started = false;
    std::vector<id> center_tokens;
    std::vector<id> workspace_tokens;
    dispatch_source_t memory_source = nil;
    int power_token = 0;
    bool power_registered = false;
    AudioObjectPropertyListenerBlock audio_listener = nil;
    std::vector<AudioObjectPropertyAddress> audio_addresses;
    std::weak_ptr<Core>* display_context = nullptr;
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
    impl_->refresh(reason);
}

void CapabilityService::stop() {
    impl_->stop();
}

} // namespace catro::platform::macos

#include "worker_priority.hpp"

#include <new>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <avrt.h>
#elif defined(__APPLE__)
#include <pthread.h>
#endif

namespace catro::tools {

struct VoiceWorkerPriority::Impl {
    bool active = false;

#if defined(_WIN32)
    HANDLE task = nullptr;

    ~Impl() {
        if (task != nullptr) {
            AvRevertMmThreadCharacteristics(task);
        }
    }
#elif defined(__APPLE__)
    qos_class_t previous = QOS_CLASS_DEFAULT;
    int previous_relative = 0;

    ~Impl() {
        if (active) {
            (void)pthread_set_qos_class_self_np(previous, previous_relative);
        }
    }
#else
    ~Impl() = default;
#endif
};

VoiceWorkerPriority::VoiceWorkerPriority() noexcept
    : impl_(std::unique_ptr<Impl>(new (std::nothrow) Impl())) {
    if (!impl_) {
        return;
    }

#if defined(_WIN32)
    DWORD task_index = 0;
    impl_->task = AvSetMmThreadCharacteristicsW(L"Audio", &task_index);
    if (impl_->task != nullptr) {
        (void)AvSetMmThreadPriority(impl_->task, AVRT_PRIORITY_HIGH);
        impl_->active = true;
    }
#elif defined(__APPLE__)
    impl_->previous = pthread_get_qos_class_np(pthread_self(), &impl_->previous_relative);
    if (pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0) == 0) {
        impl_->active = true;
    }
#endif
}

VoiceWorkerPriority::~VoiceWorkerPriority() = default;

bool VoiceWorkerPriority::elevated() const noexcept {
    return impl_ && impl_->active;
}

} // namespace catro::tools

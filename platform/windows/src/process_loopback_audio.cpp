#include <catro/platform/windows/process_loopback_audio.hpp>

#include <Windows.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <new>
#include <span>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace catro::platform::windows {
namespace {

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::FtmBase;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
using Microsoft::WRL::ClassicCom;

using Handle =
    std::unique_ptr<std::remove_pointer_t<HANDLE>,
                    decltype(&CloseHandle)>;

[[nodiscard]] Handle make_event(
    bool manual_reset = false) noexcept {
    return {
        CreateEventW(
            nullptr,
            manual_reset ? TRUE : FALSE,
            FALSE,
            nullptr),
        &CloseHandle};
}

[[nodiscard]] StreamAudioError error_from_hresult(
    StreamAudioErrorCode code,
    HRESULT result) noexcept {
    return StreamAudioError{
        code,
        static_cast<std::int64_t>(result)};
}

[[nodiscard]] WAVEFORMATEX stereo_format() noexcept {
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    format.nChannels =
        static_cast<WORD>(kStreamAudioChannels);
    format.nSamplesPerSec = kStreamAudioSampleRate;
    format.wBitsPerSample = 32;
    format.nBlockAlign = static_cast<WORD>(
        format.nChannels *
        format.wBitsPerSample / 8U);
    format.nAvgBytesPerSec =
        format.nSamplesPerSec *
        format.nBlockAlign;
    return format;
}

class ActivationHandler final
    : public RuntimeClass<
          RuntimeClassFlags<ClassicCom>,
          FtmBase,
          IActivateAudioInterfaceCompletionHandler> {
public:
    ActivationHandler(
        HANDLE completed,
        ComPtr<IAudioClient>* destination,
        HRESULT* result) noexcept
        : completed_(completed),
          destination_(destination),
          result_(result) {}

    STDMETHODIMP ActivateCompleted(
        IActivateAudioInterfaceAsyncOperation*
            operation) override {
        HRESULT activation = E_UNEXPECTED;
        ComPtr<IUnknown> unknown;
        auto result = operation != nullptr
            ? operation->GetActivateResult(
                  &activation, &unknown)
            : E_POINTER;
        if (SUCCEEDED(result)) {
            result = activation;
        }
        if (SUCCEEDED(result)) {
            result = unknown.As(destination_);
        }
        if (result_ != nullptr) {
            *result_ = result;
        }
        if (completed_ != nullptr) {
            SetEvent(completed_);
        }
        return S_OK;
    }

private:
    HANDLE completed_ = nullptr;
    ComPtr<IAudioClient>* destination_ = nullptr;
    HRESULT* result_ = nullptr;
};

[[nodiscard]] HRESULT activate_process_loopback(
    std::uint32_t process_id,
    HANDLE completed,
    ComPtr<IAudioClient>& client) noexcept {
    AUDIOCLIENT_ACTIVATION_PARAMS activation{};
    activation.ActivationType =
        AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    activation.ProcessLoopbackParams.TargetProcessId =
        static_cast<DWORD>(process_id);
    activation.ProcessLoopbackParams.ProcessLoopbackMode =
        PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

    PROPVARIANT parameters{};
    parameters.vt = VT_BLOB;
    parameters.blob.cbSize =
        sizeof(activation);
    parameters.blob.pBlobData =
        reinterpret_cast<BYTE*>(&activation);

    HRESULT activation_result = E_PENDING;
    auto handler =
        Microsoft::WRL::Make<ActivationHandler>(
            completed,
            &client,
            &activation_result);
    if (!handler) {
        return E_OUTOFMEMORY;
    }

    ComPtr<IActivateAudioInterfaceAsyncOperation>
        operation;
    const auto begin = ActivateAudioInterfaceAsync(
        VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
        __uuidof(IAudioClient),
        &parameters,
        handler.Get(),
        &operation);
    if (FAILED(begin)) {
        return begin;
    }

    const auto waited =
        WaitForSingleObject(completed, 10'000);
    if (waited != WAIT_OBJECT_0) {
        return waited == WAIT_TIMEOUT
            ? HRESULT_FROM_WIN32(ERROR_TIMEOUT)
            : HRESULT_FROM_WIN32(GetLastError());
    }
    return activation_result;
}

void publish_error(
    std::mutex& mutex,
    std::optional<StreamAudioError>& destination,
    StreamAudioError error) noexcept {
    try {
        std::scoped_lock lock(mutex);
        destination = error;
    } catch (...) {
    }
}

[[nodiscard]] StreamAudioStatistics snapshot(
    const std::atomic_bool& running,
    const std::atomic<std::uint64_t>& callbacks,
    const std::atomic<std::uint64_t>& frames,
    const std::atomic<std::uint64_t>& glitches,
    std::mutex& error_mutex,
    const std::optional<StreamAudioError>& error) noexcept {
    StreamAudioStatistics result;
    result.running =
        running.load(std::memory_order_acquire);
    result.callbacks =
        callbacks.load(std::memory_order_relaxed);
    result.frames =
        frames.load(std::memory_order_relaxed);
    result.glitches =
        glitches.load(std::memory_order_relaxed);
    try {
        std::scoped_lock lock(error_mutex);
        result.error = error;
    } catch (...) {
    }
    return result;
}

} // namespace

struct ProcessLoopbackAudioCapture::Impl {
    std::optional<StreamAudioError> start(
        std::uint32_t process_id,
        Sink next_sink) {
        stop();

        if (process_id == 0 || !next_sink) {
            return StreamAudioError{
                StreamAudioErrorCode::initialization_failed,
                E_INVALIDARG};
        }

        sink_ = std::move(next_sink);
        stop_event_ = make_event();
        if (!stop_event_) {
            return StreamAudioError{
                StreamAudioErrorCode::os_failure,
                static_cast<std::int64_t>(
                    HRESULT_FROM_WIN32(
                        GetLastError()))};
        }

        std::promise<std::optional<StreamAudioError>>
            ready;
        auto result = ready.get_future();
        try {
            worker_ = std::thread(
                [this,
                 process_id,
                 ready = std::move(ready)]() mutable {
                    run(process_id, std::move(ready));
                });
        } catch (...) {
            sink_ = {};
            stop_event_.reset();
            return StreamAudioError{
                StreamAudioErrorCode::worker_start_failed,
                E_FAIL};
        }

        auto failure = result.get();
        if (failure) {
            stop();
        }
        return failure;
    }

    void stop() noexcept {
        if (stop_event_) {
            SetEvent(stop_event_.get());
        }
        if (worker_.joinable()) {
            worker_.join();
        }
        stop_event_.reset();
        sink_ = {};
        running_.store(
            false, std::memory_order_release);
    }

    void run(
        std::uint32_t process_id,
        std::promise<
            std::optional<StreamAudioError>> ready) noexcept {
        const auto apartment =
            CoInitializeEx(
                nullptr, COINIT_MULTITHREADED);
        if (FAILED(apartment)) {
            const auto failure =
                error_from_hresult(
                    StreamAudioErrorCode::
                        activation_failed,
                    apartment);
            publish_error(
                error_mutex_, error_, failure);
            ready.set_value(failure);
            return;
        }

        const auto cleanup = [&] {
            CoUninitialize();
        };

        auto activation_event = make_event();
        auto sample_event = make_event();
        if (!activation_event || !sample_event) {
            const auto failure =
                StreamAudioError{
                    StreamAudioErrorCode::os_failure,
                    static_cast<std::int64_t>(
                        HRESULT_FROM_WIN32(
                            GetLastError()))};
            publish_error(
                error_mutex_, error_, failure);
            ready.set_value(failure);
            cleanup();
            return;
        }

        ComPtr<IAudioClient> client;
        auto result = activate_process_loopback(
            process_id,
            activation_event.get(),
            client);
        if (FAILED(result)) {
            const auto code =
                result == E_NOTIMPL ||
                        result ==
                            HRESULT_FROM_WIN32(
                                ERROR_NOT_SUPPORTED)
                    ? StreamAudioErrorCode::unsupported
                    : StreamAudioErrorCode::
                          activation_failed;
            const auto failure =
                error_from_hresult(code, result);
            publish_error(
                error_mutex_, error_, failure);
            ready.set_value(failure);
            cleanup();
            return;
        }

        auto format = stereo_format();
        result = client->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK |
                AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            0,
            0,
            &format,
            nullptr);
        if (FAILED(result)) {
            const auto failure =
                error_from_hresult(
                    StreamAudioErrorCode::
                        initialization_failed,
                    result);
            publish_error(
                error_mutex_, error_, failure);
            ready.set_value(failure);
            cleanup();
            return;
        }

        UINT32 buffer_frames = 0;
        ComPtr<IAudioCaptureClient> capture;
        result = client->GetBufferSize(
            &buffer_frames);
        if (SUCCEEDED(result)) {
            result = client->GetService(
                IID_PPV_ARGS(&capture));
        }
        if (SUCCEEDED(result)) {
            result = client->SetEventHandle(
                sample_event.get());
        }

        std::vector<float> silence;
        if (SUCCEEDED(result)) {
            try {
                silence.assign(
                    static_cast<std::size_t>(
                        buffer_frames) *
                        kStreamAudioChannels,
                    0.0F);
            } catch (...) {
                result = E_OUTOFMEMORY;
            }
        }

        if (SUCCEEDED(result)) {
            result = client->Start();
        }
        if (FAILED(result)) {
            const auto failure =
                error_from_hresult(
                    StreamAudioErrorCode::
                        initialization_failed,
                    result);
            publish_error(
                error_mutex_, error_, failure);
            ready.set_value(failure);
            cleanup();
            return;
        }

        {
            std::scoped_lock lock(error_mutex_);
            error_.reset();
        }
        running_.store(
            true, std::memory_order_release);
        ready.set_value(std::nullopt);

        DWORD task_index = 0;
        const auto mmcss =
            AvSetMmThreadCharacteristicsW(
                L"Pro Audio", &task_index);

        const HANDLE waits[] = {
            stop_event_.get(),
            sample_event.get(),
        };

        bool failed = false;
        while (!failed) {
            const auto signaled =
                WaitForMultipleObjects(
                    2, waits, FALSE, 2000);
            if (signaled == WAIT_OBJECT_0) {
                break;
            }
            if (signaled == WAIT_FAILED) {
                publish_error(
                    error_mutex_,
                    error_,
                    StreamAudioError{
                        StreamAudioErrorCode::os_failure,
                        static_cast<std::int64_t>(
                            HRESULT_FROM_WIN32(
                                GetLastError()))});
                break;
            }
            if (signaled != WAIT_OBJECT_0 + 1) {
                continue;
            }

            callbacks_.fetch_add(
                1, std::memory_order_relaxed);

            UINT32 packet_frames = 0;
            while (SUCCEEDED(
                       result =
                           capture->GetNextPacketSize(
                               &packet_frames)) &&
                   packet_frames != 0) {
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                result = capture->GetBuffer(
                    &data,
                    &frames,
                    &flags,
                    nullptr,
                    nullptr);
                if (FAILED(result)) {
                    failed = true;
                    break;
                }

                if ((flags &
                     AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) !=
                    0) {
                    glitches_.fetch_add(
                        1, std::memory_order_relaxed);
                }

                const auto samples =
                    static_cast<std::size_t>(
                        frames) *
                    kStreamAudioChannels;
                if ((flags &
                     AUDCLNT_BUFFERFLAGS_SILENT) !=
                    0) {
                    sink_(
                        std::span<const float>(
                            silence.data(),
                            std::min(
                                samples,
                                silence.size())));
                } else if (data != nullptr) {
                    sink_(
                        std::span<const float>(
                            reinterpret_cast<
                                const float*>(data),
                            samples));
                }

                frames_.fetch_add(
                    frames,
                    std::memory_order_relaxed);
                result =
                    capture->ReleaseBuffer(frames);
                if (FAILED(result)) {
                    failed = true;
                    break;
                }
            }
        }

        client->Stop();
        if (mmcss != nullptr) {
            AvRevertMmThreadCharacteristics(mmcss);
        }

        if (failed) {
            publish_error(
                error_mutex_,
                error_,
                error_from_hresult(
                    StreamAudioErrorCode::os_failure,
                    result));
        }
        running_.store(
            false, std::memory_order_release);
        cleanup();
    }

    Sink sink_;
    Handle stop_event_{nullptr, &CloseHandle};
    std::thread worker_;

    std::atomic_bool running_{false};
    std::atomic<std::uint64_t> callbacks_{0};
    std::atomic<std::uint64_t> frames_{0};
    std::atomic<std::uint64_t> glitches_{0};
    mutable std::mutex error_mutex_;
    std::optional<StreamAudioError> error_;
};

ProcessLoopbackAudioCapture::
    ProcessLoopbackAudioCapture()
    : impl_(std::make_unique<Impl>()) {}

ProcessLoopbackAudioCapture::
    ~ProcessLoopbackAudioCapture() {
    impl_->stop();
}

std::optional<StreamAudioError>
ProcessLoopbackAudioCapture::start(
    std::uint32_t process_id,
    Sink sink) {
    return impl_->start(
        process_id, std::move(sink));
}

void ProcessLoopbackAudioCapture::stop() noexcept {
    impl_->stop();
}

StreamAudioStatistics
ProcessLoopbackAudioCapture::statistics()
    const noexcept {
    return snapshot(
        impl_->running_,
        impl_->callbacks_,
        impl_->frames_,
        impl_->glitches_,
        impl_->error_mutex_,
        impl_->error_);
}

struct WasapiStreamAudioRenderer::Impl {
    std::optional<StreamAudioError> start(
        Source next_source) {
        stop();

        if (!next_source) {
            return StreamAudioError{
                StreamAudioErrorCode::
                    initialization_failed,
                E_INVALIDARG};
        }

        source_ = std::move(next_source);
        stop_event_ = make_event();
        if (!stop_event_) {
            return StreamAudioError{
                StreamAudioErrorCode::os_failure,
                static_cast<std::int64_t>(
                    HRESULT_FROM_WIN32(
                        GetLastError()))};
        }

        std::promise<std::optional<StreamAudioError>>
            ready;
        auto result = ready.get_future();
        try {
            worker_ = std::thread(
                [this,
                 ready = std::move(ready)]() mutable {
                    run(std::move(ready));
                });
        } catch (...) {
            source_ = {};
            stop_event_.reset();
            return StreamAudioError{
                StreamAudioErrorCode::worker_start_failed,
                E_FAIL};
        }

        auto failure = result.get();
        if (failure) {
            stop();
        }
        return failure;
    }

    void stop() noexcept {
        if (stop_event_) {
            SetEvent(stop_event_.get());
        }
        if (worker_.joinable()) {
            worker_.join();
        }
        stop_event_.reset();
        source_ = {};
        running_.store(
            false, std::memory_order_release);
    }

    void run(
        std::promise<
            std::optional<StreamAudioError>> ready) noexcept {
        const auto apartment =
            CoInitializeEx(
                nullptr, COINIT_MULTITHREADED);
        if (FAILED(apartment)) {
            const auto failure =
                error_from_hresult(
                    StreamAudioErrorCode::os_failure,
                    apartment);
            publish_error(
                error_mutex_, error_, failure);
            ready.set_value(failure);
            return;
        }

        ComPtr<IMMDeviceEnumerator> devices;
        ComPtr<IMMDevice> device;
        ComPtr<IAudioClient> client;
        ComPtr<IAudioRenderClient> render;

        auto result = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            IID_PPV_ARGS(&devices));
        if (SUCCEEDED(result)) {
            result =
                devices->GetDefaultAudioEndpoint(
                    eRender,
                    eCommunications,
                    &device);
        }
        if (SUCCEEDED(result)) {
            result = device->Activate(
                __uuidof(IAudioClient),
                CLSCTX_ALL,
                nullptr,
                &client);
        }

        auto sample_event = make_event();
        if (SUCCEEDED(result) && !sample_event) {
            result = HRESULT_FROM_WIN32(
                GetLastError());
        }

        auto format = stereo_format();
        if (SUCCEEDED(result)) {
            result = client->Initialize(
                AUDCLNT_SHAREMODE_SHARED,
                AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                    AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                    AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                0,
                0,
                &format,
                nullptr);
        }

        UINT32 buffer_frames = 0;
        if (SUCCEEDED(result)) {
            result =
                client->GetBufferSize(
                    &buffer_frames);
        }
        if (SUCCEEDED(result)) {
            result =
                client->GetService(
                    IID_PPV_ARGS(&render));
        }
        if (SUCCEEDED(result)) {
            result =
                client->SetEventHandle(
                    sample_event.get());
        }

        if (FAILED(result)) {
            const auto failure =
                error_from_hresult(
                    device
                        ? StreamAudioErrorCode::
                              initialization_failed
                        : StreamAudioErrorCode::
                              device_unavailable,
                    result);
            publish_error(
                error_mutex_, error_, failure);
            ready.set_value(failure);
            CoUninitialize();
            return;
        }

        BYTE* initial = nullptr;
        result = render->GetBuffer(
            buffer_frames, &initial);
        if (SUCCEEDED(result)) {
            result = render->ReleaseBuffer(
                buffer_frames,
                AUDCLNT_BUFFERFLAGS_SILENT);
        }
        if (SUCCEEDED(result)) {
            result = client->Start();
        }
        if (FAILED(result)) {
            const auto failure =
                error_from_hresult(
                    StreamAudioErrorCode::
                        initialization_failed,
                    result);
            publish_error(
                error_mutex_, error_, failure);
            ready.set_value(failure);
            CoUninitialize();
            return;
        }

        {
            std::scoped_lock lock(error_mutex_);
            error_.reset();
        }
        running_.store(
            true, std::memory_order_release);
        ready.set_value(std::nullopt);

        DWORD task_index = 0;
        const auto mmcss =
            AvSetMmThreadCharacteristicsW(
                L"Pro Audio", &task_index);
        const HANDLE waits[] = {
            stop_event_.get(),
            sample_event.get(),
        };

        bool failed = false;
        while (!failed) {
            const auto signaled =
                WaitForMultipleObjects(
                    2, waits, FALSE, 2000);
            if (signaled == WAIT_OBJECT_0) {
                break;
            }
            if (signaled == WAIT_FAILED) {
                result = HRESULT_FROM_WIN32(
                    GetLastError());
                failed = true;
                break;
            }
            if (signaled != WAIT_OBJECT_0 + 1) {
                continue;
            }

            UINT32 padding = 0;
            result = client->GetCurrentPadding(
                &padding);
            if (FAILED(result)) {
                failed = true;
                break;
            }
            if (padding >= buffer_frames) {
                continue;
            }

            const auto available =
                buffer_frames - padding;
            BYTE* data = nullptr;
            result = render->GetBuffer(
                available, &data);
            if (FAILED(result)) {
                failed = true;
                break;
            }

            callbacks_.fetch_add(
                1, std::memory_order_relaxed);
            auto samples =
                std::span<float>(
                    reinterpret_cast<float*>(data),
                    static_cast<std::size_t>(
                        available) *
                        kStreamAudioChannels);
            source_(samples);
            frames_.fetch_add(
                available,
                std::memory_order_relaxed);

            result = render->ReleaseBuffer(
                available, 0);
            if (FAILED(result)) {
                failed = true;
                break;
            }
        }

        client->Stop();
        if (mmcss != nullptr) {
            AvRevertMmThreadCharacteristics(mmcss);
        }

        if (failed) {
            glitches_.fetch_add(
                1, std::memory_order_relaxed);
            publish_error(
                error_mutex_,
                error_,
                error_from_hresult(
                    StreamAudioErrorCode::os_failure,
                    result));
        }
        running_.store(
            false, std::memory_order_release);
        CoUninitialize();
    }

    Source source_;
    Handle stop_event_{nullptr, &CloseHandle};
    std::thread worker_;

    std::atomic_bool running_{false};
    std::atomic<std::uint64_t> callbacks_{0};
    std::atomic<std::uint64_t> frames_{0};
    std::atomic<std::uint64_t> glitches_{0};
    mutable std::mutex error_mutex_;
    std::optional<StreamAudioError> error_;
};

WasapiStreamAudioRenderer::
    WasapiStreamAudioRenderer()
    : impl_(std::make_unique<Impl>()) {}

WasapiStreamAudioRenderer::
    ~WasapiStreamAudioRenderer() {
    impl_->stop();
}

std::optional<StreamAudioError>
WasapiStreamAudioRenderer::start(
    Source source) {
    return impl_->start(std::move(source));
}

void WasapiStreamAudioRenderer::stop() noexcept {
    impl_->stop();
}

StreamAudioStatistics
WasapiStreamAudioRenderer::statistics()
    const noexcept {
    return snapshot(
        impl_->running_,
        impl_->callbacks_,
        impl_->frames_,
        impl_->glitches_,
        impl_->error_mutex_,
        impl_->error_);
}

} // namespace catro::platform::windows

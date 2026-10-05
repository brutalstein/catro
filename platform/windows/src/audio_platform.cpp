#include "text.hpp"

#include <catro/audio/realtime.hpp>
#include <catro/platform/windows/audio_platform.hpp>

#include <Windows.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <avrt.h>
#include <ks.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmreg.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace catro::platform::windows {
namespace {

namespace audio = catro::audio;
namespace caps = catro::capabilities;
using Microsoft::WRL::ComPtr;

constexpr std::string_view kEndpointPrefix = "mmdevice:";
constexpr REFERENCE_TIME kUnitsPerSecond = 10'000'000;
// A missed event is not an error (a paused device raises none); the wait only bounds how long a
// stop request can go unnoticed if the stop event were ever missed.
constexpr DWORD kWaitMilliseconds = 2000;

using Handle = std::unique_ptr<std::remove_pointer_t<HANDLE>, decltype(&CloseHandle)>;

Handle make_event(bool manual_reset = false) {
    return {CreateEventW(nullptr, manual_reset ? TRUE : FALSE, FALSE, nullptr), &CloseHandle};
}

std::uint32_t engine_frames(REFERENCE_TIME duration) {
    return static_cast<std::uint32_t>(duration * audio::kSampleRate / kUnitsPerSecond);
}

audio::AudioError error_for(HRESULT result) {
    auto code = audio::AudioErrorCode::os_failure;
    if (result == AUDCLNT_E_DEVICE_INVALIDATED || result == AUDCLNT_E_RESOURCES_INVALIDATED) {
        code = audio::AudioErrorCode::device_lost;
    } else if (result == AUDCLNT_E_DEVICE_IN_USE || result == AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED) {
        code = audio::AudioErrorCode::device_in_use;
    } else if (result == AUDCLNT_E_UNSUPPORTED_FORMAT) {
        code = audio::AudioErrorCode::format_unsupported;
    } else if (result == E_ACCESSDENIED) {
        // The Windows microphone privacy setting blocks the activation.
        code = audio::AudioErrorCode::permission_denied;
    } else if (result == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
        code = audio::AudioErrorCode::device_not_found;
    }
    return {code, static_cast<std::int64_t>(result)};
}

// 48 kHz float mono; AUTOCONVERTPCM makes the audio engine convert to and from the mix format.
WAVEFORMATEXTENSIBLE engine_format() {
    WAVEFORMATEXTENSIBLE format{};
    format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format.Format.nChannels = 1;
    format.Format.nSamplesPerSec = audio::kSampleRate;
    format.Format.wBitsPerSample = 32;
    format.Format.nBlockAlign = sizeof(float);
    format.Format.nAvgBytesPerSec = audio::kSampleRate * sizeof(float);
    format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    format.Samples.wValidBitsPerSample = 32;
    format.dwChannelMask = SPEAKER_FRONT_CENTER;
    format.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    return format;
}

enum class Direction {
    capture,
    render,
};

class WasapiStream final : public audio::AudioStream {
public:
    WasapiStream(Direction direction, std::optional<caps::AudioEndpointId> device, audio::CaptureSink* sink,
                 audio::RenderSource* source, audio::StreamFailure failure)
        : direction_(direction), device_(std::move(device)), sink_(sink), source_(source),
          failure_(std::move(failure)) {}

    ~WasapiStream() override {
        if (thread_.joinable()) {
            SetEvent(stop_event_.get());
            thread_.join();
        }
    }

    WasapiStream(const WasapiStream&) = delete;
    WasapiStream& operator=(const WasapiStream&) = delete;

    // Starts the stream thread and waits until it has opened the device.
    std::optional<audio::AudioError> open() {
        if (!stop_event_ || !start_event_) {
            return audio::AudioError{audio::AudioErrorCode::os_failure, static_cast<std::int64_t>(GetLastError())};
        }
        std::promise<std::optional<audio::AudioError>> opened;
        auto result = opened.get_future();
        thread_ = std::thread([this, opened = std::move(opened)]() mutable { run(opened); });
        return result.get();
    }

    const audio::StreamInfo& info() const noexcept override { return info_; }

    std::optional<audio::AudioError> start() override {
        auto result = started_.get_future();
        SetEvent(start_event_.get());
        return result.get();
    }

    void request_stop() noexcept override {
        if (stop_event_) {
            SetEvent(stop_event_.get());
        }
    }

    std::uint64_t glitches() const noexcept override { return glitches_.load(std::memory_order_relaxed); }

private:
    void run(std::promise<std::optional<audio::AudioError>>& opened) {
        const auto apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(apartment)) {
            opened.set_value(error_for(apartment));
            return;
        }
        std::optional<audio::AudioError> error;
        bool reported_open = false;
        bool ready = false;
        try {
            error = initialize();
            ready = !error;
            opened.set_value(error);
            reported_open = true;
            if (ready) {
                error = serve();
            }
        } catch (...) {
            error = error_for(E_FAIL);
            if (!reported_open) {
                opened.set_value(error);
            }
        }
        if (ready && !start_reported_) {
            report_start(error.value_or(audio::AudioError{audio::AudioErrorCode::device_lost}));
        }
        client_.Reset();
        capture_.Reset();
        render_.Reset();
        CoUninitialize();
        if (ready && error && failure_) {
            failure_(*error);
        }
    }

    std::optional<audio::AudioError> initialize() {
        ComPtr<IMMDeviceEnumerator> devices;
        auto result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&devices));
        if (FAILED(result)) {
            return error_for(result);
        }
        const auto flow = direction_ == Direction::capture ? eCapture : eRender;
        ComPtr<IMMDevice> device;
        if (device_) {
            const std::string_view value = device_->value;
            const auto id = value.starts_with(kEndpointPrefix) ? wide(value.substr(kEndpointPrefix.size())) : L"";
            if (id.empty() || FAILED(devices->GetDevice(id.c_str(), &device))) {
                return audio::AudioError{audio::AudioErrorCode::device_not_found};
            }
            ComPtr<IMMEndpoint> endpoint;
            EDataFlow actual = eAll;
            DWORD state = 0;
            if (FAILED(device.As(&endpoint)) || FAILED(endpoint->GetDataFlow(&actual)) || actual != flow ||
                FAILED(device->GetState(&state)) || state != DEVICE_STATE_ACTIVE) {
                return audio::AudioError{audio::AudioErrorCode::device_not_found};
            }
        } else {
            result = devices->GetDefaultAudioEndpoint(flow, eCommunications, &device);
            if (FAILED(result)) {
                return error_for(result);
            }
        }

        LPWSTR raw_id = nullptr;
        if (SUCCEEDED(device->GetId(&raw_id))) {
            info_.device = {std::string(kEndpointPrefix) + utf8(raw_id), caps::IdentityScope::persistent};
            CoTaskMemFree(raw_id);
        }
        result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client_);
        if (FAILED(result)) {
            return error_for(result);
        }
        WAVEFORMATEX* mix = nullptr;
        if (SUCCEEDED(client_->GetMixFormat(&mix))) {
            info_.device_sample_rate = mix->nSamplesPerSec;
            info_.device_channels = mix->nChannels;
            CoTaskMemFree(mix);
        }
        // ponytail: default shared-mode period (10 ms typical); IAudioClient3 minimum periods can
        // cut monitor latency but do not combine with AUTOCONVERTPCM.
        REFERENCE_TIME period = 0;
        result = client_->GetDevicePeriod(&period, nullptr);
        if (FAILED(result)) {
            return error_for(result);
        }
        auto format = engine_format();
        result = client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                     AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                         AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                     period, 0, reinterpret_cast<WAVEFORMATEX*>(&format), nullptr);
        if (FAILED(result)) {
            return error_for(result);
        }
        // Keep this Catro session audible during Windows communications ducking.
        ComPtr<IAudioSessionControl2> session;
        if (SUCCEEDED(client_->GetService(IID_PPV_ARGS(&session)))) {
            (void)session->SetDuckingPreference(TRUE);
        }
        buffer_event_ = make_event();
        REFERENCE_TIME latency = 0;
        if (!buffer_event_ || FAILED(result = client_->SetEventHandle(buffer_event_.get())) ||
            FAILED(result = client_->GetBufferSize(&buffer_frames_)) ||
            FAILED(result = client_->GetStreamLatency(&latency))) {
            return error_for(buffer_event_ ? result : HRESULT_FROM_WIN32(GetLastError()));
        }
        info_.period_frames = engine_frames(period);
        info_.device_latency_frames = engine_frames(latency);

        if (direction_ == Direction::capture) {
            result = client_->GetService(IID_PPV_ARGS(&capture_));
            // Silent packets are delivered as zeros from here; allocated before the stream runs.
            silence_.assign(buffer_frames_, 0.0F);
        } else {
            result = client_->GetService(IID_PPV_ARGS(&render_));
        }
        return FAILED(result) ? std::optional{error_for(result)} : std::nullopt;
    }

    // Waits for start or stop, then runs the real-time loop until stopped or failed.
    std::optional<audio::AudioError> serve() {
        const HANDLE before[] = {stop_event_.get(), start_event_.get()};
        if (WaitForMultipleObjects(2, before, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) {
            report_start(audio::AudioError{audio::AudioErrorCode::device_lost});
            return std::nullopt;
        }
        auto result = S_OK;
        if (render_) {
            // Start from a full buffer of silence so the first period cannot underrun.
            BYTE* data = nullptr;
            result = render_->GetBuffer(buffer_frames_, &data);
            if (SUCCEEDED(result)) {
                result = render_->ReleaseBuffer(buffer_frames_, AUDCLNT_BUFFERFLAGS_SILENT);
            }
        }
        if (SUCCEEDED(result)) {
            result = client_->Start();
        }
        if (FAILED(result)) {
            report_start(error_for(result));
            return std::nullopt;
        }
        report_start(std::nullopt);

        DWORD task = 0;
        const auto mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task);
        std::optional<audio::AudioError> failure;
        const HANDLE running[] = {stop_event_.get(), buffer_event_.get()};
        while (true) {
            const auto signalled = WaitForMultipleObjects(2, running, FALSE, kWaitMilliseconds);
            if (signalled == WAIT_OBJECT_0) {
                break;
            }
            if (signalled == WAIT_FAILED) {
                failure = error_for(HRESULT_FROM_WIN32(GetLastError()));
                break;
            }
            if (signalled == WAIT_TIMEOUT) {
                UINT32 padding = 0;
                result = client_->GetCurrentPadding(&padding);
                if (FAILED(result)) {
                    failure = error_for(result);
                    break;
                }
            } else if (signalled == WAIT_OBJECT_0 + 1) {
                result = capture_ ? drain_capture() : fill_render();
                if (FAILED(result)) {
                    failure = error_for(result);
                    break;
                }
            }
        }
        client_->Stop();
        if (mmcss != nullptr) {
            AvRevertMmThreadCharacteristics(mmcss);
        }
        return failure;
    }

    HRESULT drain_capture() noexcept {
        UINT32 packet = 0;
        auto result = S_OK;
        while (SUCCEEDED(result = capture_->GetNextPacketSize(&packet)) && packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            result = capture_->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            if (FAILED(result)) {
                return result;
            }
            if ((flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0) {
                glitches_.fetch_add(1, std::memory_order_relaxed);
            }
            if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0) {
                sink_->on_captured(std::span<const float>(silence_.data(), std::min<std::size_t>(frames, silence_.size())));
            } else {
                sink_->on_captured(std::span<const float>(reinterpret_cast<const float*>(data), frames));
            }
            result = capture_->ReleaseBuffer(frames);
            if (FAILED(result)) {
                return result;
            }
        }
        return result;
    }

    HRESULT fill_render() noexcept {
        UINT32 padding = 0;
        auto result = client_->GetCurrentPadding(&padding);
        if (FAILED(result) || padding >= buffer_frames_) {
            return result;
        }
        const auto available = buffer_frames_ - padding;
        BYTE* data = nullptr;
        result = render_->GetBuffer(available, &data);
        if (FAILED(result)) {
            return result;
        }
        source_->on_render(std::span<float>(reinterpret_cast<float*>(data), available));
        return render_->ReleaseBuffer(available, 0);
    }

    void report_start(std::optional<audio::AudioError> error) {
        if (!start_reported_) {
            started_.set_value(error);
            start_reported_ = true;
        }
    }

    Direction direction_;
    std::optional<caps::AudioEndpointId> device_;
    audio::CaptureSink* sink_;
    audio::RenderSource* source_;
    audio::StreamFailure failure_;
    audio::StreamInfo info_;
    Handle stop_event_ = make_event(true);
    Handle start_event_ = make_event();
    Handle buffer_event_{nullptr, &CloseHandle};
    std::promise<std::optional<audio::AudioError>> started_;
    bool start_reported_ = false;
    std::atomic<std::uint64_t> glitches_{0};
    // Stream thread only.
    ComPtr<IAudioClient> client_;
    ComPtr<IAudioCaptureClient> capture_;
    ComPtr<IAudioRenderClient> render_;
    UINT32 buffer_frames_ = 0;
    std::vector<float> silence_;
    std::thread thread_;
};

audio::OpenResult open(Direction direction, const std::optional<caps::AudioEndpointId>& device,
                       audio::CaptureSink* sink, audio::RenderSource* source, audio::StreamFailure failure) {
    auto stream = std::make_unique<WasapiStream>(direction, device, sink, source, std::move(failure));
    if (auto error = stream->open()) {
        return *error;
    }
    return std::unique_ptr<audio::AudioStream>(std::move(stream));
}

} // namespace

audio::OpenResult WasapiAudioPlatform::open_capture(const std::optional<caps::AudioEndpointId>& device,
                                                    audio::CaptureSink& sink, audio::StreamFailure failure) {
    return open(Direction::capture, device, &sink, nullptr, std::move(failure));
}

audio::OpenResult WasapiAudioPlatform::open_render(const std::optional<caps::AudioEndpointId>& device,
                                                   audio::RenderSource& source, audio::StreamFailure failure) {
    return open(Direction::render, device, nullptr, &source, std::move(failure));
}

std::vector<AudioDeviceName> list_audio_devices(audio::DeviceDirection direction) {
    // The caller may already be in an apartment; only undo an initialization made here.
    const auto apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::vector<AudioDeviceName> result;
    {
        ComPtr<IMMDeviceEnumerator> devices;
        ComPtr<IMMDeviceCollection> collection;
        UINT count = 0;
        const auto flow = direction == audio::DeviceDirection::capture ? eCapture : eRender;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&devices))) &&
            SUCCEEDED(devices->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection)) &&
            SUCCEEDED(collection->GetCount(&count))) {
            for (UINT index = 0; index < count; ++index) {
                ComPtr<IMMDevice> device;
                ComPtr<IPropertyStore> store;
                LPWSTR raw_id = nullptr;
                if (FAILED(collection->Item(index, &device)) || FAILED(device->GetId(&raw_id))) {
                    continue;
                }
                AudioDeviceName entry{std::string(kEndpointPrefix) + utf8(raw_id), {}};
                CoTaskMemFree(raw_id);
                PROPVARIANT value;
                PropVariantInit(&value);
                if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)) &&
                    SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR &&
                    value.pwszVal != nullptr) {
                    entry.name = utf8(value.pwszVal);
                }
                PropVariantClear(&value);
                if (entry.name.empty()) {
                    entry.name = "Audio device";
                }
                result.push_back(std::move(entry));
            }
        }
    }
    if (SUCCEEDED(apartment)) {
        CoUninitialize();
    }
    return result;
}

std::optional<caps::AudioEndpointId> WasapiAudioPlatform::default_device(audio::DeviceDirection direction) {
    // The caller may already be in an apartment; only undo an initialization made here.
    const auto apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::optional<caps::AudioEndpointId> result;
    {
        ComPtr<IMMDeviceEnumerator> devices;
        ComPtr<IMMDevice> device;
        LPWSTR raw_id = nullptr;
        const auto flow = direction == audio::DeviceDirection::capture ? eCapture : eRender;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&devices))) &&
            SUCCEEDED(devices->GetDefaultAudioEndpoint(flow, eCommunications, &device)) &&
            SUCCEEDED(device->GetId(&raw_id))) {
            result = caps::AudioEndpointId{std::string(kEndpointPrefix) + utf8(raw_id), caps::IdentityScope::persistent};
            CoTaskMemFree(raw_id);
        }
    }
    if (SUCCEEDED(apartment)) {
        CoUninitialize();
    }
    return result;
}

bool WasapiAudioPlatform::device_available(const caps::AudioEndpointId& endpoint_id,
                                          audio::DeviceDirection direction) {
    const auto apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool available = false;
    {
        ComPtr<IMMDeviceEnumerator> devices;
        ComPtr<IMMDevice> device;
        ComPtr<IMMEndpoint> endpoint;
        const std::string_view value = endpoint_id.value;
        const auto id = value.starts_with(kEndpointPrefix) ? wide(value.substr(kEndpointPrefix.size())) : L"";
        DWORD state = 0;
        EDataFlow flow = eAll;
        available = !id.empty() &&
            SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&devices))) &&
            SUCCEEDED(devices->GetDevice(id.c_str(), &device)) &&
            SUCCEEDED(device->GetState(&state)) && state == DEVICE_STATE_ACTIVE &&
            SUCCEEDED(device.As(&endpoint)) && SUCCEEDED(endpoint->GetDataFlow(&flow)) &&
            flow == (direction == audio::DeviceDirection::capture ? eCapture : eRender);
    }
    if (SUCCEEDED(apartment)) {
        CoUninitialize();
    }
    return available;
}

} // namespace catro::platform::windows

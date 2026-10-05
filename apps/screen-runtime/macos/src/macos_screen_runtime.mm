#include <catro/macos_screen_runtime.hpp>

#include <catro/platform/macos/video_decoder.hpp>
#include <catro/platform/macos/video_encoder.hpp>
#include <catro/platform/macos/video_presenter.hpp>

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <vector>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace catro::screen {
namespace {

using namespace std::chrono_literals;
using platform::macos::EncodedAccessUnit;
using platform::macos::MacH264HardwareDecoder;
using platform::macos::MacH264HardwareEncoder;
using platform::macos::MacScreenCapture;
using platform::macos::MacVideoPresenter;
using platform::macos::NativeVideoFrame;
using platform::macos::ScreenCaptureConfig;
using platform::macos::ScreenCaptureNativeAdapter;
using platform::macos::VideoDecoderConfig;
using platform::macos::VideoDecoderError;
using platform::macos::VideoEncoderConfig;
using platform::macos::VideoEncoderError;

constexpr auto kFirstFrameTimeout = 3s;
constexpr std::uint32_t kPreviewMaxFps = 10;
// A mid-stream encoder failure (a VideoToolbox session invalidated by sleep or a GPU switch)
// restarts the encoder instead of ending the share. Viewers keep the last frame meanwhile; the
// share ends only after this many failed restarts in a row.
constexpr auto kEncoderRetryInterval = 500ms;
constexpr std::uint32_t kEncoderRetryLimit = 20;

// Joins the stream-audio pump on every exit path of the sender worker.
struct AudioPump {
    std::atomic_bool stop{false};
    std::thread worker;

    ~AudioPump() {
        stop.store(true, std::memory_order_release);
        if (worker.joinable()) {
            worker.join();
        }
    }
};

[[nodiscard]] RoomScreenApi production_room_api() noexcept {
    return RoomScreenApi{
        .snapshot = &catro_room_runtime_snapshot,
        .send_video = &catro_room_runtime_send_video,
        .receive_video = &catro_room_runtime_receive_video,
        .send_stream_audio = &catro_room_runtime_send_stream_audio,
        .receive_stream_audio = &catro_room_runtime_receive_stream_audio,
        .request_keyframe = &catro_room_runtime_request_keyframe,
        .keyframe_requests = &catro_room_runtime_keyframe_requests,
    };
}

[[nodiscard]] ScreenTransportConfig transport_from_share(const MacScreenShareConfig& config) {
    ScreenTransportConfig transport;
    transport.room_runtime = config.room_runtime;
    transport.payload_type = config.payload_type;
    transport.mtu_bytes = config.mtu_bytes;
    transport.max_access_unit_bytes = config.max_access_unit_bytes;
    return transport;
}

[[nodiscard]] std::int64_t elapsed_100ns(Clock::time_point started) noexcept {
    using Ticks = std::chrono::duration<std::int64_t, std::ratio<1, 10'000'000>>;
    return std::chrono::duration_cast<Ticks>(Clock::now() - started).count();
}

[[nodiscard]] std::optional<ScreenShareError> attach_surface(MacVideoPresenter& presenter, void* host_layer,
                                                             ScreenShareErrorCode code) {
    if (host_layer == nullptr) {
        presenter.reset();
        return std::nullopt;
    }
    auto error = presenter.replace_surface(host_layer);
    if (!error) {
        error = presenter.set_visible(true);
    }
    if (error) {
        return ScreenShareError{code, platform::macos::name(error->code), error->native_code};
    }
    return std::nullopt;
}

namespace {

constexpr UInt32 kAudioInputElement = 1;
constexpr UInt32 kAudioOutputElement = 0;

AudioObjectPropertyAddress audio_address(
    AudioObjectPropertySelector selector,
    AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal) {
    return {selector, scope, kAudioObjectPropertyElementMain};
}

template <class T>
std::optional<T> audio_scalar(
    AudioObjectID object,
    const AudioObjectPropertyAddress& where) {
    T value{};
    UInt32 size = sizeof(T);
    if (AudioObjectGetPropertyData(
            object, &where, 0, nullptr, &size, &value) != noErr ||
        size != sizeof(T)) {
        return std::nullopt;
    }
    return value;
}

std::uint32_t output_channels(AudioObjectID device) {
    const auto where = audio_address(
        kAudioDevicePropertyStreamConfiguration,
        kAudioObjectPropertyScopeOutput);
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(
            device, &where, 0, nullptr, &size) != noErr ||
        size < sizeof(AudioBufferList)) {
        return 0;
    }
    std::vector<std::byte> bytes(size);
    if (AudioObjectGetPropertyData(
            device, &where, 0, nullptr, &size, bytes.data()) != noErr) {
        return 0;
    }
    const auto* list =
        reinterpret_cast<const AudioBufferList*>(bytes.data());
    std::uint32_t channels = 0;
    for (UInt32 index = 0;
         index < list->mNumberBuffers;
         ++index) {
        channels += list->mBuffers[index].mNumberChannels;
    }
    return channels;
}

std::optional<AudioObjectID> resolve_output_device(
    std::string_view endpoint) {
    AudioObjectID device = kAudioObjectUnknown;
    if (endpoint.empty()) {
        device = audio_scalar<AudioObjectID>(
            kAudioObjectSystemObject,
            audio_address(
                kAudioHardwarePropertyDefaultOutputDevice))
                     .value_or(kAudioObjectUnknown);
    } else {
        constexpr std::string_view prefix = "coreaudio:";
        constexpr std::string_view suffix = ":output";
        if (!endpoint.starts_with(prefix) ||
            !endpoint.ends_with(suffix) ||
            endpoint.size() <=
                prefix.size() + suffix.size()) {
            return std::nullopt;
        }
        const auto uid = endpoint.substr(
            prefix.size(),
            endpoint.size() - prefix.size() - suffix.size());
        CFStringRef text = CFStringCreateWithBytes(
            nullptr,
            reinterpret_cast<const UInt8*>(uid.data()),
            static_cast<CFIndex>(uid.size()),
            kCFStringEncodingUTF8,
            false);
        if (text == nullptr) {
            return std::nullopt;
        }
        const auto where = audio_address(
            kAudioHardwarePropertyTranslateUIDToDevice);
        UInt32 size = sizeof(device);
        const auto status = AudioObjectGetPropertyData(
            kAudioObjectSystemObject,
            &where,
            sizeof(text),
            &text,
            &size,
            &device);
        CFRelease(text);
        if (status != noErr) {
            return std::nullopt;
        }
    }
    if (device == kAudioObjectUnknown ||
        output_channels(device) == 0) {
        return std::nullopt;
    }
    const auto alive = audio_scalar<UInt32>(
        device,
        audio_address(kAudioDevicePropertyDeviceIsAlive));
    if (alive && *alive == 0) {
        return std::nullopt;
    }
    return device;
}

std::optional<AudioObjectID> effective_output_device(
    std::string_view requested) {
    if (!requested.empty()) {
        if (const auto chosen =
                resolve_output_device(requested)) {
            return chosen;
        }
    }
    return resolve_output_device({});
}

// AUHAL keeps watched stream audio on the same selected output as voice. If a Bluetooth/USB device
// disappears, playback falls back to the current system default and returns to the saved endpoint
// when it reappears. These CoreAudio APIs predate Monterey and keep the 12.3 deployment floor.
class CoreAudioStreamOutput final : public StreamAudioOutput {
public:
    using DeviceProvider = std::function<std::string()>;

    explicit CoreAudioStreamOutput(DeviceProvider provider)
        : provider_(std::move(provider)),
          queue_(dispatch_queue_create(
              "dev.catro.stream-audio.device",
              DISPATCH_QUEUE_SERIAL)) {}

    ~CoreAudioStreamOutput() override { stop(); }

    [[nodiscard]] bool start(
        StreamAudioRenderBridge& bridge) override {
        stop();

        const auto requested = provider_();
        const auto resolved =
            effective_output_device(requested);
        if (!resolved) {
            return false;
        }
        device_ = *resolved;
        device_channels_ = output_channels(device_);
        device_rate_ = audio_scalar<Float64>(
            device_,
            audio_address(
                kAudioDevicePropertyNominalSampleRate))
                           .value_or(0.0);
        if (device_channels_ == 0 || device_rate_ <= 0.0) {
            device_ = kAudioObjectUnknown;
            return false;
        }
        channels_ = std::clamp<std::uint32_t>(
            device_channels_, 1, 2);
        bridge_ = &bridge;
        failed_.store(false, std::memory_order_release);

        AudioComponentDescription description{};
        description.componentType = kAudioUnitType_Output;
        description.componentSubType =
            kAudioUnitSubType_HALOutput;
        description.componentManufacturer =
            kAudioUnitManufacturer_Apple;
        const auto component =
            AudioComponentFindNext(nullptr, &description);
        if (component == nullptr ||
            AudioComponentInstanceNew(component, &unit_) != noErr) {
            unit_ = nullptr;
            bridge_ = nullptr;
            return false;
        }

        const UInt32 input_enabled = 0;
        const UInt32 output_enabled = 1;
        if (AudioUnitSetProperty(
                unit_,
                kAudioOutputUnitProperty_EnableIO,
                kAudioUnitScope_Input,
                kAudioInputElement,
                &input_enabled,
                sizeof(input_enabled)) != noErr ||
            AudioUnitSetProperty(
                unit_,
                kAudioOutputUnitProperty_EnableIO,
                kAudioUnitScope_Output,
                kAudioOutputElement,
                &output_enabled,
                sizeof(output_enabled)) != noErr ||
            AudioUnitSetProperty(
                unit_,
                kAudioOutputUnitProperty_CurrentDevice,
                kAudioUnitScope_Global,
                kAudioOutputElement,
                &device_,
                sizeof(device_)) != noErr) {
            stop();
            return false;
        }

        AudioStreamBasicDescription format{};
        format.mSampleRate = 48'000.0;
        format.mFormatID = kAudioFormatLinearPCM;
        format.mFormatFlags =
            kAudioFormatFlagIsFloat |
            kAudioFormatFlagIsPacked;
        format.mChannelsPerFrame = channels_;
        format.mBitsPerChannel = 32;
        format.mBytesPerFrame =
            static_cast<UInt32>(
                sizeof(float) * channels_);
        format.mFramesPerPacket = 1;
        format.mBytesPerPacket =
            format.mBytesPerFrame;

        UInt32 max_frames = 4096;
        UInt32 max_frames_size = sizeof(max_frames);
        (void)AudioUnitGetProperty(
            unit_,
            kAudioUnitProperty_MaximumFramesPerSlice,
            kAudioUnitScope_Global,
            0,
            &max_frames,
            &max_frames_size);
        if (channels_ == 1) {
            stereo_scratch_.assign(
                static_cast<std::size_t>(max_frames) * 2U,
                0.0F);
        }

        const AURenderCallbackStruct callback{
            &CoreAudioStreamOutput::render, this};
        if (AudioUnitSetProperty(
                unit_,
                kAudioUnitProperty_StreamFormat,
                kAudioUnitScope_Input,
                kAudioOutputElement,
                &format,
                sizeof(format)) != noErr ||
            AudioUnitSetProperty(
                unit_,
                kAudioUnitProperty_SetRenderCallback,
                kAudioUnitScope_Input,
                kAudioOutputElement,
                &callback,
                sizeof(callback)) != noErr ||
            AudioUnitInitialize(unit_) != noErr ||
            !observe_device()) {
            stop();
            return false;
        }

        if (AudioOutputUnitStart(unit_) != noErr) {
            stop();
            return false;
        }
        return true;
    }

    [[nodiscard]] bool healthy() override {
        if (unit_ == nullptr ||
            failed_.load(std::memory_order_acquire)) {
            return false;
        }
        const auto desired =
            effective_output_device(provider_());
        if (!desired || *desired != device_) {
            return false;
        }
        const auto current_rate = audio_scalar<Float64>(
            device_,
            audio_address(
                kAudioDevicePropertyNominalSampleRate));
        if (!current_rate ||
            *current_rate != device_rate_ ||
            output_channels(device_) != device_channels_) {
            return false;
        }
        UInt32 running = 0;
        UInt32 size = sizeof(running);
        return AudioUnitGetProperty(
                   unit_,
                   kAudioOutputUnitProperty_IsRunning,
                   kAudioUnitScope_Global,
                   kAudioOutputElement,
                   &running,
                   &size) == noErr &&
               running != 0;
    }

    void stop() noexcept override {
        if (unit_ != nullptr) {
            (void)AudioOutputUnitStop(unit_);
        }
        if (device_listener_ != nil) {
            for (const auto& where : observed_) {
                AudioObjectRemovePropertyListenerBlock(
                    device_,
                    &where,
                    queue_,
                    device_listener_);
            }
            observed_.clear();
            dispatch_sync(queue_, ^{
            });
            device_listener_ = nil;
        }
        if (unit_ != nullptr) {
            AudioUnitUninitialize(unit_);
            AudioComponentInstanceDispose(unit_);
            unit_ = nullptr;
        }
        bridge_ = nullptr;
        device_ = kAudioObjectUnknown;
        channels_ = 0;
        device_channels_ = 0;
        device_rate_ = 0.0;
        stereo_scratch_.clear();
        failed_.store(false, std::memory_order_release);
    }

private:
    [[nodiscard]] bool observe_device() {
        device_listener_ =
            ^(UInt32,
              const AudioObjectPropertyAddress*) {
              failed_.store(
                  true, std::memory_order_release);
            };
        const std::array addresses{
            audio_address(
                kAudioDevicePropertyDeviceIsAlive),
            audio_address(
                kAudioDevicePropertyNominalSampleRate),
            audio_address(
                kAudioDevicePropertyStreamConfiguration,
                kAudioObjectPropertyScopeOutput),
        };
        // Some aggregate/virtual devices decline one of these listeners. Polling in healthy()
        // still detects route, rate and channel changes, so listeners are an acceleration only.
        for (const auto& where : addresses) {
            if (AudioObjectAddPropertyListenerBlock(
                    device_,
                    &where,
                    queue_,
                    device_listener_) == noErr) {
                observed_.push_back(where);
            }
        }
        if (observed_.empty()) {
            device_listener_ = nil;
        }
        return true;
    }

    static OSStatus render(
        void* context,
        AudioUnitRenderActionFlags*,
        const AudioTimeStamp*,
        UInt32,
        UInt32 frames,
        AudioBufferList* data) noexcept {
        auto& output =
            *static_cast<CoreAudioStreamOutput*>(context);
        if (data == nullptr ||
            data->mNumberBuffers == 0 ||
            output.bridge_ == nullptr) {
            return noErr;
        }

        auto& first = data->mBuffers[0];
        if (first.mData == nullptr) {
            return noErr;
        }
        auto* samples =
            static_cast<float*>(first.mData);
        const auto available =
            static_cast<std::size_t>(
                first.mDataByteSize / sizeof(float));

        if (output.channels_ == 1) {
            const auto stereo_count =
                static_cast<std::size_t>(frames) * 2U;
            if (stereo_count >
                output.stereo_scratch_.size()) {
                std::fill(
                    samples,
                    samples + std::min<std::size_t>(
                                  available, frames),
                    0.0F);
                output.failed_.store(
                    true, std::memory_order_release);
                return noErr;
            }
            output.bridge_->on_render(
                std::span<float>(
                    output.stereo_scratch_.data(),
                    stereo_count));
            const auto mono_count =
                std::min<std::size_t>(
                    available, frames);
            for (std::size_t index = 0;
                 index < mono_count;
                 ++index) {
                samples[index] =
                    0.5F *
                    (output.stereo_scratch_[index * 2U] +
                     output.stereo_scratch_[
                         index * 2U + 1U]);
            }
        } else {
            const auto wanted =
                static_cast<std::size_t>(frames) * 2U;
            output.bridge_->on_render(
                std::span<float>(
                    samples,
                    std::min(available, wanted)));
        }

        for (UInt32 index = 1;
             index < data->mNumberBuffers;
             ++index) {
            auto& extra = data->mBuffers[index];
            if (extra.mData != nullptr) {
                std::memset(
                    extra.mData,
                    0,
                    extra.mDataByteSize);
            }
        }
        return noErr;
    }

    DeviceProvider provider_;
    dispatch_queue_t queue_;
    AudioObjectPropertyListenerBlock device_listener_ = nil;
    std::vector<AudioObjectPropertyAddress> observed_;
    std::vector<float> stereo_scratch_;
    AudioComponentInstance unit_ = nullptr;
    StreamAudioRenderBridge* bridge_ = nullptr;
    AudioObjectID device_ = kAudioObjectUnknown;
    std::uint32_t channels_ = 0;
    std::uint32_t device_channels_ = 0;
    Float64 device_rate_ = 0.0;
    std::atomic_bool failed_{false};
};

} // namespace

} // namespace

bool valid_share(const MacScreenShareConfig& config) noexcept {
    return config.source.native_id != 0 && config.room_runtime != nullptr &&
           valid_media_bounds(config.payload_type, config.mtu_bytes, config.max_access_unit_bytes) &&
           config.max_width >= 320 && config.max_width <= 7680 && config.max_height >= 180 &&
           config.max_height <= 4320 && config.fps >= 1 && config.fps <= 120 && config.bitrate >= 128'000 &&
           config.bitrate <= 50'000'000 && config.stream_audio_bitrate >= 32'000 &&
           config.stream_audio_bitrate <= 512'000 && config.ssrc != 0;
}

struct MacScreenShareRuntime::Impl {
    explicit Impl(const RoomScreenApi& api) : api_(api) {}

    // VideoToolbox decode + Core Animation presentation edge of the shared receive loop.
    class RemoteViewer final : public RemoteVideoViewer {
    public:
        RemoteViewer(Impl& owner, std::size_t max_access_unit_bytes) : owner_(owner) {
            config_.max_access_unit_bytes = max_access_unit_bytes;
            // Every supported Mac decodes H.264 in hardware; a software session is still better
            // than a black stream when VideoToolbox declines the hardware path.
            config_.require_hardware = false;
        }

        void release() noexcept override {
            decoder_.stop();
        }

        [[nodiscard]] std::optional<ScreenShareError> start_decoder() override {
            {
                std::scoped_lock lock(failure_mutex_);
                async_failure_.reset();
            }
            if (const auto failure = decoder_.start(
                    config_, [this](const NativeVideoFrame& frame) { on_frame(frame); },
                    [this](const VideoDecoderError& failure) {
                        std::scoped_lock lock(failure_mutex_);
                        async_failure_ = failure;
                    })) {
                return decoder_error(*failure);
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<ScreenShareError> decode_and_present(std::span<const std::byte> annex_b,
                                                                         std::int64_t pts_100ns) override {
            {
                std::scoped_lock lock(failure_mutex_);
                if (async_failure_) {
                    return decoder_error(*std::exchange(async_failure_, std::nullopt));
                }
            }
            if (const auto failure = decoder_.decode(annex_b, pts_100ns)) {
                return decoder_error(*failure);
            }
            return std::nullopt;
        }

    private:
        [[nodiscard]] static ScreenShareError decoder_error(const VideoDecoderError& failure) {
            return ScreenShareError{ScreenShareErrorCode::decoder_failed, platform::macos::name(failure.code),
                                    failure.native_code};
        }

        // VideoToolbox output thread. Presentation is best effort: a detached, hidden, or busy surface
        // drops the frame (counted by the presenter) instead of failing the room.
        void on_frame(const NativeVideoFrame& frame) noexcept {
            auto& counters = owner_.counters_;
            counters.remote_decoded.fetch_add(1, std::memory_order_relaxed);
            counters.remote_width.store(frame.width, std::memory_order_relaxed);
            counters.remote_height.store(frame.height, std::memory_order_relaxed);
            (void)owner_.remote_presenter_.present(frame);
            const auto presentation = owner_.remote_presenter_.statistics();
            counters.remote_presented.store(presentation.frames_presented, std::memory_order_relaxed);
            counters.remote_present_drops.store(presentation.frames_dropped, std::memory_order_relaxed);
            counters.remote_last_frame_ns.store(steady_now_ns(), std::memory_order_release);
        }

        Impl& owner_;
        MacH264HardwareDecoder decoder_;
        VideoDecoderConfig config_;
        std::mutex failure_mutex_;
        std::optional<VideoDecoderError> async_failure_;
    };

    [[nodiscard]] std::optional<ScreenShareError> start_listening(const ScreenTransportConfig& config) {
        std::scoped_lock lifecycle_lock(lifecycle_mutex_);
        if (!valid_transport(config) || config.room_runtime == nullptr) {
            return ScreenShareError{ScreenShareErrorCode::invalid_config,
                                    "macOS screen sharing requires a valid RTC room transport", 0};
        }
        if (receiver_worker_.joinable() && !stop_requested_.load(std::memory_order_acquire) && transport_config_ &&
            *transport_config_ == config) {
            return std::nullopt;
        }
        stop_locked();
        return start_transport_locked(config);
    }

    [[nodiscard]] std::optional<ScreenShareError> start(const MacScreenShareConfig& config) {
        std::scoped_lock lifecycle_lock(lifecycle_mutex_);
        if (!valid_share(config)) {
            return ScreenShareError{ScreenShareErrorCode::invalid_config, "invalid screen-share configuration", 0};
        }
        const auto transport = transport_from_share(config);
        const bool reuse_transport = receiver_worker_.joinable() &&
                                     !stop_requested_.load(std::memory_order_acquire) && transport_config_ &&
                                     *transport_config_ == transport &&
                                     state_.load(std::memory_order_acquire) == ScreenShareState::listening;
        if (!reuse_transport) {
            stop_locked();
            if (auto failure = start_transport_locked(transport)) {
                return failure;
            }
        }
        stop_sharing_locked();
        reset_local_statistics();
        {
            std::scoped_lock lock(metadata_mutex_);
            source_title_ = config.source.title;
            error_.clear();
        }
        stream_audio_enabled_.store(config.share_audio, std::memory_order_release);
        share_stop_requested_.store(false, std::memory_order_release);
        state_.store(ScreenShareState::starting, std::memory_order_release);
        try {
            sender_worker_ = std::thread([this, config] { run_sender_guarded(config); });
        } catch (...) {
            share_stop_requested_.store(true, std::memory_order_release);
            state_.store(ScreenShareState::failed, std::memory_order_release);
            return ScreenShareError{ScreenShareErrorCode::worker_start_failed, "screen-share worker could not start",
                                    0};
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<ScreenShareError> start_transport_locked(const ScreenTransportConfig& config) {
        reset_all_statistics();
        {
            std::scoped_lock lock(metadata_mutex_);
            source_title_.clear();
            error_.clear();
        }
        const auto room = api_.snapshot(config.room_runtime);
        if (room.state == CATRO_ROOM_FAILED || room.state == CATRO_ROOM_IDLE) {
            state_.store(ScreenShareState::failed, std::memory_order_release);
            return ScreenShareError{ScreenShareErrorCode::network_failed,
                                    room.error[0] != '\0' ? room.error : "RTC room transport is not connected", 0};
        }
        transport_config_ = config;
        stop_requested_.store(false, std::memory_order_release);
        share_stop_requested_.store(true, std::memory_order_release);
        state_.store(ScreenShareState::listening, std::memory_order_release);
        try {
            receiver_worker_ = std::thread([this, config] { run_receiver_guarded(config); });
            stream_audio_receiver_worker_ = std::thread([this, config] { run_stream_audio_receiver_guarded(config); });
        } catch (...) {
            stop_requested_.store(true, std::memory_order_release);
            stop_cv_.notify_all();
            if (stream_audio_receiver_worker_.joinable()) {
                stream_audio_receiver_worker_.join();
            }
            if (receiver_worker_.joinable()) {
                receiver_worker_.join();
            }
            transport_config_.reset();
            state_.store(ScreenShareState::failed, std::memory_order_release);
            return ScreenShareError{ScreenShareErrorCode::worker_start_failed, "screen receive worker could not start",
                                    0};
        }
        return std::nullopt;
    }

    void stop_sharing() noexcept {
        std::scoped_lock lifecycle_lock(lifecycle_mutex_);
        stop_sharing_locked();
    }

    void set_local_preview_enabled(bool enabled) noexcept {
        // Sampled by the sender on its frame cadence; never perturbs encode or transport timing.
        local_preview_enabled_.store(enabled, std::memory_order_release);
    }

    void set_echo_sink(std::function<void(std::span<const float>)> sink) {
        counters_.echo_sink = std::move(sink);
    }

    void set_stream_volume(float volume) noexcept {
        counters_.remote_stream_volume.store(std::clamp(volume, 0.0F, 2.0F), std::memory_order_relaxed);
    }

    void set_output_device(std::string endpoint) {
        std::scoped_lock lock(output_device_mutex_);
        output_device_ = std::move(endpoint);
    }

    [[nodiscard]] std::string output_device() const {
        std::scoped_lock lock(output_device_mutex_);
        return output_device_;
    }

    void set_remote_viewing_enabled(bool enabled) noexcept {
        counters_.remote_viewing_enabled.store(enabled, std::memory_order_release);
        if (!enabled) {
            // The receive worker releases the decoder within one <= 20 ms receive iteration.
            counters_.remote_last_frame_ns.store(0, std::memory_order_release);
            counters_.remote_width.store(0, std::memory_order_relaxed);
            counters_.remote_height.store(0, std::memory_order_relaxed);
        }
    }

    void stop_sharing_locked() noexcept {
        share_stop_requested_.store(true, std::memory_order_release);
        stop_cv_.notify_all();
        if (sender_worker_.joinable()) {
            sender_worker_.join();
        }
        stream_audio_enabled_.store(false, std::memory_order_release);
        stream_audio_active_.store(false, std::memory_order_release);
        if (receiver_worker_.joinable() && state_.load(std::memory_order_acquire) != ScreenShareState::failed) {
            state_.store(ScreenShareState::listening, std::memory_order_release);
        }
    }

    void stop() noexcept {
        std::scoped_lock lifecycle_lock(lifecycle_mutex_);
        stop_locked();
    }

    void stop_locked() noexcept {
        share_stop_requested_.store(true, std::memory_order_release);
        stop_requested_.store(true, std::memory_order_release);
        stop_cv_.notify_all();
        for (auto* worker : {&sender_worker_, &stream_audio_receiver_worker_, &receiver_worker_}) {
            if (worker->joinable()) {
                worker->join();
            }
        }
        stream_audio_enabled_.store(false, std::memory_order_release);
        stream_audio_active_.store(false, std::memory_order_release);
        counters_.remote_stream_audio_active.store(false, std::memory_order_release);
        transport_config_.reset();
        state_.store(ScreenShareState::idle, std::memory_order_release);
        stop_requested_.store(false, std::memory_order_release);
    }

    [[nodiscard]] bool should_stop_sender() const noexcept {
        return stop_requested_.load(std::memory_order_acquire) ||
               share_stop_requested_.load(std::memory_order_acquire);
    }

    void run_sender_guarded(const MacScreenShareConfig& config) noexcept {
        try {
            run_sender(config);
        } catch (const std::exception& error) {
            fail_share(error.what() != nullptr ? std::string_view{error.what()}
                                               : std::string_view{"screen sender worker exception"});
        } catch (...) {
            fail_share("screen sender worker exception");
        }
    }

    // Stream audio rides the same ScreenCaptureKit stream as the video; a 20 ms pump moves captured
    // PCM into the shared Opus sender, exactly as the Windows process-loopback path does.
    void pump_stream_audio(StreamAudioCaptureBridge& bridge, StreamAudioSender& sender,
                           const std::atomic_bool& stop) noexcept {
        StreamAudioPcmFrame pcm{};
        while (!stop.load(std::memory_order_acquire) && !should_stop_sender()) {
            if (!bridge.try_pop(pcm)) {
                std::unique_lock stop_lock(stop_mutex_);
                stop_cv_.wait_for(stop_lock, 2ms, [this] { return should_stop_sender(); });
                continue;
            }
            sender.send(pcm);
            // Shared app or system audio also plays on this Mac's speakers.
            if (counters_.echo_sink) {
                counters_.echo_sink(pcm);
            }
        }
    }

    void run_sender(const MacScreenShareConfig& config) {
        // Declared before the capture so both outlive its audio callbacks.
        StreamAudioCaptureBridge audio_bridge;
        std::optional<StreamAudioSender> audio_sender;
        // ScreenCaptureKit captures audio from macOS 13; on Monterey the stream carries no sound.
        bool share_audio = false;
        if (@available(macOS 13.0, *)) {
            share_audio = config.share_audio;
        }
        if (share_audio) {
            audio_sender.emplace(api_, config.room_runtime, counters_);
            if (const auto failure = audio_sender->start(config.stream_audio_bitrate, config.ssrc)) {
                set_stream_audio_error(voice::name(failure->code), failure->native_code);
                audio_sender.reset();
            }
        }
        MacScreenCapture capture;
        ScreenCaptureConfig capture_config;
        capture_config.max_width = config.max_width;
        capture_config.max_height = config.max_height;
        capture_config.frame_rate = config.fps;
        ScreenCaptureNativeAdapter::AudioHandler on_audio;
        if (audio_sender) {
            on_audio = [&audio_bridge](std::span<const float> samples) { audio_bridge.on_captured(samples); };
        }
        auto capture_error = capture.start_source(config.source, capture_config, std::move(on_audio));
        if (capture_error && audio_sender) {
            // Audio is optional: keep the picture flowing and say why the sound is missing.
            set_stream_audio_error("ScreenCaptureKit audio capture failed", capture_error->native_code);
            audio_sender.reset();
            capture_error = capture.start_source(config.source, capture_config);
        }
        if (capture_error) {
            fail_share(platform::macos::name(capture_error->code), capture_error->native_code);
            return;
        }
        NativeVideoFrame first;
        const auto first_deadline = Clock::now() + kFirstFrameTimeout;
        while (!should_stop_sender() && Clock::now() < first_deadline && !capture.wait_for_latest(first, 50ms)) {
            if (const auto stats = capture.statistics(); stats.error) {
                fail_share(platform::macos::name(stats.error->code), stats.error->native_code);
                capture.stop();
                return;
            }
        }
        if (should_stop_sender()) {
            capture.stop();
            return;
        }
        if (!first) {
            fail_share("ScreenCaptureKit did not produce a frame for the selected source");
            capture.stop();
            return;
        }

        VideoSender video_sender(api_, config.room_runtime, nullptr,
                                 video::H264RtpConfig{config.ssrc, config.payload_type, config.mtu_bytes}, counters_);
        const auto started = Clock::now();
        // VideoToolbox delivers access units on its own output thread, including the flush inside
        // stop(); the mutex keeps packetization ordered and stops sending after the first failure.
        std::mutex send_mutex;
        bool send_failed = false;
        const auto send_access_unit = [&](const EncodedAccessUnit& access_unit) {
            std::scoped_lock lock(send_mutex);
            if (send_failed) {
                return;
            }
            frames_encoded_.fetch_add(1, std::memory_order_relaxed);
            const auto sent = video_sender.send(access_unit.bytes, monotonic_rtp_timestamp(started, Clock::now()));
            switch (sent.status) {
            case VideoSendStatus::sent:
            case VideoSendStatus::dropped:
                return;
            case VideoSendStatus::room_failed:
            case VideoSendStatus::network_failed:
                send_failed = true;
                fail_session(room_error_text(api_, config.room_runtime, "RTC room video transport failed"));
                return;
            case VideoSendStatus::not_packetizable:
                send_failed = true;
                fail_share("H.264 access unit is not RFC 6184 packetizable");
                return;
            }
        };
        // VideoToolbox reports output failures on its own thread; the sender loop picks them up.
        std::atomic<const char*> async_failure{nullptr};
        std::atomic<std::int64_t> async_failure_code{0};
        const auto encoder_failed = [&async_failure, &async_failure_code](
                                        const VideoEncoderError& error) {
            async_failure_code.store(error.native_code, std::memory_order_relaxed);
            async_failure.store(platform::macos::name(error.code), std::memory_order_release);
        };
        MacH264HardwareEncoder encoder;
        std::uint32_t current_width = 0;
        std::uint32_t current_height = 0;
        bool encoder_broken = false;
        std::uint32_t encoder_retries = 0;
        auto next_encoder_retry = Clock::time_point{};
        std::uint32_t preview_accumulator = 0;
        const auto preview_fps = std::min(config.fps, kPreviewMaxFps);

        const auto configure_encoder =
            [&](const NativeVideoFrame& frame) -> std::optional<VideoEncoderError> {
            encoder.stop();
            async_failure.store(nullptr, std::memory_order_relaxed);
            VideoEncoderConfig encoder_config;
            encoder_config.width = frame.width;
            encoder_config.height = frame.height;
            encoder_config.frame_rate = config.fps;
            encoder_config.bitrate = video_sender.bitrate(config.bitrate);
            encoder_config.gop_frames = config.fps * 2U;
            encoder_config.max_access_unit_bytes = config.max_access_unit_bytes;
            encoder_config.require_hardware = true;
            if (const auto error = encoder.start(encoder_config, send_access_unit, encoder_failed)) {
                return error;
            }
            current_width = frame.width;
            current_height = frame.height;
            source_width_.store(config.source.width != 0 ? config.source.width : current_width,
                                std::memory_order_relaxed);
            source_height_.store(config.source.height != 0 ? config.source.height : current_height,
                                 std::memory_order_relaxed);
            encoded_width_.store(current_width, std::memory_order_relaxed);
            encoded_height_.store(current_height, std::memory_order_relaxed);
            return std::nullopt;
        };
        // Returns false only when the share has to end.
        const auto restart_encoder_later = [&](const char* message, std::int64_t native_code) {
            encoder.stop();
            if (++encoder_retries > kEncoderRetryLimit) {
                fail_share(message, native_code);
                return false;
            }
            encoder_broken = true;
            next_encoder_retry = Clock::now() + kEncoderRetryInterval;
            return true;
        };
        const auto update_preview = [&](const NativeVideoFrame& frame) {
            if (!local_preview_enabled_.load(std::memory_order_acquire)) {
                preview_accumulator = 0;
                return;
            }
            preview_accumulator += preview_fps;
            if (preview_accumulator < config.fps) {
                return;
            }
            preview_accumulator -= config.fps;
            // Preview is presentation-only: an unattached or busy surface drops the frame and can
            // never stop the share.
            (void)preview_presenter_.present(frame);
            const auto stats = preview_presenter_.statistics();
            preview_frames_.store(stats.frames_presented, std::memory_order_relaxed);
            preview_drops_.store(stats.frames_dropped, std::memory_order_relaxed);
        };
        const auto process_frame = [&](NativeVideoFrame frame) -> bool {
            if (!frame) {
                return true;
            }
            if (const auto* message = async_failure.exchange(nullptr, std::memory_order_acquire)) {
                return restart_encoder_later(message, async_failure_code.load(std::memory_order_relaxed));
            }
            if (encoder_broken && Clock::now() < next_encoder_retry) {
                return true;
            }
            if (encoder_broken || frame.width != current_width || frame.height != current_height) {
                if (const auto error = configure_encoder(frame)) {
                    return restart_encoder_later(platform::macos::name(error->code), error->native_code);
                }
                encoder_broken = false;
            }
            update_preview(frame);
            frame.pts_100ns = elapsed_100ns(started);
            if (const auto bitrate = video_sender.adapt_bitrate(config.bitrate, steady_now_ns())) {
                encoder.set_bitrate(bitrate);
            }
            if (const auto error = encoder.encode(frame, video_sender.keyframe_requested())) {
                return restart_encoder_later(platform::macos::name(error->code), error->native_code);
            }
            encoder_retries = 0;
            return !should_stop_sender();
        };

        // The first start fails at once: a share that never started has nothing to keep live.
        if (const auto error = configure_encoder(first)) {
            fail_share(platform::macos::name(error->code), error->native_code);
            capture.stop();
            return;
        }
        AudioPump audio_pump;
        if (audio_sender) {
            audio_pump.worker =
                std::thread([&] { pump_stream_audio(audio_bridge, *audio_sender, audio_pump.stop); });
            stream_audio_active_.store(true, std::memory_order_release);
        }
        state_.store(ScreenShareState::sharing, std::memory_order_release);
        bool running = process_frame(first);
        first = {};
        const auto period = frame_period(config.fps);
        auto next_frame = Clock::now() + period;
        while (running && !should_stop_sender()) {
            {
                std::unique_lock stop_lock(stop_mutex_);
                stop_cv_.wait_until(stop_lock, next_frame, [this] { return should_stop_sender(); });
            }
            if (should_stop_sender()) {
                break;
            }
            const auto now = Clock::now();
            do {
                next_frame += period;
            } while (next_frame <= now);
            NativeVideoFrame frame;
            if (capture.wait_for_latest(frame, 5ms)) {
                running = process_frame(std::move(frame));
            }
            if (const auto stats = capture.statistics(); running && stats.error) {
                fail_share(platform::macos::name(stats.error->code), stats.error->native_code);
                running = false;
            }
        }
        capture.stop();
        encoder.stop(); // flushes pending access units through send_access_unit
        const auto encoder_stats = encoder.statistics();
        frames_encoded_.store(encoder_stats.frames_encoded, std::memory_order_relaxed);
        encoder_output_failures_.store(encoder_stats.output_failures, std::memory_order_relaxed);
        capture_contention_drops_.store(capture.statistics().contention_drops, std::memory_order_relaxed);
        counters_.stream_audio_capture_drops.store(audio_bridge.dropped_callbacks(), std::memory_order_relaxed);
        if (state_.load(std::memory_order_acquire) != ScreenShareState::failed &&
            !stop_requested_.load(std::memory_order_acquire)) {
            state_.store(ScreenShareState::listening, std::memory_order_release);
        }
    }

    void run_receiver_guarded(const ScreenTransportConfig& config) noexcept {
        try {
            RemoteViewer viewer(*this, config.max_access_unit_bytes);
            const VideoReceiveContext context{
                .api = api_,
                .config = config,
                .socket = nullptr,
                .counters = &counters_,
                .stop_requested = &stop_requested_,
                .trace = nullptr,
            };
            if (const auto failure = run_video_receive_loop(context, viewer)) {
                fail_session(failure->message, failure->native_code);
            }
        } catch (...) {
            fail_session("screen receiver worker exception");
        }
    }

    void run_stream_audio_receiver_guarded(const ScreenTransportConfig& config) noexcept {
        try {
            CoreAudioStreamOutput output(
                [this] { return output_device(); });
            run_stream_audio_receive_loop(api_, config.room_runtime, counters_, stop_requested_, output);
        } catch (...) {
            counters_.remote_stream_audio_decode_failures.fetch_add(1, std::memory_order_relaxed);
        }
        counters_.remote_stream_audio_active.store(false, std::memory_order_release);
    }

    void fail_share(std::string_view message, std::int64_t native_code = 0) noexcept {
        set_error(message, native_code);
        share_stop_requested_.store(true, std::memory_order_release);
        stop_cv_.notify_all();
        state_.store(ScreenShareState::failed, std::memory_order_release);
    }

    void fail_session(std::string_view message, std::int64_t native_code = 0) noexcept {
        set_error(message, native_code);
        share_stop_requested_.store(true, std::memory_order_release);
        stop_requested_.store(true, std::memory_order_release);
        stop_cv_.notify_all();
        state_.store(ScreenShareState::failed, std::memory_order_release);
    }

    void set_error(std::string_view message, std::int64_t native_code) noexcept {
        store_error(error_, message, native_code);
    }

    void set_stream_audio_error(std::string_view message, std::int64_t native_code = 0) noexcept {
        store_error(stream_audio_error_, message, native_code);
    }

    void store_error(std::string& target, std::string_view message, std::int64_t native_code) noexcept {
        try {
            std::string owned{message};
            if (native_code != 0) {
                owned += " (native ";
                owned += std::to_string(native_code);
                owned += ")";
            }
            std::scoped_lock lock(metadata_mutex_);
            target = std::move(owned);
        } catch (...) {
        }
    }

    void reset_local_statistics() noexcept {
        for (auto* value : {&source_width_, &source_height_, &encoded_width_, &encoded_height_}) {
            value->store(0, std::memory_order_relaxed);
        }
        for (auto* value :
             {&frames_encoded_, &preview_frames_, &preview_drops_, &encoder_output_failures_, &capture_contention_drops_}) {
            value->store(0, std::memory_order_relaxed);
        }
        stream_audio_active_.store(false, std::memory_order_relaxed);
        counters_.reset_local();
        std::scoped_lock lock(metadata_mutex_);
        stream_audio_error_.clear();
    }

    void reset_all_statistics() noexcept {
        reset_local_statistics();
        counters_.reset_remote();
        counters_.peer_unreachable_events.store(0, std::memory_order_relaxed);
    }

    [[nodiscard]] ScreenShareSnapshot snapshot() const {
        ScreenShareSnapshot result;
        result.state = state_.load(std::memory_order_acquire);
        {
            std::scoped_lock lock(metadata_mutex_);
            result.source_title = source_title_;
            result.error = error_;
            result.stream_audio_error = stream_audio_error_;
        }
        counters_.fill(result);
        constexpr auto relaxed = std::memory_order_relaxed;
        result.source_width = source_width_.load(relaxed);
        result.source_height = source_height_.load(relaxed);
        result.encoded_width = encoded_width_.load(relaxed);
        result.encoded_height = encoded_height_.load(relaxed);
        result.frames_encoded = frames_encoded_.load(relaxed);
        result.preview_frames = preview_frames_.load(relaxed);
        result.preview_drops = preview_drops_.load(relaxed);
        result.encoder_output_failures = encoder_output_failures_.load(relaxed);
        result.capture_contention_drops = capture_contention_drops_.load(relaxed);
        result.stream_audio_enabled = stream_audio_enabled_.load(relaxed);
        result.stream_audio_active = stream_audio_active_.load(relaxed);
        return result;
    }

    RoomScreenApi api_;
    std::mutex lifecycle_mutex_;
    mutable std::mutex metadata_mutex_;
    mutable std::mutex output_device_mutex_;
    std::mutex stop_mutex_;
    std::condition_variable stop_cv_;
    std::string source_title_;
    std::string error_;
    std::string stream_audio_error_;
    std::string output_device_;
    std::optional<ScreenTransportConfig> transport_config_;
    MacVideoPresenter preview_presenter_;
    MacVideoPresenter remote_presenter_;
    std::thread sender_worker_;
    std::thread receiver_worker_;
    std::thread stream_audio_receiver_worker_;
    std::atomic_bool stop_requested_{false};
    std::atomic_bool share_stop_requested_{true};
    std::atomic_bool local_preview_enabled_{true};
    std::atomic<ScreenShareState> state_{ScreenShareState::idle};
    std::atomic<std::uint32_t> source_width_{0};
    std::atomic<std::uint32_t> source_height_{0};
    std::atomic<std::uint32_t> encoded_width_{0};
    std::atomic<std::uint32_t> encoded_height_{0};
    std::atomic<std::uint64_t> frames_encoded_{0};
    std::atomic<std::uint64_t> preview_frames_{0};
    std::atomic<std::uint64_t> preview_drops_{0};
    std::atomic<std::uint64_t> encoder_output_failures_{0};
    std::atomic<std::uint64_t> capture_contention_drops_{0};
    std::atomic_bool stream_audio_enabled_{false};
    std::atomic_bool stream_audio_active_{false};
    ScreenTransportCounters counters_;
};

MacScreenShareRuntime::MacScreenShareRuntime() : MacScreenShareRuntime(production_room_api()) {}

MacScreenShareRuntime::MacScreenShareRuntime(const RoomScreenApi& room_api)
    : impl_(std::make_unique<Impl>(room_api)) {}

MacScreenShareRuntime::~MacScreenShareRuntime() {
    impl_->stop();
}

std::optional<ScreenShareError> MacScreenShareRuntime::start_listening(const ScreenTransportConfig& config) {
    return impl_->start_listening(config);
}

std::optional<ScreenShareError> MacScreenShareRuntime::start(const MacScreenShareConfig& config) {
    return impl_->start(config);
}

void MacScreenShareRuntime::stop_sharing() noexcept {
    impl_->stop_sharing();
}

void MacScreenShareRuntime::set_local_preview_enabled(bool enabled) noexcept {
    impl_->set_local_preview_enabled(enabled);
}

void MacScreenShareRuntime::set_remote_viewing_enabled(bool enabled) noexcept {
    impl_->set_remote_viewing_enabled(enabled);
}

void MacScreenShareRuntime::set_stream_volume(float volume) noexcept {
    impl_->set_stream_volume(volume);
}

void MacScreenShareRuntime::set_output_device(std::string endpoint) {
    impl_->set_output_device(std::move(endpoint));
}

void MacScreenShareRuntime::set_echo_sink(std::function<void(std::span<const float>)> sink) {
    impl_->set_echo_sink(std::move(sink));
}

void MacScreenShareRuntime::stop() noexcept {
    impl_->stop();
}

ScreenShareSnapshot MacScreenShareRuntime::snapshot() const {
    return impl_->snapshot();
}

std::optional<ScreenShareError> MacScreenShareRuntime::attach_preview_surface(void* host_layer) {
    return attach_surface(impl_->preview_presenter_, host_layer, ScreenShareErrorCode::preview_failed);
}

std::optional<ScreenShareError> MacScreenShareRuntime::attach_remote_surface(void* host_layer) {
    return attach_surface(impl_->remote_presenter_, host_layer, ScreenShareErrorCode::remote_present_failed);
}

} // namespace catro::screen

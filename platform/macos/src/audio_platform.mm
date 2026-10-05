#include <catro/platform/macos/audio_platform.hpp>

#include <catro/audio/realtime.hpp>

#import <AVFoundation/AVFoundation.h>
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <dispatch/dispatch.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace catro::platform::macos {
namespace {

namespace caps = catro::capabilities;
using audio::AudioError;
using audio::AudioErrorCode;

enum class Direction {
    capture,
    render,
};

// Returned by the converter input callback once the captured block is used up.
constexpr OSStatus kBlockConsumed = 0x626c6b21; // 'blk!'
constexpr UInt32 kInputElement = 1;
constexpr UInt32 kOutputElement = 0;

AudioObjectPropertyAddress address(AudioObjectPropertySelector selector,
                                   AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal) {
    return {selector, scope, kAudioObjectPropertyElementMain};
}

template <class T>
std::optional<T> scalar(AudioObjectID object, const AudioObjectPropertyAddress& where) {
    T value{};
    UInt32 size = sizeof(T);
    if (AudioObjectGetPropertyData(object, &where, 0, nullptr, &size, &value) != noErr || size != sizeof(T)) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::string> uid_of(AudioObjectID device) {
    const auto value = scalar<CFStringRef>(device, address(kAudioDevicePropertyDeviceUID));
    if (!value || *value == nullptr) {
        return std::nullopt;
    }
    const auto length = CFStringGetMaximumSizeForEncoding(CFStringGetLength(*value), kCFStringEncodingUTF8) + 1;
    std::string buffer(static_cast<std::size_t>(length), '\0');
    const bool converted = CFStringGetCString(*value, buffer.data(), length, kCFStringEncodingUTF8);
    CFRelease(*value);
    if (!converted) {
        return std::nullopt;
    }
    buffer.resize(std::char_traits<char>::length(buffer.c_str()));
    return buffer;
}

AudioObjectPropertyScope scope_of(Direction direction) {
    return direction == Direction::capture ? kAudioObjectPropertyScopeInput : kAudioObjectPropertyScopeOutput;
}

std::string_view suffix_of(Direction direction) {
    return direction == Direction::capture ? ":input" : ":output";
}

bool has_streams(AudioObjectID device, Direction direction) {
    const auto where = address(kAudioDevicePropertyStreams, scope_of(direction));
    UInt32 size = 0;
    return AudioObjectGetPropertyDataSize(device, &where, 0, nullptr, &size) == noErr && size > 0;
}

std::uint32_t channels_of(AudioObjectID device, Direction direction) {
    const auto where = address(kAudioDevicePropertyStreamConfiguration, scope_of(direction));
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(device, &where, 0, nullptr, &size) != noErr || size < sizeof(AudioBufferList)) {
        return 0;
    }
    std::vector<std::byte> data(size);
    if (AudioObjectGetPropertyData(device, &where, 0, nullptr, &size, data.data()) != noErr) {
        return 0;
    }
    const auto* list = reinterpret_cast<const AudioBufferList*>(data.data());
    std::uint32_t total = 0;
    for (UInt32 index = 0; index < list->mNumberBuffers; ++index) {
        total += list->mBuffers[index].mNumberChannels;
    }
    return total;
}

// "coreaudio:<uid>:input|output" for this direction, or the default device.
std::optional<AudioObjectID> resolve(const std::optional<caps::AudioEndpointId>& endpoint, Direction direction) {
    AudioObjectID device = kAudioObjectUnknown;
    if (!endpoint) {
        const auto selector = direction == Direction::capture ? kAudioHardwarePropertyDefaultInputDevice
                                                              : kAudioHardwarePropertyDefaultOutputDevice;
        device = scalar<AudioObjectID>(kAudioObjectSystemObject, address(selector)).value_or(kAudioObjectUnknown);
    } else {
        constexpr std::string_view prefix = "coreaudio:";
        const std::string_view value = endpoint->value;
        const auto suffix = suffix_of(direction);
        if (!value.starts_with(prefix) || !value.ends_with(suffix) || value.size() <= prefix.size() + suffix.size()) {
            return std::nullopt;
        }
        const auto uid = value.substr(prefix.size(), value.size() - prefix.size() - suffix.size());
        CFStringRef text = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(uid.data()),
                                                   static_cast<CFIndex>(uid.size()), kCFStringEncodingUTF8, false);
        if (text == nullptr) {
            return std::nullopt;
        }
        const auto where = address(kAudioHardwarePropertyTranslateUIDToDevice);
        UInt32 size = sizeof(device);
        const auto status =
            AudioObjectGetPropertyData(kAudioObjectSystemObject, &where, sizeof(text), &text, &size, &device);
        CFRelease(text);
        if (status != noErr) {
            return std::nullopt;
        }
    }
    if (device == kAudioObjectUnknown || !has_streams(device, direction)) {
        return std::nullopt;
    }
    return device;
}

AudioError error_for(OSStatus status) {
    const auto native = std::optional<std::int64_t>{status};
    switch (status) {
    case kAudioHardwareBadDeviceError:
    case kAudioHardwareBadObjectError:
        return {AudioErrorCode::device_lost, native};
    case kAudioDevicePermissionsError:
        return {AudioErrorCode::device_in_use, native};
    case kAudioDeviceUnsupportedFormatError:
    case kAudioUnitErr_FormatNotSupported:
        return {AudioErrorCode::format_unsupported, native};
    default:
        return {AudioErrorCode::os_failure, native};
    }
}

AudioStreamBasicDescription float_format(Float64 rate, UInt32 channels) {
    return {
        .mSampleRate = rate,
        .mFormatID = kAudioFormatLinearPCM,
        .mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved,
        .mBytesPerPacket = sizeof(float),
        .mFramesPerPacket = 1,
        .mBytesPerFrame = sizeof(float),
        .mChannelsPerFrame = channels,
        .mBitsPerChannel = 32,
        .mReserved = 0,
    };
}

std::uint32_t engine_frames(double device_frames, Float64 device_rate) {
    return device_rate > 0 ? static_cast<std::uint32_t>(std::lround(device_frames * audio::kSampleRate / device_rate))
                           : 0;
}

class CoreAudioStream final : public audio::AudioStream {
public:
    CoreAudioStream(Direction direction, AudioObjectID device, audio::CaptureSink* sink, audio::RenderSource* source,
                    audio::StreamFailure failure)
        : direction_(direction), device_(device), sink_(sink), source_(source), failure_(std::move(failure)),
          queue_(dispatch_queue_create("dev.catro.audio.device", DISPATCH_QUEUE_SERIAL)) {}

    ~CoreAudioStream() override {
        if (unit_ != nullptr) {
            AudioOutputUnitStop(unit_);
        }
        // Removal does not wait for a listener already running; draining the queue does.
        if (alive_listener_ != nil) {
            const auto where = address(kAudioDevicePropertyDeviceIsAlive);
            AudioObjectRemovePropertyListenerBlock(device_, &where, queue_, alive_listener_);
        }
        if (overload_listener_ != nil) {
            const auto where = address(kAudioDeviceProcessorOverload);
            AudioObjectRemovePropertyListenerBlock(device_, &where, queue_, overload_listener_);
        }
        dispatch_sync(queue_, ^{
                      });
        if (unit_ != nullptr) {
            AudioUnitUninitialize(unit_);
            AudioComponentInstanceDispose(unit_);
        }
        if (converter_ != nullptr) {
            AudioConverterDispose(converter_);
        }
    }

    CoreAudioStream(const CoreAudioStream&) = delete;
    CoreAudioStream& operator=(const CoreAudioStream&) = delete;

    std::optional<AudioError> initialize() {
        const auto scope = scope_of(direction_);
        const auto uid = uid_of(device_);
        if (!uid) {
            return AudioError{AudioErrorCode::device_not_found};
        }
        const auto device_rate = scalar<Float64>(device_, address(kAudioDevicePropertyNominalSampleRate)).value_or(0);
        if (device_rate <= 0) {
            return AudioError{AudioErrorCode::format_unsupported};
        }
        info_.device = caps::AudioEndpointId{"coreaudio:" + *uid + std::string(suffix_of(direction_)),
                                             caps::IdentityScope::persistent};
        info_.device_sample_rate = static_cast<std::uint32_t>(std::lround(device_rate));
        info_.device_channels = channels_of(device_, direction_);
        const auto period = scalar<UInt32>(device_, address(kAudioDevicePropertyBufferFrameSize)).value_or(0);
        const auto latency = scalar<UInt32>(device_, address(kAudioDevicePropertyLatency, scope)).value_or(0);
        const auto safety = scalar<UInt32>(device_, address(kAudioDevicePropertySafetyOffset, scope)).value_or(0);
        info_.period_frames = engine_frames(period, device_rate);
        info_.device_latency_frames = engine_frames(static_cast<double>(latency) + safety, device_rate);

        const AudioComponentDescription description{
            .componentType = kAudioUnitType_Output,
            .componentSubType = kAudioUnitSubType_HALOutput,
            .componentManufacturer = kAudioUnitManufacturer_Apple,
            .componentFlags = 0,
            .componentFlagsMask = 0,
        };
        const auto component = AudioComponentFindNext(nullptr, &description);
        if (component == nullptr) {
            return AudioError{AudioErrorCode::os_failure};
        }
        auto status = AudioComponentInstanceNew(component, &unit_);
        if (status != noErr) {
            unit_ = nullptr;
            return error_for(status);
        }

        const UInt32 input_enabled = direction_ == Direction::capture ? 1 : 0;
        const UInt32 output_enabled = direction_ == Direction::render ? 1 : 0;
        if ((status = AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input,
                                           kInputElement, &input_enabled, sizeof(input_enabled))) != noErr ||
            (status = AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output,
                                           kOutputElement, &output_enabled, sizeof(output_enabled))) != noErr ||
            (status = AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global,
                                           kOutputElement, &device_, sizeof(device_))) != noErr) {
            return error_for(status);
        }

        UInt32 max_frames = 4096;
        UInt32 size = sizeof(max_frames);
        AudioUnitGetProperty(unit_, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &max_frames,
                             &size);

        if (direction_ == Direction::capture) {
            // AUHAL does not convert the sample rate on its input side, so capture arrives at the
            // device rate (first device channel) and an AudioConverter takes it to the engine rate.
            const auto client = float_format(device_rate, 1);
            if ((status = AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output,
                                               kInputElement, &client, sizeof(client))) != noErr) {
                return error_for(status);
            }
            if (std::lround(device_rate) != static_cast<long>(audio::kSampleRate)) {
                const auto engine = float_format(audio::kSampleRate, 1);
                if ((status = AudioConverterNew(&client, &engine, &converter_)) != noErr) {
                    converter_ = nullptr;
                    return error_for(status);
                }
            }
            captured_.assign(max_frames, 0.0F);
            converted_.assign(engine_frames(max_frames, device_rate) + 64, 0.0F);
            const AURenderCallbackStruct callback{&CoreAudioStream::on_input, this};
            status = AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global,
                                          kInputElement, &callback, sizeof(callback));
        } else {
            // AUHAL converts rate and format on its output side. Two identical channels, so a stereo
            // device plays the mono engine signal on both sides instead of the left one only.
            const auto client = float_format(audio::kSampleRate, std::clamp<UInt32>(info_.device_channels, 1, 2));
            if ((status = AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input,
                                               kOutputElement, &client, sizeof(client))) != noErr) {
                return error_for(status);
            }
            const AURenderCallbackStruct callback{&CoreAudioStream::on_output, this};
            status = AudioUnitSetProperty(unit_, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input,
                                          kOutputElement, &callback, sizeof(callback));
        }
        if (status != noErr || (status = AudioUnitInitialize(unit_)) != noErr) {
            return error_for(status);
        }
        return listen();
    }

    const audio::StreamInfo& info() const noexcept override { return info_; }

    std::optional<AudioError> start() override {
        const auto status = AudioOutputUnitStart(unit_);
        return status == noErr ? std::nullopt : std::optional{error_for(status)};
    }

    void request_stop() noexcept override {
        if (unit_ != nullptr) {
            (void)AudioOutputUnitStop(unit_);
        }
    }

    std::uint64_t glitches() const noexcept override { return glitches_.load(std::memory_order_relaxed); }

private:
    // Device removal and processor overloads arrive on this stream's serial queue, never on the
    // real-time thread.
    std::optional<AudioError> listen() {
        alive_listener_ = ^(UInt32, const AudioObjectPropertyAddress*) {
          const auto alive = scalar<UInt32>(device_, address(kAudioDevicePropertyDeviceIsAlive));
          if ((!alive || *alive == 0) && !failed_.exchange(true)) {
              failure_(AudioError{AudioErrorCode::device_lost});
          }
        };
        overload_listener_ = ^(UInt32, const AudioObjectPropertyAddress*) {
          glitches_.fetch_add(1, std::memory_order_relaxed);
        };
        const auto alive = address(kAudioDevicePropertyDeviceIsAlive);
        const auto overload = address(kAudioDeviceProcessorOverload);
        auto status = AudioObjectAddPropertyListenerBlock(device_, &alive, queue_, alive_listener_);
        if (status != noErr) {
            alive_listener_ = nil;
            return error_for(status);
        }
        // Overload reporting is optional evidence; a device without it simply reports none.
        if (AudioObjectAddPropertyListenerBlock(device_, &overload, queue_, overload_listener_) != noErr) {
            overload_listener_ = nil;
        }
        return std::nullopt;
    }

    static OSStatus on_input(void* context, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* time,
                             UInt32 bus, UInt32 frames, AudioBufferList*) noexcept {
        auto& stream = *static_cast<CoreAudioStream*>(context);
        if (frames > stream.captured_.size()) {
            stream.glitches_.fetch_add(1, std::memory_order_relaxed);
            return noErr;
        }
        AudioBufferList list{};
        list.mNumberBuffers = 1;
        list.mBuffers[0] = {1, static_cast<UInt32>(frames * sizeof(float)), stream.captured_.data()};
        const auto status = AudioUnitRender(stream.unit_, flags, time, bus, frames, &list);
        if (status != noErr) {
            stream.glitches_.fetch_add(1, std::memory_order_relaxed);
            return status;
        }
        if (stream.converter_ == nullptr) {
            stream.sink_->on_captured(std::span<const float>(stream.captured_.data(), frames));
            return noErr;
        }
        stream.pending_ = frames;
        UInt32 produced = static_cast<UInt32>(stream.converted_.size());
        AudioBufferList out{};
        out.mNumberBuffers = 1;
        out.mBuffers[0] = {1, static_cast<UInt32>(produced * sizeof(float)), stream.converted_.data()};
        const auto converted =
            AudioConverterFillComplexBuffer(stream.converter_, &CoreAudioStream::supply, &stream, &produced, &out, nullptr);
        if (converted != noErr && converted != kBlockConsumed) {
            stream.glitches_.fetch_add(1, std::memory_order_relaxed);
        }
        if (produced > 0) {
            stream.sink_->on_captured(std::span<const float>(stream.converted_.data(), produced));
        }
        return noErr;
    }

    // Hands the converter the captured block once; the converter keeps any remainder.
    static OSStatus supply(AudioConverterRef, UInt32* packets, AudioBufferList* data,
                           AudioStreamPacketDescription**, void* context) noexcept {
        auto& stream = *static_cast<CoreAudioStream*>(context);
        if (stream.pending_ == 0) {
            *packets = 0;
            return kBlockConsumed;
        }
        data->mNumberBuffers = 1;
        data->mBuffers[0] = {1, static_cast<UInt32>(stream.pending_ * sizeof(float)), stream.captured_.data()};
        *packets = stream.pending_;
        stream.pending_ = 0;
        return noErr;
    }

    static OSStatus on_output(void* context, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32,
                              UInt32 frames, AudioBufferList* data) noexcept {
        auto& stream = *static_cast<CoreAudioStream*>(context);
        auto& first = data->mBuffers[0];
        const auto count = std::min<UInt32>(frames, first.mDataByteSize / sizeof(float));
        auto* samples = static_cast<float*>(first.mData);
        stream.source_->on_render(std::span<float>(samples, count));
        for (UInt32 channel = 1; channel < data->mNumberBuffers; ++channel) {
            std::memcpy(data->mBuffers[channel].mData, samples,
                        std::min<std::size_t>(data->mBuffers[channel].mDataByteSize, count * sizeof(float)));
        }
        return noErr;
    }

    Direction direction_;
    AudioObjectID device_;
    audio::CaptureSink* sink_;
    audio::RenderSource* source_;
    audio::StreamFailure failure_;
    audio::StreamInfo info_;
    dispatch_queue_t queue_;
    AudioObjectPropertyListenerBlock alive_listener_ = nil;
    AudioObjectPropertyListenerBlock overload_listener_ = nil;
    AudioUnit unit_ = nullptr;
    AudioConverterRef converter_ = nullptr;
    // Preallocated before the stream runs; the real-time callbacks only index into them.
    std::vector<float> captured_;
    std::vector<float> converted_;
    UInt32 pending_ = 0;
    std::atomic<std::uint64_t> glitches_{0};
    std::atomic<bool> failed_{false};
};

audio::OpenResult open_stream(Direction direction, const std::optional<caps::AudioEndpointId>& endpoint,
                       audio::CaptureSink* sink, audio::RenderSource* source, audio::StreamFailure failure) {
    const auto device = resolve(endpoint, direction);
    if (!device) {
        return AudioError{AudioErrorCode::device_not_found};
    }
    auto stream = std::make_unique<CoreAudioStream>(direction, *device, sink, source, std::move(failure));
    if (auto error = stream->initialize()) {
        return *error;
    }
    return stream;
}

} // namespace

audio::OpenResult CoreAudioPlatform::open_capture(const std::optional<caps::AudioEndpointId>& device,
                                                  audio::CaptureSink& sink, audio::StreamFailure failure) {
    switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]) {
    case AVAuthorizationStatusDenied:
    case AVAuthorizationStatusRestricted:
        return AudioError{AudioErrorCode::permission_denied};
    case AVAuthorizationStatusNotDetermined:
    case AVAuthorizationStatusAuthorized:
        break;
    }
    return open_stream(Direction::capture, device, &sink, nullptr, std::move(failure));
}

audio::OpenResult CoreAudioPlatform::open_render(const std::optional<caps::AudioEndpointId>& device,
                                                 audio::RenderSource& source, audio::StreamFailure failure) {
    return open_stream(Direction::render, device, nullptr, &source, std::move(failure));
}

std::optional<caps::AudioEndpointId> CoreAudioPlatform::default_device(audio::DeviceDirection direction) {
    const auto native = direction == audio::DeviceDirection::capture ? Direction::capture : Direction::render;
    const auto device = resolve(std::nullopt, native);
    const auto uid = device ? uid_of(*device) : std::nullopt;
    if (!uid) {
        return std::nullopt;
    }
    return caps::AudioEndpointId{"coreaudio:" + *uid + std::string(suffix_of(native)), caps::IdentityScope::persistent};
}

bool CoreAudioPlatform::device_available(const caps::AudioEndpointId& device,
                                         audio::DeviceDirection direction) {
    const auto native = direction == audio::DeviceDirection::capture ? Direction::capture : Direction::render;
    return resolve(std::optional<caps::AudioEndpointId>{device}, native).has_value();
}

} // namespace catro::platform::macos

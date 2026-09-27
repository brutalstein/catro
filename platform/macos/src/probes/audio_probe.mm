#include "../probe_support.hpp"

#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace catro::platform::macos {
namespace {

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

// Variable-size property data; nothing when the property is absent or unreadable.
std::optional<std::vector<std::byte>> bytes(AudioObjectID object, const AudioObjectPropertyAddress& where) {
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(object, &where, 0, nullptr, &size) != noErr) {
        return std::nullopt;
    }
    std::vector<std::byte> data(size);
    if (size > 0 && AudioObjectGetPropertyData(object, &where, 0, nullptr, &size, data.data()) != noErr) {
        return std::nullopt;
    }
    data.resize(size);
    return data;
}

// CoreAudio hands over retained strings.
std::optional<std::string> text(AudioObjectID object, AudioObjectPropertySelector selector) {
    const auto value = scalar<CFStringRef>(object, address(selector));
    if (!value || *value == nullptr) {
        return std::nullopt;
    }
    std::optional<std::string> result;
    const auto length = CFStringGetMaximumSizeForEncoding(CFStringGetLength(*value), kCFStringEncodingUTF8) + 1;
    std::string buffer(static_cast<std::size_t>(length), '\0');
    if (CFStringGetCString(*value, buffer.data(), length, kCFStringEncodingUTF8)) {
        buffer.resize(std::char_traits<char>::length(buffer.c_str()));
        result = std::move(buffer);
    }
    CFRelease(*value);
    return result;
}

std::optional<std::uint32_t> channels(AudioObjectID device, AudioObjectPropertyScope scope) {
    const auto data = bytes(device, address(kAudioDevicePropertyStreamConfiguration, scope));
    if (!data || data->size() < offsetof(AudioBufferList, mBuffers)) {
        return std::nullopt;
    }
    const auto* list = reinterpret_cast<const AudioBufferList*>(data->data());
    if (data->size() < offsetof(AudioBufferList, mBuffers) + std::size_t{list->mNumberBuffers} * sizeof(AudioBuffer)) {
        return std::nullopt;
    }
    std::uint32_t total = 0;
    for (UInt32 index = 0; index < list->mNumberBuffers; ++index) {
        total += list->mBuffers[index].mNumberChannels;
    }
    return total;
}

std::optional<caps::SampleFormat> sample_format(const AudioStreamBasicDescription& format) {
    if (format.mFormatID != kAudioFormatLinearPCM) {
        return std::nullopt;
    }
    if ((format.mFormatFlags & kAudioFormatFlagIsFloat) != 0) {
        return format.mBitsPerChannel == 32 ? std::optional{caps::SampleFormat::pcm_f32} : std::nullopt;
    }
    switch (format.mBitsPerChannel) {
    case 16:
        return caps::SampleFormat::pcm_s16;
    case 24:
        return caps::SampleFormat::pcm_s24;
    case 32:
        return caps::SampleFormat::pcm_s32;
    default:
        return std::nullopt;
    }
}

// Format of the first stream in the direction, as the HAL presents it to clients.
std::optional<caps::SampleFormat> stream_format(AudioObjectID device, AudioObjectPropertyScope scope) {
    const auto data = bytes(device, address(kAudioDevicePropertyStreams, scope));
    if (!data || data->size() < sizeof(AudioStreamID)) {
        return std::nullopt;
    }
    AudioStreamID stream = 0;
    std::memcpy(&stream, data->data(), sizeof(stream));
    const auto format = scalar<AudioStreamBasicDescription>(stream, address(kAudioStreamPropertyVirtualFormat));
    return format ? sample_format(*format) : std::nullopt;
}

} // namespace

caps::ProbeFragment run_audio_probe(const caps::ProbeSpec& spec) {
    const auto started = std::chrono::steady_clock::now();
    auto fragment = begin_fragment(spec);

    // Reads HAL properties only; no device is started and no stream is opened.
    std::vector<NativeAudioDevice> endpoints;
    const auto list = bytes(kAudioObjectSystemObject, address(kAudioHardwarePropertyDevices));
    if (list) {
        std::vector<AudioObjectID> devices(list->size() / sizeof(AudioObjectID));
        std::memcpy(devices.data(), list->data(), devices.size() * sizeof(AudioObjectID));
        const auto default_input =
            scalar<AudioObjectID>(kAudioObjectSystemObject, address(kAudioHardwarePropertyDefaultInputDevice));
        const auto default_output =
            scalar<AudioObjectID>(kAudioObjectSystemObject, address(kAudioHardwarePropertyDefaultOutputDevice));
        const std::tuple<AudioObjectPropertyScope, caps::AudioDirection, std::optional<AudioObjectID>> directions[] = {
            {kAudioObjectPropertyScopeInput, caps::AudioDirection::input, default_input},
            {kAudioObjectPropertyScopeOutput, caps::AudioDirection::output, default_output},
        };

        for (const auto device : devices) {
            const auto uid = text(device, kAudioDevicePropertyDeviceUID);
            if (!uid) {
                add_issue(fragment.issues, spec.probe_id, caps::IssueCode::not_reported);
                continue;
            }
            const auto name = text(device, kAudioObjectPropertyName);
            const auto alive = scalar<UInt32>(device, address(kAudioDevicePropertyDeviceIsAlive));
            const auto rate = scalar<Float64>(device, address(kAudioDevicePropertyNominalSampleRate));
            for (const auto& [scope, direction, default_device] : directions) {
                // A device is an endpoint in each direction that has channels.
                const auto count = channels(device, scope);
                if (count.value_or(0) == 0) {
                    continue;
                }
                NativeAudioDevice endpoint{
                    .uid = *uid,
                    .direction = direction,
                    .name = name.value_or(""),
                    .alive = alive.value_or(1) != 0,
                    .channels = count,
                    .sample_format = stream_format(device, scope),
                };
                if (rate && *rate > 0 && *rate < 1e7) {
                    endpoint.sample_rate_hz = static_cast<std::uint32_t>(std::lround(*rate));
                }
                if (default_device) {
                    endpoint.is_default = *default_device == device;
                }
                endpoints.push_back(std::move(endpoint));
            }
        }
    } else {
        add_issue(fragment.issues, spec.probe_id, caps::IssueCode::os_failure);
    }

    fragment.audio = translate_audio(endpoints, spec.probe_id, fragment.issues);
    settle_outcome(fragment);
    finish_fragment(fragment, started);
    return fragment;
}

} // namespace catro::platform::macos

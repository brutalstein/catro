#include "../probe_support.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreMedia/CoreMedia.h>
#include <VideoToolbox/VideoToolbox.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace catro::platform::macos {
namespace {

constexpr FourCharCode four_cc(char a, char b, char c, char d) {
    return static_cast<FourCharCode>(a) << 24U | static_cast<FourCharCode>(b) << 16U |
           static_cast<FourCharCode>(c) << 8U | static_cast<FourCharCode>(d);
}

std::optional<caps::Codec> codec(FourCharCode type) {
    if (type == kCMVideoCodecType_H264) {
        return caps::Codec::h264;
    }
    if (type == kCMVideoCodecType_HEVC) {
        return caps::Codec::hevc;
    }
    // kCMVideoCodecType_AV1, spelled out for SDKs that predate the constant.
    if (type == four_cc('a', 'v', '0', '1')) {
        return caps::Codec::av1;
    }
    return std::nullopt;
}

std::string text(CFDictionaryRef entry, CFStringRef key) {
    const auto value = static_cast<CFStringRef>(CFDictionaryGetValue(entry, key));
    if (value == nullptr || CFGetTypeID(value) != CFStringGetTypeID()) {
        return {};
    }
    const auto length = CFStringGetMaximumSizeForEncoding(CFStringGetLength(value), kCFStringEncodingUTF8) + 1;
    std::string result(static_cast<std::size_t>(length), '\0');
    if (!CFStringGetCString(value, result.data(), length, kCFStringEncodingUTF8)) {
        return {};
    }
    result.resize(std::char_traits<char>::length(result.c_str()));
    return result;
}

std::optional<std::int64_t> number(CFDictionaryRef entry, CFStringRef key) {
    const auto value = static_cast<CFNumberRef>(CFDictionaryGetValue(entry, key));
    std::int64_t result = 0;
    if (value == nullptr || CFGetTypeID(value) != CFNumberGetTypeID() ||
        !CFNumberGetValue(value, kCFNumberSInt64Type, &result)) {
        return std::nullopt;
    }
    return result;
}

bool flag(CFDictionaryRef entry, CFStringRef key) {
    const auto value = static_cast<CFBooleanRef>(CFDictionaryGetValue(entry, key));
    return value != nullptr && CFGetTypeID(value) == CFBooleanGetTypeID() && CFBooleanGetValue(value);
}

} // namespace

caps::ProbeFragment run_encoder_probe(const caps::ProbeSpec& spec) {
    const auto started = std::chrono::steady_clock::now();
    auto fragment = begin_fragment(spec);

    // Lists registered encoders without creating a compression session.
    std::vector<NativeEncoder> encoders;
    CFArrayRef list = nullptr;
    const auto status = VTCopyVideoEncoderList(nullptr, &list);
    if (status == noErr && list != nullptr) {
        for (CFIndex index = 0; index < CFArrayGetCount(list); ++index) {
            const auto entry = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(list, index));
            if (entry == nullptr || CFGetTypeID(entry) != CFDictionaryGetTypeID()) {
                continue;
            }
            const auto type = number(entry, kVTVideoEncoderList_CodecType);
            const auto translated = type ? codec(static_cast<FourCharCode>(*type)) : std::nullopt;
            if (!translated) {
                continue;
            }
            NativeEncoder encoder{
                .codec = *translated,
                .encoder_id = text(entry, kVTVideoEncoderList_EncoderID),
                .name = text(entry, kVTVideoEncoderList_DisplayName),
                .hardware = flag(entry, kVTVideoEncoderList_IsHardwareAccelerated),
            };
            if (const auto gpu = number(entry, kVTVideoEncoderList_GPURegistryID); gpu && *gpu != 0) {
                encoder.gpu = static_cast<std::uint64_t>(*gpu);
            }
            encoders.push_back(std::move(encoder));
        }
    } else {
        fragment.native_error = status;
        add_issue(fragment.issues, spec.probe_id, caps::IssueCode::os_failure);
    }
    if (list != nullptr) {
        CFRelease(list);
    }

    const auto gpus = enumerate_gpus();
    fragment.encoders = translate_encoders(encoders, gpus, spec.probe_id, fragment.issues);
    settle_outcome(fragment);
    finish_fragment(fragment, started);
    return fragment;
}

} // namespace catro::platform::macos

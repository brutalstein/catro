#include <catro/platform/macos/video_encoder.hpp>

#import <CoreMedia/CoreMedia.h>
#import <Foundation/Foundation.h>
#import <VideoToolbox/VideoToolbox.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <utility>

namespace catro::platform::macos {
namespace {

constexpr std::array<std::byte, 4> kStartCode{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{1}};

bool append_bounded(std::vector<std::byte>& out, const std::uint8_t* data, std::size_t size, std::size_t limit) {
    if (out.size() + kStartCode.size() + size > limit) {
        return false;
    }
    out.insert(out.end(), kStartCode.begin(), kStartCode.end());
    const auto* bytes = reinterpret_cast<const std::byte*>(data);
    out.insert(out.end(), bytes, bytes + size);
    return true;
}

bool is_keyframe(CMSampleBufferRef sample) {
    CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sample, false);
    if (attachments == nullptr || CFArrayGetCount(attachments) == 0) {
        return true;
    }
    auto attachment = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(attachments, 0));
    return !CFDictionaryContainsKey(attachment, kCMSampleAttachmentKey_NotSync);
}

enum class Conversion { ok, malformed, too_large };

// AVCC (4-byte big-endian lengths) to Annex-B, prefixing SPS/PPS on keyframes.
Conversion to_annex_b(CMSampleBufferRef sample, bool keyframe, std::size_t limit, std::vector<std::byte>& out) {
    out.clear();
    if (keyframe) {
        CMFormatDescriptionRef format = CMSampleBufferGetFormatDescription(sample);
        std::size_t count = 0;
        if (CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format, 0, nullptr, nullptr, &count, nullptr) !=
            noErr) {
            return Conversion::malformed;
        }
        for (std::size_t index = 0; index < count; ++index) {
            const std::uint8_t* set = nullptr;
            std::size_t size = 0;
            if (CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format, index, &set, &size, nullptr, nullptr) !=
                noErr) {
                return Conversion::malformed;
            }
            if (!append_bounded(out, set, size, limit)) {
                return Conversion::too_large;
            }
        }
    }

    CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sample);
    char* data = nullptr;
    std::size_t total = 0;
    if (block == nullptr || CMBlockBufferGetDataPointer(block, 0, nullptr, &total, &data) != kCMBlockBufferNoErr) {
        return Conversion::malformed;
    }
    std::size_t offset = 0;
    while (offset + 4 <= total) {
        const auto* header = reinterpret_cast<const std::uint8_t*>(data + offset);
        const std::size_t length = (std::size_t{header[0]} << 24U) | (std::size_t{header[1]} << 16U) |
                                   (std::size_t{header[2]} << 8U) | std::size_t{header[3]};
        offset += 4;
        if (length == 0 || length > total - offset) {
            return Conversion::malformed;
        }
        if (!append_bounded(out, header + 4, length, limit)) {
            return Conversion::too_large;
        }
        offset += length;
    }
    return offset == total ? Conversion::ok : Conversion::malformed;
}

bool session_flag(VTCompressionSessionRef session, CFStringRef key) {
    CFBooleanRef value = nullptr;
    if (VTSessionCopyProperty(session, key, kCFAllocatorDefault, &value) != noErr || value == nullptr) {
        return false;
    }
    const bool result = CFBooleanGetValue(value);
    CFRelease(value);
    return result;
}

} // namespace

std::optional<H264EncoderError> validate(const H264EncoderConfig& config) noexcept {
    const bool even = config.width % 2 == 0 && config.height % 2 == 0;
    const bool size_ok = config.width >= 16 && config.height >= 16 && config.width <= 8192 && config.height <= 8192;
    const bool rate_ok = config.frame_rate_numerator != 0 && config.frame_rate_denominator != 0 &&
                         config.frame_rate_numerator / config.frame_rate_denominator <= 240;
    const bool bitrate_ok = config.bitrate >= 100'000 && config.bitrate <= 200'000'000;
    if (!even || !size_ok || !rate_ok || !bitrate_ok || config.gop_frames == 0 ||
        config.max_access_unit_bytes < 1024) {
        return H264EncoderError{H264EncoderErrorCode::invalid_config, 0};
    }
    return std::nullopt;
}

struct MacH264Encoder::Impl {
    VTCompressionSessionRef session = nullptr;
    H264EncoderConfig config;
    H264EncoderStatistics stats;
    std::uint64_t sequence = 0;

    void release() noexcept {
        if (session != nullptr) {
            VTCompressionSessionInvalidate(session);
            CFRelease(session);
            session = nullptr;
        }
    }
};

MacH264Encoder::MacH264Encoder() : impl_(std::make_unique<Impl>()) {}

MacH264Encoder::~MacH264Encoder() {
    stop();
}

std::optional<H264EncoderError> MacH264Encoder::start(const H264EncoderConfig& config) {
    stop();
    if (auto invalid = validate(config)) {
        return invalid;
    }
    impl_->config = config;
    impl_->stats = {};
    impl_->sequence = 0;

    @autoreleasepool {
        NSMutableDictionary* specification = [@{
            (__bridge NSString*)kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder : @YES,
            (__bridge NSString*)kVTVideoEncoderSpecification_EnableLowLatencyRateControl : @YES,
        } mutableCopy];
        if (config.require_hardware) {
            specification[(__bridge NSString*)kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder] = @YES;
        }
        NSDictionary* source_attributes = @{
            (__bridge NSString*)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
            (__bridge NSString*)kCVPixelBufferIOSurfacePropertiesKey : @{},
        };

        const auto create = [&](NSDictionary* spec) {
            return VTCompressionSessionCreate(kCFAllocatorDefault, static_cast<std::int32_t>(config.width),
                                              static_cast<std::int32_t>(config.height), kCMVideoCodecType_H264,
                                              (__bridge CFDictionaryRef)spec,
                                              (__bridge CFDictionaryRef)source_attributes, kCFAllocatorDefault,
                                              nullptr, nullptr, &impl_->session);
        };
        auto status = create(specification);
        impl_->stats.low_latency_rate_control = status == noErr;
        if (status != noErr && !config.require_hardware) {
            // Diagnostics only: the software encoder has no low-latency rate control.
            status = create(@{});
        }
        if (status != noErr) {
            impl_->session = nullptr;
            return H264EncoderError{config.require_hardware ? H264EncoderErrorCode::hardware_unavailable
                                                            : H264EncoderErrorCode::session_creation_failed,
                                    status};
        }

        impl_->stats.hardware_accelerated =
            session_flag(impl_->session, kVTCompressionPropertyKey_UsingHardwareAcceleratedVideoEncoder);
        if (config.require_hardware && !impl_->stats.hardware_accelerated) {
            impl_->release();
            return H264EncoderError{H264EncoderErrorCode::hardware_unavailable, 0};
        }

        const double frame_rate =
            static_cast<double>(config.frame_rate_numerator) / static_cast<double>(config.frame_rate_denominator);
        const std::array<std::pair<CFStringRef, CFTypeRef>, 6> properties{{
            {kVTCompressionPropertyKey_RealTime, kCFBooleanTrue},
            {kVTCompressionPropertyKey_AllowFrameReordering, kCFBooleanFalse},
            {kVTCompressionPropertyKey_ProfileLevel, kVTProfileLevel_H264_Main_AutoLevel},
            {kVTCompressionPropertyKey_AverageBitRate, (__bridge CFTypeRef) @(config.bitrate)},
            {kVTCompressionPropertyKey_ExpectedFrameRate, (__bridge CFTypeRef) @(frame_rate)},
            {kVTCompressionPropertyKey_MaxKeyFrameInterval, (__bridge CFTypeRef) @(config.gop_frames)},
        }};
        for (const auto& [key, value] : properties) {
            status = VTSessionSetProperty(impl_->session, key, value);
            if (status != noErr) {
                impl_->release();
                return H264EncoderError{H264EncoderErrorCode::property_failed, status};
            }
        }
        status = VTCompressionSessionPrepareToEncodeFrames(impl_->session);
        if (status != noErr) {
            impl_->release();
            return H264EncoderError{H264EncoderErrorCode::session_creation_failed, status};
        }
    }
    return std::nullopt;
}

std::optional<H264EncoderError> MacH264Encoder::encode(const PixelBuffer& source,
                                                        std::int64_t pts_100ns,
                                                        bool force_keyframe,
                                                        EncodedAccessUnit& output) {
    output.bytes.clear();
    output.keyframe = false;
    if (impl_->session == nullptr || !source) {
        return H264EncoderError{H264EncoderErrorCode::invalid_source, 0};
    }

    auto& stats = impl_->stats;
    stats.frames_submitted += 1;
    const auto started = std::chrono::steady_clock::now();
    __block OSStatus callback_status = noErr;
    __block Conversion conversion = Conversion::ok;
    __block bool emitted = false;
    __block bool keyframe = false;
    std::vector<std::byte>* bytes = &output.bytes;
    const std::size_t limit = impl_->config.max_access_unit_bytes;

    OSStatus status = noErr;
    @autoreleasepool {
        NSDictionary* frame_options =
            force_keyframe ? @{(__bridge NSString*)kVTEncodeFrameOptionKey_ForceKeyFrame : @YES} : nil;
        VTEncodeInfoFlags flags = 0;
        status = VTCompressionSessionEncodeFrameWithOutputHandler(
            impl_->session, source.get(), CMTimeMake(pts_100ns, 10'000'000), kCMTimeInvalid,
            (__bridge CFDictionaryRef)frame_options, &flags,
            ^(OSStatus result, VTEncodeInfoFlags, CMSampleBufferRef sample) {
              callback_status = result;
              if (result != noErr || sample == nullptr) {
                  return;
              }
              emitted = true;
              keyframe = is_keyframe(sample);
              conversion = to_annex_b(sample, keyframe, limit, *bytes);
            });
        if (status == noErr) {
            status = VTCompressionSessionCompleteFrames(impl_->session, kCMTimeInvalid);
        }
    }

    const auto elapsed = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
    stats.encode_total_us += elapsed;
    stats.encode_max_us = std::max(stats.encode_max_us, elapsed);

    if (status != noErr || callback_status != noErr || conversion == Conversion::malformed) {
        stats.encode_failures += 1;
        output.bytes.clear();
        return H264EncoderError{H264EncoderErrorCode::encode_failed, status != noErr ? status : callback_status};
    }
    if (conversion == Conversion::too_large) {
        stats.oversized_outputs += 1;
        output.bytes.clear();
        return H264EncoderError{H264EncoderErrorCode::output_too_large, 0};
    }
    if (!emitted) {
        stats.frames_dropped += 1;
        return std::nullopt;
    }
    output.keyframe = keyframe;
    output.sequence = ++impl_->sequence;
    output.pts_100ns = pts_100ns;
    stats.frames_encoded += 1;
    stats.encoded_bytes += output.bytes.size();
    stats.keyframes += keyframe ? 1U : 0U;
    return std::nullopt;
}

void MacH264Encoder::stop() noexcept {
    impl_->release();
}

bool MacH264Encoder::running() const noexcept {
    return impl_->session != nullptr;
}

H264EncoderStatistics MacH264Encoder::statistics() const noexcept {
    return impl_->stats;
}

} // namespace catro::platform::macos

#include <catro/platform/macos/video_decoder.hpp>

#import <CoreMedia/CoreMedia.h>
#import <Foundation/Foundation.h>
#import <VideoToolbox/VideoToolbox.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <vector>

namespace catro::platform::macos {
namespace {

using Nal = std::span<const std::byte>;

// Splits an Annex-B access unit on 3- or 4-byte start codes. Rejects leading garbage and empty NALs.
bool split_annex_b(std::span<const std::byte> input, std::vector<Nal>& nals) {
    nals.clear();
    const auto start_code_at = [&](std::size_t index) -> std::size_t {
        if (index + 3 <= input.size() && input[index] == std::byte{0} && input[index + 1] == std::byte{0}) {
            if (input[index + 2] == std::byte{1}) {
                return 3;
            }
            if (index + 4 <= input.size() && input[index + 2] == std::byte{0} && input[index + 3] == std::byte{1}) {
                return 4;
            }
        }
        return 0;
    };

    std::size_t prefix = start_code_at(0);
    if (prefix == 0) {
        return false;
    }
    std::size_t begin = prefix;
    for (std::size_t index = begin; index < input.size();) {
        const auto next = start_code_at(index);
        if (next == 0) {
            ++index;
            continue;
        }
        if (index == begin) {
            return false;
        }
        nals.push_back(input.subspan(begin, index - begin));
        index += next;
        begin = index;
    }
    if (begin >= input.size()) {
        return false;
    }
    nals.push_back(input.subspan(begin));
    return true;
}

std::uint8_t nal_type(Nal nal) {
    return static_cast<std::uint8_t>(std::to_integer<std::uint8_t>(nal[0]) & 0x1FU);
}

bool session_flag(VTDecompressionSessionRef session, CFStringRef key) {
    CFBooleanRef value = nullptr;
    if (VTSessionCopyProperty(session, key, kCFAllocatorDefault, &value) != noErr || value == nullptr) {
        return false;
    }
    const bool result = CFBooleanGetValue(value);
    CFRelease(value);
    return result;
}

} // namespace

struct MacH264Decoder::Impl {
    H264DecoderConfig config;
    H264DecoderStatistics stats;
    bool started = false;
    VTDecompressionSessionRef session = nullptr;
    CMVideoFormatDescriptionRef format = nullptr;
    std::vector<std::byte> sps;
    std::vector<std::byte> pps;
    std::vector<Nal> nals;

    void release_session() noexcept {
        if (session != nullptr) {
            VTDecompressionSessionWaitForAsynchronousFrames(session);
            VTDecompressionSessionInvalidate(session);
            CFRelease(session);
            session = nullptr;
        }
        if (format != nullptr) {
            CFRelease(format);
            format = nullptr;
        }
    }

    std::optional<H264DecoderError> rebuild(Nal new_sps, Nal new_pps) {
        release_session();
        const std::array<const std::uint8_t*, 2> sets{reinterpret_cast<const std::uint8_t*>(new_sps.data()),
                                                      reinterpret_cast<const std::uint8_t*>(new_pps.data())};
        const std::array<std::size_t, 2> sizes{new_sps.size(), new_pps.size()};
        auto status = CMVideoFormatDescriptionCreateFromH264ParameterSets(kCFAllocatorDefault, 2, sets.data(),
                                                                          sizes.data(), 4, &format);
        if (status != noErr) {
            format = nullptr;
            return H264DecoderError{H264DecoderErrorCode::format_failed, status};
        }

        @autoreleasepool {
            NSMutableDictionary* specification = [@{
                (__bridge NSString*)kVTVideoDecoderSpecification_EnableHardwareAcceleratedVideoDecoder : @YES,
            } mutableCopy];
            if (config.require_hardware) {
                specification[(__bridge NSString*)
                                  kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder] = @YES;
            }
            NSDictionary* destination = @{
                (__bridge NSString*)kCVPixelBufferPixelFormatTypeKey :
                    @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
                (__bridge NSString*)kCVPixelBufferIOSurfacePropertiesKey : @{},
                (__bridge NSString*)kCVPixelBufferMetalCompatibilityKey : @YES,
            };
            status = VTDecompressionSessionCreate(kCFAllocatorDefault, format, (__bridge CFDictionaryRef)specification,
                                                  (__bridge CFDictionaryRef)destination, nullptr, &session);
        }
        if (status != noErr) {
            session = nullptr;
            release_session();
            return H264DecoderError{config.require_hardware ? H264DecoderErrorCode::hardware_unavailable
                                                            : H264DecoderErrorCode::session_creation_failed,
                                    status};
        }
        stats.hardware_accelerated =
            session_flag(session, kVTDecompressionPropertyKey_UsingHardwareAcceleratedVideoDecoder);
        if (config.require_hardware && !stats.hardware_accelerated) {
            release_session();
            return H264DecoderError{H264DecoderErrorCode::hardware_unavailable, 0};
        }
        const auto dimensions = CMVideoFormatDescriptionGetDimensions(format);
        stats.width = static_cast<std::uint32_t>(dimensions.width);
        stats.height = static_cast<std::uint32_t>(dimensions.height);
        sps.assign(new_sps.begin(), new_sps.end());
        pps.assign(new_pps.begin(), new_pps.end());
        return std::nullopt;
    }
};

MacH264Decoder::MacH264Decoder() : impl_(std::make_unique<Impl>()) {}

MacH264Decoder::~MacH264Decoder() {
    stop();
}

std::optional<H264DecoderError> MacH264Decoder::start(const H264DecoderConfig& config) {
    stop();
    if (config.max_access_unit_bytes < 1024 || config.max_access_unit_bytes > 64U * 1024U * 1024U) {
        return H264DecoderError{H264DecoderErrorCode::invalid_config, 0};
    }
    impl_->config = config;
    impl_->stats = {};
    impl_->started = true;
    return std::nullopt;
}

std::optional<H264DecoderError> MacH264Decoder::decode(std::span<const std::byte> access_unit,
                                                        std::int64_t pts_100ns,
                                                        DecodedFrame& output) {
    output.buffer.reset();
    output.pts_100ns = pts_100ns;
    auto& impl = *impl_;
    auto& stats = impl.stats;
    if (!impl.started) {
        return H264DecoderError{H264DecoderErrorCode::invalid_config, 0};
    }
    if (access_unit.size() > impl.config.max_access_unit_bytes) {
        stats.oversized_inputs += 1;
        return H264DecoderError{H264DecoderErrorCode::input_too_large, 0};
    }
    if (!split_annex_b(access_unit, impl.nals)) {
        stats.malformed_inputs += 1;
        return H264DecoderError{H264DecoderErrorCode::malformed_input, 0};
    }
    stats.frames_submitted += 1;
    stats.compressed_bytes += access_unit.size();

    Nal new_sps;
    Nal new_pps;
    std::size_t sample_bytes = 0;
    for (const auto nal : impl.nals) {
        const auto type = nal_type(nal);
        if (type == 7) {
            new_sps = nal;
        } else if (type == 8) {
            new_pps = nal;
        } else if (type >= 1 && type <= 5) {
            sample_bytes += 4 + nal.size();
        }
    }
    if (!new_sps.empty() && !new_pps.empty() &&
        (impl.session == nullptr || !std::ranges::equal(new_sps, impl.sps) || !std::ranges::equal(new_pps, impl.pps))) {
        const bool replacing = impl.session != nullptr;
        if (auto failure = impl.rebuild(new_sps, new_pps)) {
            return failure;
        }
        stats.stream_changes += replacing ? 1U : 0U;
    }
    if (sample_bytes == 0) {
        return std::nullopt;
    }
    if (impl.session == nullptr) {
        stats.waiting_for_parameter_sets += 1;
        return std::nullopt;
    }

    const auto started = std::chrono::steady_clock::now();
    CMBlockBufferRef block = nullptr;
    auto status = CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, nullptr, sample_bytes, kCFAllocatorDefault,
                                                     nullptr, 0, sample_bytes, kCMBlockBufferAssureMemoryNowFlag,
                                                     &block);
    std::size_t offset = 0;
    for (const auto nal : impl.nals) {
        const auto type = nal_type(nal);
        if (status != kCMBlockBufferNoErr || type < 1 || type > 5) {
            continue;
        }
        const auto length = static_cast<std::uint32_t>(nal.size());
        const std::array<std::uint8_t, 4> header{static_cast<std::uint8_t>(length >> 24U),
                                                 static_cast<std::uint8_t>(length >> 16U),
                                                 static_cast<std::uint8_t>(length >> 8U),
                                                 static_cast<std::uint8_t>(length)};
        status = CMBlockBufferReplaceDataBytes(header.data(), block, offset, header.size());
        if (status == kCMBlockBufferNoErr) {
            status = CMBlockBufferReplaceDataBytes(nal.data(), block, offset + 4, nal.size());
        }
        offset += 4 + nal.size();
    }

    CMSampleBufferRef sample = nullptr;
    if (status == noErr) {
        const CMSampleTimingInfo timing{kCMTimeInvalid, CMTimeMake(pts_100ns, 10'000'000), kCMTimeInvalid};
        status = CMSampleBufferCreateReady(kCFAllocatorDefault, block, impl.format, 1, 1, &timing, 1, &sample_bytes,
                                           &sample);
    }
    if (block != nullptr) {
        CFRelease(block);
    }

    __block OSStatus callback_status = noErr;
    __block CVImageBufferRef decoded = nullptr;
    if (status == noErr) {
        VTDecodeInfoFlags flags = 0;
        status = VTDecompressionSessionDecodeFrameWithOutputHandler(
            impl.session, sample, 0, &flags,
            ^(OSStatus result, VTDecodeInfoFlags, CVImageBufferRef image, CMTime, CMTime) {
              callback_status = result;
              if (result == noErr && image != nullptr) {
                  decoded = CVPixelBufferRetain(image);
              }
            });
        if (status == noErr) {
            status = VTDecompressionSessionWaitForAsynchronousFrames(impl.session);
        }
    }
    if (sample != nullptr) {
        CFRelease(sample);
    }
    output.buffer = PixelBuffer::adopt(decoded);

    const auto elapsed = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
    stats.decode_total_us += elapsed;
    stats.decode_max_us = std::max(stats.decode_max_us, elapsed);
    if (status != noErr || callback_status != noErr) {
        stats.decode_failures += 1;
        output.buffer.reset();
        return H264DecoderError{H264DecoderErrorCode::decode_failed, status != noErr ? status : callback_status};
    }
    stats.frames_decoded += output.buffer ? 1U : 0U;
    return std::nullopt;
}

void MacH264Decoder::stop() noexcept {
    impl_->release_session();
    impl_->sps.clear();
    impl_->pps.clear();
    impl_->started = false;
}

bool MacH264Decoder::running() const noexcept {
    return impl_->started;
}

H264DecoderStatistics MacH264Decoder::statistics() const noexcept {
    return impl_->stats;
}

} // namespace catro::platform::macos

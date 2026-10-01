#include <catro/platform/macos/video_decoder.hpp>

#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <VideoToolbox/VideoToolbox.h>

#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <utility>
#include <vector>

namespace catro::platform::macos {
namespace {

struct NalUnit {
    const std::byte* bytes = nullptr;
    std::size_t size = 0;
};

[[nodiscard]] std::vector<NalUnit> split_annex_b(
    std::span<const std::byte> input) {
    std::vector<NalUnit> units;
    auto start_code = [&](std::size_t at) -> std::size_t {
        if (at + 3 <= input.size() &&
            input[at] == std::byte{0} &&
            input[at + 1] == std::byte{0} &&
            input[at + 2] == std::byte{1}) {
            return 3;
        }
        if (at + 4 <= input.size() &&
            input[at] == std::byte{0} &&
            input[at + 1] == std::byte{0} &&
            input[at + 2] == std::byte{0} &&
            input[at + 3] == std::byte{1}) {
            return 4;
        }
        return 0;
    };

    std::size_t cursor = 0;
    while (cursor < input.size()) {
        const auto prefix = start_code(cursor);
        if (prefix == 0) {
            return {};
        }
        const auto begin = cursor + prefix;
        auto end = begin;
        while (end < input.size() && start_code(end) == 0) {
            ++end;
        }
        if (begin == end) {
            return {};
        }
        units.push_back(NalUnit{input.data() + begin, end - begin});
        cursor = end;
    }
    return units;
}

class VideoToolboxDecoderAdapter final : public VideoDecoderNativeAdapter {
public:
    VideoDecoderStartResult start(
        const VideoDecoderConfig& config,
        OutputHandler on_output,
        FailureHandler on_failure) noexcept override {
        stop();
        config_ = config;
        on_output_ = std::move(on_output);
        on_failure_ = std::move(on_failure);
        active_ = true;
        return VideoDecoderStartResult{
            .hardware_accelerated = config.require_hardware,
            .implementation_name = config.require_hardware
                ? "VideoToolbox hardware H.264"
                : "VideoToolbox H.264",
        };
    }

    std::optional<VideoDecoderError> decode(
        std::span<const std::byte> access_unit,
        std::int64_t pts_100ns) noexcept override {
        if (!active_) {
            return VideoDecoderError{
                VideoDecoderErrorCode::input_failed, paramErr};
        }
        const auto units = split_annex_b(access_unit);
        if (units.empty()) {
            return VideoDecoderError{
                VideoDecoderErrorCode::malformed_access_unit, paramErr};
        }

        bool format_changed = false;
        for (const auto& unit : units) {
            const auto type =
                std::to_integer<std::uint8_t>(unit.bytes[0]) & 0x1fU;
            if (type == 7U) {
                std::vector<std::byte> value(unit.bytes, unit.bytes + unit.size);
                format_changed = value != sps_;
                sps_ = std::move(value);
            } else if (type == 8U) {
                std::vector<std::byte> value(unit.bytes, unit.bytes + unit.size);
                format_changed = format_changed || value != pps_;
                pps_ = std::move(value);
            }
        }
        if (format_changed && session_ != nullptr) {
            destroy_session();
        }
        if (session_ == nullptr) {
            if (sps_.empty() || pps_.empty()) {
                return std::nullopt;
            }
            if (const auto error = create_session()) {
                return error;
            }
        }

        std::vector<std::byte> avcc;
        avcc.reserve(access_unit.size());
        for (const auto& unit : units) {
            const auto size = static_cast<std::uint32_t>(unit.size);
            avcc.push_back(static_cast<std::byte>((size >> 24U) & 0xffU));
            avcc.push_back(static_cast<std::byte>((size >> 16U) & 0xffU));
            avcc.push_back(static_cast<std::byte>((size >> 8U) & 0xffU));
            avcc.push_back(static_cast<std::byte>(size & 0xffU));
            avcc.insert(avcc.end(), unit.bytes, unit.bytes + unit.size);
        }

        CMBlockBufferRef block = nullptr;
        OSStatus status = CMBlockBufferCreateWithMemoryBlock(
            kCFAllocatorDefault,
            nullptr,
            avcc.size(),
            kCFAllocatorDefault,
            nullptr,
            0,
            avcc.size(),
            0,
            &block);
        if (status != kCMBlockBufferNoErr || block == nullptr) {
            return VideoDecoderError{
                VideoDecoderErrorCode::input_failed, status};
        }
        status = CMBlockBufferReplaceDataBytes(
            avcc.data(), block, 0, avcc.size());
        if (status != kCMBlockBufferNoErr) {
            CFRelease(block);
            return VideoDecoderError{
                VideoDecoderErrorCode::input_failed, status};
        }

        const CMTime pts = CMTimeMake(pts_100ns, 10'000'000);
        const std::size_t sample_size = avcc.size();
        CMSampleTimingInfo timing{
            .duration = kCMTimeInvalid,
            .presentationTimeStamp = pts,
            .decodeTimeStamp = kCMTimeInvalid,
        };
        CMSampleBufferRef sample = nullptr;
        status = CMSampleBufferCreateReady(
            kCFAllocatorDefault,
            block,
            format_,
            1,
            1,
            &timing,
            1,
            &sample_size,
            &sample);
        CFRelease(block);
        if (status != noErr || sample == nullptr) {
            return VideoDecoderError{
                VideoDecoderErrorCode::input_failed, status};
        }

        auto* context = new (std::nothrow) FrameContext{
            sequence_.fetch_add(1, std::memory_order_relaxed) + 1,
            pts_100ns};
        if (context == nullptr) {
            CFRelease(sample);
            return VideoDecoderError{
                VideoDecoderErrorCode::input_failed, memFullErr};
        }
        VTDecodeInfoFlags flags = 0;
        status = VTDecompressionSessionDecodeFrame(
            session_,
            sample,
            kVTDecodeFrame_EnableAsynchronousDecompression,
            context,
            &flags);
        CFRelease(sample);
        if (status != noErr) {
            delete context;
            return VideoDecoderError{
                VideoDecoderErrorCode::input_failed, status};
        }
        return std::nullopt;
    }

    void stop() noexcept override {
        active_ = false;
        destroy_session();
        sps_.clear();
        pps_.clear();
        on_output_ = {};
        on_failure_ = {};
    }

    ~VideoToolboxDecoderAdapter() override {
        stop();
    }

private:
    struct FrameContext {
        std::uint64_t sequence = 0;
        std::int64_t pts_100ns = 0;
    };

    static void output_callback(
        void* context,
        void* frame_context,
        OSStatus status,
        VTDecodeInfoFlags,
        CVImageBufferRef image,
        CMTime,
        CMTime) {
        auto* self = static_cast<VideoToolboxDecoderAdapter*>(context);
        std::unique_ptr<FrameContext> frame(
            static_cast<FrameContext*>(frame_context));
        if (status != noErr || image == nullptr) {
            if (self->on_failure_) {
                self->on_failure_(VideoDecoderError{
                    VideoDecoderErrorCode::output_failed, status});
            }
            return;
        }
        CVPixelBufferRef pixel = static_cast<CVPixelBufferRef>(image);
        CVPixelBufferRetain(pixel);
        std::shared_ptr<void> lease(
            pixel,
            [](void* value) {
                CVPixelBufferRelease(static_cast<CVPixelBufferRef>(value));
            });
        if (self->on_output_) {
            self->on_output_(NativeVideoFrame{
                .lease = std::move(lease),
                .pixel_buffer = pixel,
                .sequence = frame ? frame->sequence : 0,
                .width = static_cast<std::uint32_t>(
                    CVPixelBufferGetWidth(pixel)),
                .height = static_cast<std::uint32_t>(
                    CVPixelBufferGetHeight(pixel)),
                .pixel_format = CVPixelBufferGetPixelFormatType(pixel),
                .pts_100ns = frame ? frame->pts_100ns : 0,
            });
        }
    }

    [[nodiscard]] std::optional<VideoDecoderError> create_session() noexcept {
        const std::uint8_t* sets[] = {
            reinterpret_cast<const std::uint8_t*>(sps_.data()),
            reinterpret_cast<const std::uint8_t*>(pps_.data())};
        const std::size_t sizes[] = {sps_.size(), pps_.size()};
        OSStatus status =
            CMVideoFormatDescriptionCreateFromH264ParameterSets(
                kCFAllocatorDefault,
                2,
                sets,
                sizes,
                4,
                &format_);
        if (status != noErr || format_ == nullptr) {
            return VideoDecoderError{
                VideoDecoderErrorCode::malformed_access_unit, status};
        }

        NSDictionary* specification = @{
            (__bridge NSString*)
                kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder :
                    @(config_.require_hardware),
            (__bridge NSString*)
                kVTVideoDecoderSpecification_EnableHardwareAcceleratedVideoDecoder :
                    @YES,
        };
        NSDictionary* attributes = @{
            (__bridge NSString*)kCVPixelBufferMetalCompatibilityKey : @YES,
            (__bridge NSString*)kCVPixelBufferPixelFormatTypeKey :
                @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
        };
        VTDecompressionOutputCallbackRecord callback{
            &VideoToolboxDecoderAdapter::output_callback,
            this};
        status = VTDecompressionSessionCreate(
            kCFAllocatorDefault,
            format_,
            (__bridge CFDictionaryRef)specification,
            (__bridge CFDictionaryRef)attributes,
            &callback,
            &session_);
        if (status != noErr || session_ == nullptr) {
            destroy_session();
            return VideoDecoderError{
                config_.require_hardware
                    ? VideoDecoderErrorCode::hardware_decoder_unavailable
                    : VideoDecoderErrorCode::session_creation_failed,
                status};
        }
        return std::nullopt;
    }

    void destroy_session() noexcept {
        if (session_ != nullptr) {
            VTDecompressionSessionWaitForAsynchronousFrames(session_);
            VTDecompressionSessionInvalidate(session_);
            CFRelease(session_);
            session_ = nullptr;
        }
        if (format_ != nullptr) {
            CFRelease(format_);
            format_ = nullptr;
        }
    }

    VTDecompressionSessionRef session_ = nullptr;
    CMVideoFormatDescriptionRef format_ = nullptr;
    VideoDecoderConfig config_;
    OutputHandler on_output_;
    FailureHandler on_failure_;
    std::vector<std::byte> sps_;
    std::vector<std::byte> pps_;
    std::atomic_uint64_t sequence_{0};
    bool active_ = false;
};

[[nodiscard]] bool valid_annex_b(
    std::span<const std::byte> input) noexcept {
    if (input.size() < 4) {
        return false;
    }
    return (input[0] == std::byte{0} &&
            input[1] == std::byte{0} &&
            input[2] == std::byte{1}) ||
           (input.size() >= 5 &&
            input[0] == std::byte{0} &&
            input[1] == std::byte{0} &&
            input[2] == std::byte{0} &&
            input[3] == std::byte{1});
}

} // namespace

struct MacH264HardwareDecoder::Impl {
    explicit Impl(std::unique_ptr<VideoDecoderNativeAdapter> value)
        : adapter(std::move(value)) {}

    std::unique_ptr<VideoDecoderNativeAdapter> adapter;
    mutable std::mutex mutex;
    VideoDecoderStatistics stats;
    VideoDecoderConfig config;
    OutputHandler on_output;
    FailureHandler on_failure;
    std::uint64_t generation = 0;
    bool active = false;
};

std::unique_ptr<VideoDecoderNativeAdapter>
make_video_decoder_native_adapter() {
    return std::make_unique<VideoToolboxDecoderAdapter>();
}

MacH264HardwareDecoder::MacH264HardwareDecoder()
    : MacH264HardwareDecoder(make_video_decoder_native_adapter()) {}

MacH264HardwareDecoder::MacH264HardwareDecoder(
    std::unique_ptr<VideoDecoderNativeAdapter> adapter)
    : impl_(std::make_unique<Impl>(std::move(adapter))) {}

MacH264HardwareDecoder::~MacH264HardwareDecoder() {
    stop();
}

std::optional<VideoDecoderError> MacH264HardwareDecoder::start(
    const VideoDecoderConfig& config,
    OutputHandler on_output,
    FailureHandler on_failure) noexcept {
    stop();
    if (config.max_access_unit_bytes < 5) {
        return VideoDecoderError{VideoDecoderErrorCode::invalid_config, 0};
    }
    std::uint64_t current = 0;
    {
        std::scoped_lock lock(impl_->mutex);
        impl_->config = config;
        impl_->on_output = std::move(on_output);
        impl_->on_failure = std::move(on_failure);
        impl_->stats = {};
        current = ++impl_->generation;
    }
    const auto result = impl_->adapter->start(
        config,
        [owner = impl_.get(), current](NativeVideoFrame output) {
            OutputHandler handler;
            {
                std::scoped_lock lock(owner->mutex);
                if (!owner->active || owner->generation != current) {
                    return;
                }
                ++owner->stats.frames_decoded;
                handler = owner->on_output;
            }
            if (handler) {
                handler(output);
            }
        },
        [owner = impl_.get(), current](VideoDecoderError error) {
            FailureHandler handler;
            {
                std::scoped_lock lock(owner->mutex);
                if (!owner->active || owner->generation != current) {
                    return;
                }
                ++owner->stats.output_failures;
                handler = owner->on_failure;
            }
            if (handler) {
                handler(error);
            }
        });
    if (result.error) {
        return result.error;
    }
    if (config.require_hardware && !result.hardware_accelerated) {
        impl_->adapter->stop();
        return VideoDecoderError{
            VideoDecoderErrorCode::hardware_decoder_unavailable, 0};
    }
    {
        std::scoped_lock lock(impl_->mutex);
        impl_->stats.hardware_accelerated = result.hardware_accelerated;
        impl_->stats.implementation_name = result.implementation_name;
        impl_->active = true;
    }
    return std::nullopt;
}

std::optional<VideoDecoderError> MacH264HardwareDecoder::decode(
    std::span<const std::byte> access_unit,
    std::int64_t pts_100ns) noexcept {
    {
        std::scoped_lock lock(impl_->mutex);
        if (!impl_->active) {
            ++impl_->stats.input_failures;
            return VideoDecoderError{
                VideoDecoderErrorCode::input_failed, 0};
        }
        if (access_unit.size() > impl_->config.max_access_unit_bytes) {
            ++impl_->stats.oversized_inputs;
            return VideoDecoderError{
                VideoDecoderErrorCode::input_too_large, 0};
        }
        if (!valid_annex_b(access_unit)) {
            ++impl_->stats.malformed_inputs;
            return VideoDecoderError{
                VideoDecoderErrorCode::malformed_access_unit, 0};
        }
        ++impl_->stats.frames_submitted;
        impl_->stats.compressed_bytes += access_unit.size();
    }
    const auto error = impl_->adapter->decode(access_unit, pts_100ns);
    if (error) {
        std::scoped_lock lock(impl_->mutex);
        ++impl_->stats.input_failures;
    }
    return error;
}

void MacH264HardwareDecoder::stop() noexcept {
    bool was_active = false;
    {
        std::scoped_lock lock(impl_->mutex);
        was_active = impl_->active;
        ++impl_->generation;
        impl_->active = false;
        impl_->on_output = {};
        impl_->on_failure = {};
    }
    if (was_active) {
        impl_->adapter->stop();
    }
}

bool MacH264HardwareDecoder::running() const noexcept {
    std::scoped_lock lock(impl_->mutex);
    return impl_->active;
}

VideoDecoderStatistics MacH264HardwareDecoder::statistics() const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->stats;
}

} // namespace catro::platform::macos

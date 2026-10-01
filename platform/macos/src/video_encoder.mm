#include <catro/platform/macos/video_encoder.hpp>

#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <VideoToolbox/VideoToolbox.h>

#include <array>
#include <cstring>
#include <mutex>
#include <utility>

namespace catro::platform::macos {
namespace {

constexpr std::array<std::byte, 4> kStartCode{
    std::byte{0}, std::byte{0}, std::byte{0}, std::byte{1}};

void append_nal(
    std::vector<std::byte>& destination,
    const std::uint8_t* bytes,
    std::size_t size) {
    destination.insert(
        destination.end(), kStartCode.begin(), kStartCode.end());
    destination.insert(
        destination.end(),
        reinterpret_cast<const std::byte*>(bytes),
        reinterpret_cast<const std::byte*>(bytes + size));
}

[[nodiscard]] bool is_keyframe(CMSampleBufferRef sample) noexcept {
    CFArrayRef attachments =
        CMSampleBufferGetSampleAttachmentsArray(sample, false);
    if (attachments == nullptr || CFArrayGetCount(attachments) == 0) {
        return true;
    }
    auto* values = static_cast<CFDictionaryRef>(
        const_cast<void*>(CFArrayGetValueAtIndex(attachments, 0)));
    return !CFDictionaryContainsKey(
        values, kCMSampleAttachmentKey_NotSync);
}

class VideoToolboxEncoderAdapter final : public VideoEncoderNativeAdapter {
public:
    VideoEncoderStartResult start(
        const VideoEncoderConfig& config,
        OutputHandler on_output,
        FailureHandler on_failure) noexcept override {
        stop();
        config_ = config;
        on_output_ = std::move(on_output);
        on_failure_ = std::move(on_failure);

        NSDictionary* specification = @{
            (__bridge NSString*)
                kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder :
                    @(config.require_hardware),
            (__bridge NSString*)
                kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder :
                    @YES,
        };
        const OSStatus status = VTCompressionSessionCreate(
            kCFAllocatorDefault,
            static_cast<int32_t>(config.width),
            static_cast<int32_t>(config.height),
            kCMVideoCodecType_H264,
            (__bridge CFDictionaryRef)specification,
            nullptr,
            nullptr,
            &VideoToolboxEncoderAdapter::output_callback,
            this,
            &session_);
        if (status != noErr || session_ == nullptr) {
            stop();
            return VideoEncoderStartResult{
                .error = VideoEncoderError{
                    config.require_hardware
                        ? VideoEncoderErrorCode::hardware_encoder_unavailable
                        : VideoEncoderErrorCode::session_creation_failed,
                    status}};
        }

        if (!set_property(kVTCompressionPropertyKey_RealTime, kCFBooleanTrue) ||
            !set_property(
                kVTCompressionPropertyKey_AllowFrameReordering,
                kCFBooleanFalse) ||
            !set_number(
                kVTCompressionPropertyKey_AverageBitRate,
                config.bitrate) ||
            !set_number(
                kVTCompressionPropertyKey_ExpectedFrameRate,
                config.frame_rate) ||
            !set_number(
                kVTCompressionPropertyKey_MaxKeyFrameInterval,
                config.gop_frames) ||
            !set_property(
                kVTCompressionPropertyKey_ProfileLevel,
                kVTProfileLevel_H264_Main_AutoLevel)) {
            stop();
            return VideoEncoderStartResult{
                .error = VideoEncoderError{
                    VideoEncoderErrorCode::property_failed, last_status_}};
        }

        const OSStatus prepare =
            VTCompressionSessionPrepareToEncodeFrames(session_);
        if (prepare != noErr) {
            stop();
            return VideoEncoderStartResult{
                .error = VideoEncoderError{
                    VideoEncoderErrorCode::session_creation_failed, prepare}};
        }

        bool hardware = false;
        CFTypeRef value = nullptr;
        if (VTSessionCopyProperty(
                session_,
                kVTCompressionPropertyKey_UsingHardwareAcceleratedVideoEncoder,
                kCFAllocatorDefault,
                &value) == noErr &&
            value != nullptr) {
            hardware = CFBooleanGetValue(static_cast<CFBooleanRef>(value));
            CFRelease(value);
        }
        return VideoEncoderStartResult{
            .hardware_accelerated = hardware,
            .implementation_name = hardware
                ? "VideoToolbox hardware H.264"
                : "VideoToolbox H.264",
        };
    }

    std::optional<VideoEncoderError> encode(
        const NativeVideoFrame& frame,
        bool force_keyframe) noexcept override {
        if (session_ == nullptr || !frame) {
            return VideoEncoderError{
                VideoEncoderErrorCode::input_failed, paramErr};
        }
        auto* context = new (std::nothrow) FrameContext{
            frame.sequence, frame.pts_100ns};
        if (context == nullptr) {
            return VideoEncoderError{
                VideoEncoderErrorCode::input_failed, memFullErr};
        }

        NSDictionary* properties = force_keyframe
            ? @{
                (__bridge NSString*)kVTEncodeFrameOptionKey_ForceKeyFrame :
                    @YES}
            : nil;
        const CMTime pts = CMTimeMake(frame.pts_100ns, 10'000'000);
        const CMTime duration = CMTimeMake(1, config_.frame_rate);
        VTEncodeInfoFlags flags = 0;
        const OSStatus status = VTCompressionSessionEncodeFrame(
            session_,
            static_cast<CVPixelBufferRef>(frame.pixel_buffer),
            pts,
            duration,
            (__bridge CFDictionaryRef)properties,
            context,
            &flags);
        if (status != noErr) {
            delete context;
            return VideoEncoderError{
                VideoEncoderErrorCode::input_failed, status};
        }
        return std::nullopt;
    }

    void stop() noexcept override {
        if (session_ != nullptr) {
            VTCompressionSessionCompleteFrames(session_, kCMTimeInvalid);
            VTCompressionSessionInvalidate(session_);
            CFRelease(session_);
            session_ = nullptr;
        }
        on_output_ = {};
        on_failure_ = {};
    }

    ~VideoToolboxEncoderAdapter() override {
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
        VTEncodeInfoFlags,
        CMSampleBufferRef sample) {
        auto* self = static_cast<VideoToolboxEncoderAdapter*>(context);
        std::unique_ptr<FrameContext> frame(
            static_cast<FrameContext*>(frame_context));
        if (status != noErr || sample == nullptr ||
            !CMSampleBufferDataIsReady(sample)) {
            if (self->on_failure_) {
                self->on_failure_(VideoEncoderError{
                    VideoEncoderErrorCode::output_failed, status});
            }
            return;
        }

        EncodedAccessUnit output;
        output.sequence = frame ? frame->sequence : 0;
        output.pts_100ns = frame ? frame->pts_100ns : 0;
        output.keyframe = is_keyframe(sample);
        output.bytes.reserve(256 * 1024);

        if (output.keyframe) {
            CMFormatDescriptionRef format =
                CMSampleBufferGetFormatDescription(sample);
            for (std::size_t index = 0; index < 2; ++index) {
                const std::uint8_t* bytes = nullptr;
                std::size_t size = 0;
                std::size_t count = 0;
                int nal_length = 0;
                if (CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
                        format,
                        index,
                        &bytes,
                        &size,
                        &count,
                        &nal_length) == noErr) {
                    append_nal(output.bytes, bytes, size);
                }
            }
        }

        CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sample);
        const std::size_t total =
            block == nullptr ? 0 : CMBlockBufferGetDataLength(block);
        std::vector<std::uint8_t> storage(total);
        if (total == 0 ||
            CMBlockBufferCopyDataBytes(
                block, 0, total, storage.data()) != kCMBlockBufferNoErr) {
            if (self->on_failure_) {
                self->on_failure_(VideoEncoderError{
                    VideoEncoderErrorCode::output_failed, paramErr});
            }
            return;
        }

        std::size_t offset = 0;
        while (offset + 4 <= storage.size()) {
            const auto size =
                (static_cast<std::size_t>(storage[offset]) << 24U) |
                (static_cast<std::size_t>(storage[offset + 1]) << 16U) |
                (static_cast<std::size_t>(storage[offset + 2]) << 8U) |
                static_cast<std::size_t>(storage[offset + 3]);
            offset += 4;
            if (size == 0 || offset + size > storage.size() ||
                output.bytes.size() + kStartCode.size() + size >
                    self->config_.max_access_unit_bytes) {
                if (self->on_failure_) {
                    self->on_failure_(VideoEncoderError{
                        size != 0 && offset + size <= storage.size()
                            ? VideoEncoderErrorCode::output_too_large
                            : VideoEncoderErrorCode::output_failed,
                        paramErr});
                }
                return;
            }
            append_nal(output.bytes, storage.data() + offset, size);
            offset += size;
        }
        if (offset != storage.size() || output.bytes.empty()) {
            if (self->on_failure_) {
                self->on_failure_(VideoEncoderError{
                    VideoEncoderErrorCode::output_failed, paramErr});
            }
            return;
        }
        if (self->on_output_) {
            self->on_output_(std::move(output));
        }
    }

    [[nodiscard]] bool set_property(
        CFStringRef key,
        CFTypeRef value) noexcept {
        last_status_ = VTSessionSetProperty(session_, key, value);
        return last_status_ == noErr;
    }

    [[nodiscard]] bool set_number(
        CFStringRef key,
        std::uint32_t value) noexcept {
        CFNumberRef number = CFNumberCreate(
            kCFAllocatorDefault, kCFNumberSInt32Type, &value);
        const bool ok = set_property(key, number);
        CFRelease(number);
        return ok;
    }

    VTCompressionSessionRef session_ = nullptr;
    VideoEncoderConfig config_;
    OutputHandler on_output_;
    FailureHandler on_failure_;
    OSStatus last_status_ = noErr;
};

} // namespace

struct MacH264HardwareEncoder::Impl {
    explicit Impl(std::unique_ptr<VideoEncoderNativeAdapter> value)
        : adapter(std::move(value)) {}

    std::unique_ptr<VideoEncoderNativeAdapter> adapter;
    mutable std::mutex mutex;
    VideoEncoderStatistics stats;
    VideoEncoderConfig config;
    OutputHandler on_output;
    FailureHandler on_failure;
    std::uint64_t generation = 0;
    bool active = false;
};

std::unique_ptr<VideoEncoderNativeAdapter>
make_video_encoder_native_adapter() {
    return std::make_unique<VideoToolboxEncoderAdapter>();
}

MacH264HardwareEncoder::MacH264HardwareEncoder()
    : MacH264HardwareEncoder(make_video_encoder_native_adapter()) {}

MacH264HardwareEncoder::MacH264HardwareEncoder(
    std::unique_ptr<VideoEncoderNativeAdapter> adapter)
    : impl_(std::make_unique<Impl>(std::move(adapter))) {}

MacH264HardwareEncoder::~MacH264HardwareEncoder() {
    stop();
}

std::optional<VideoEncoderError> MacH264HardwareEncoder::start(
    const VideoEncoderConfig& config,
    OutputHandler on_output,
    FailureHandler on_failure) noexcept {
    stop();
    if (config.width < 2 || config.height < 2 ||
        (config.width & 1U) != 0 || (config.height & 1U) != 0 ||
        config.frame_rate == 0 || config.bitrate == 0 ||
        config.gop_frames == 0 || config.max_access_unit_bytes < 5) {
        return VideoEncoderError{VideoEncoderErrorCode::invalid_config, 0};
    }

    std::uint64_t current = 0;
    {
        std::scoped_lock lock(impl_->mutex);
        impl_->config = config;
        impl_->on_output = std::move(on_output);
        impl_->on_failure = std::move(on_failure);
        impl_->stats = {};
        impl_->stats.low_latency_requested = true;
        current = ++impl_->generation;
    }
    const auto result = impl_->adapter->start(
        config,
        [owner = impl_.get(), current](EncodedAccessUnit output) {
            OutputHandler handler;
            {
                std::scoped_lock lock(owner->mutex);
                if (!owner->active || owner->generation != current) {
                    return;
                }
                ++owner->stats.frames_encoded;
                owner->stats.encoded_bytes += output.bytes.size();
                if (output.keyframe) {
                    ++owner->stats.keyframes;
                }
                handler = owner->on_output;
            }
            if (handler) {
                handler(output);
            }
        },
        [owner = impl_.get(), current](VideoEncoderError error) {
            FailureHandler handler;
            {
                std::scoped_lock lock(owner->mutex);
                if (!owner->active || owner->generation != current) {
                    return;
                }
                ++owner->stats.output_failures;
                if (error.code == VideoEncoderErrorCode::output_too_large) {
                    ++owner->stats.oversized_outputs;
                }
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
        return VideoEncoderError{
            VideoEncoderErrorCode::hardware_encoder_unavailable, 0};
    }
    {
        std::scoped_lock lock(impl_->mutex);
        impl_->stats.hardware_accelerated = result.hardware_accelerated;
        impl_->stats.implementation_name = result.implementation_name;
        impl_->active = true;
    }
    return std::nullopt;
}

std::optional<VideoEncoderError> MacH264HardwareEncoder::encode(
    const NativeVideoFrame& frame,
    bool force_keyframe) noexcept {
    {
        std::scoped_lock lock(impl_->mutex);
        if (!impl_->active || !frame ||
            frame.width != impl_->config.width ||
            frame.height != impl_->config.height) {
            ++impl_->stats.input_failures;
            return VideoEncoderError{VideoEncoderErrorCode::input_failed, 0};
        }
        ++impl_->stats.frames_submitted;
    }
    const auto error = impl_->adapter->encode(frame, force_keyframe);
    if (error) {
        std::scoped_lock lock(impl_->mutex);
        ++impl_->stats.input_failures;
    }
    return error;
}

void MacH264HardwareEncoder::stop() noexcept {
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

bool MacH264HardwareEncoder::running() const noexcept {
    std::scoped_lock lock(impl_->mutex);
    return impl_->active;
}

VideoEncoderStatistics MacH264HardwareEncoder::statistics() const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->stats;
}

} // namespace catro::platform::macos

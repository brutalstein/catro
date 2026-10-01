#include <catro/platform/macos/video_presenter.hpp>

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>

#include <mutex>

namespace catro::platform::macos {
namespace {

class SampleBufferPresenterAdapter final : public VideoPresenterNativeAdapter {
public:
    SampleBufferPresenterAdapter()
        : display_layer_([AVSampleBufferDisplayLayer layer]) {
        display_layer_.videoGravity = AVLayerVideoGravityResizeAspect;
        display_layer_.hidden = YES;
    }

    std::optional<VideoPresenterError> replace_surface(
        void* surface) noexcept override {
        if (![NSThread isMainThread]) {
            return VideoPresenterError{
                VideoPresenterErrorCode::wrong_thread, 0};
        }
        [display_layer_ removeFromSuperlayer];
        host_layer_ = (__bridge CALayer*)surface;
        if (host_layer_ != nil) {
            display_layer_.frame = host_layer_.bounds;
            display_layer_.autoresizingMask =
                kCALayerWidthSizable | kCALayerHeightSizable;
            [host_layer_ addSublayer:display_layer_];
        }
        return std::nullopt;
    }

    std::optional<VideoPresenterError> set_visible(
        bool visible) noexcept override {
        if (![NSThread isMainThread]) {
            return VideoPresenterError{
                VideoPresenterErrorCode::wrong_thread, 0};
        }
        display_layer_.hidden = !visible;
        if (!visible) {
            [display_layer_ flushAndRemoveImage];
        }
        return std::nullopt;
    }

    std::optional<VideoPresenterError> present(
        const NativeVideoFrame& frame) noexcept override {
        if (!frame) {
            return VideoPresenterError{
                VideoPresenterErrorCode::invalid_source, 0};
        }
        if (display_layer_.status == AVQueuedSampleBufferRenderingStatusFailed) {
            [display_layer_ flush];
        }
        if (!display_layer_.readyForMoreMediaData) {
            return VideoPresenterError{
                VideoPresenterErrorCode::present_failed, 0};
        }

        CVPixelBufferRef pixel =
            static_cast<CVPixelBufferRef>(frame.pixel_buffer);
        CMVideoFormatDescriptionRef format = nullptr;
        OSStatus status =
            CMVideoFormatDescriptionCreateForImageBuffer(
                kCFAllocatorDefault, pixel, &format);
        if (status != noErr || format == nullptr) {
            return VideoPresenterError{
                VideoPresenterErrorCode::present_failed, status};
        }
        CMSampleTimingInfo timing{
            .duration = kCMTimeInvalid,
            .presentationTimeStamp =
                CMTimeMake(frame.pts_100ns, 10'000'000),
            .decodeTimeStamp = kCMTimeInvalid,
        };
        CMSampleBufferRef sample = nullptr;
        status = CMSampleBufferCreateForImageBuffer(
            kCFAllocatorDefault,
            pixel,
            true,
            nullptr,
            nullptr,
            format,
            &timing,
            &sample);
        CFRelease(format);
        if (status != noErr || sample == nullptr) {
            return VideoPresenterError{
                VideoPresenterErrorCode::present_failed, status};
        }

        __strong AVSampleBufferDisplayLayer* layer = display_layer_;
        dispatch_async(dispatch_get_main_queue(), ^{
            if (!layer.hidden && layer.superlayer != nil) {
                [layer enqueueSampleBuffer:sample];
            }
            CFRelease(sample);
        });
        return std::nullopt;
    }

    void reset() noexcept override {
        auto detach = ^{
            [display_layer_ flushAndRemoveImage];
            [display_layer_ removeFromSuperlayer];
            display_layer_.hidden = YES;
            host_layer_ = nil;
        };
        if ([NSThread isMainThread]) {
            detach();
        } else {
            dispatch_sync(dispatch_get_main_queue(), detach);
        }
    }

    ~SampleBufferPresenterAdapter() override {
        reset();
    }

private:
    __strong AVSampleBufferDisplayLayer* display_layer_;
    __weak CALayer* host_layer_ = nil;
};

} // namespace

struct MacVideoPresenter::Impl {
    explicit Impl(std::unique_ptr<VideoPresenterNativeAdapter> value)
        : adapter(std::move(value)) {}

    std::unique_ptr<VideoPresenterNativeAdapter> adapter;
    mutable std::mutex mutex;
    VideoPresenterStatistics stats;
    void* surface = nullptr;
    bool visible = false;
};

std::unique_ptr<VideoPresenterNativeAdapter>
make_video_presenter_native_adapter() {
    return std::make_unique<SampleBufferPresenterAdapter>();
}

MacVideoPresenter::MacVideoPresenter()
    : MacVideoPresenter(make_video_presenter_native_adapter()) {}

MacVideoPresenter::MacVideoPresenter(
    std::unique_ptr<VideoPresenterNativeAdapter> adapter)
    : impl_(std::make_unique<Impl>(std::move(adapter))) {}

MacVideoPresenter::~MacVideoPresenter() {
    reset();
}

std::optional<VideoPresenterError> MacVideoPresenter::replace_surface(
    void* surface) noexcept {
    const auto error = impl_->adapter->replace_surface(surface);
    if (error) {
        return error;
    }
    std::scoped_lock lock(impl_->mutex);
    if (impl_->surface != nullptr && impl_->surface != surface) {
        ++impl_->stats.surface_replacements;
    }
    impl_->surface = surface;
    return std::nullopt;
}

std::optional<VideoPresenterError> MacVideoPresenter::set_visible(
    bool visible) noexcept {
    const auto error = impl_->adapter->set_visible(visible);
    if (error) {
        return error;
    }
    std::scoped_lock lock(impl_->mutex);
    impl_->visible = visible;
    return std::nullopt;
}

std::optional<VideoPresenterError> MacVideoPresenter::present(
    const NativeVideoFrame& frame) noexcept {
    {
        std::scoped_lock lock(impl_->mutex);
        if (!impl_->visible) {
            ++impl_->stats.frames_dropped;
            return VideoPresenterError{
                VideoPresenterErrorCode::not_visible, 0};
        }
        if (impl_->surface == nullptr) {
            ++impl_->stats.frames_dropped;
            return VideoPresenterError{
                VideoPresenterErrorCode::surface_unavailable, 0};
        }
        if (!frame) {
            ++impl_->stats.frames_dropped;
            return VideoPresenterError{
                VideoPresenterErrorCode::invalid_source, 0};
        }
    }

    const auto error = impl_->adapter->present(frame);
    std::scoped_lock lock(impl_->mutex);
    if (error) {
        ++impl_->stats.frames_dropped;
        return error;
    }
    if (impl_->stats.source_width != frame.width ||
        impl_->stats.source_height != frame.height) {
        ++impl_->stats.reconfigurations;
        impl_->stats.source_width = frame.width;
        impl_->stats.source_height = frame.height;
    }
    ++impl_->stats.frames_presented;
    return std::nullopt;
}

void MacVideoPresenter::reset() noexcept {
    impl_->adapter->reset();
    std::scoped_lock lock(impl_->mutex);
    impl_->surface = nullptr;
    impl_->visible = false;
}

bool MacVideoPresenter::visible() const noexcept {
    std::scoped_lock lock(impl_->mutex);
    return impl_->visible;
}

void* MacVideoPresenter::surface() const noexcept {
    std::scoped_lock lock(impl_->mutex);
    return impl_->surface;
}

VideoPresenterStatistics MacVideoPresenter::statistics() const noexcept {
    std::scoped_lock lock(impl_->mutex);
    return impl_->stats;
}

} // namespace catro::platform::macos

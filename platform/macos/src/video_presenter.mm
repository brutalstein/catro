#include <catro/platform/macos/video_presenter.hpp>

#import <QuartzCore/QuartzCore.h>

#include <mutex>
#include <utility>

namespace catro::platform::macos {

struct MacVideoPresenter::Impl {
    mutable std::mutex mutex;
    CALayer* layer = nil;
    // Keeps the shown surface out of its pool until the next frame replaces it.
    PixelBuffer shown;
    VideoPresenterStatistics stats;

    // Explicit transactions keep Core Animation correct off the main thread.
    void set_contents(id contents) {
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        layer.contents = contents;
        [CATransaction commit];
    }

    void detach() {
        if (layer == nil) {
            return;
        }
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        layer.contents = nil;
        [layer removeFromSuperlayer];
        [CATransaction commit];
        layer = nil;
    }
};

MacVideoPresenter::MacVideoPresenter() : impl_(std::make_unique<Impl>()) {}

MacVideoPresenter::~MacVideoPresenter() {
    std::scoped_lock lock(impl_->mutex);
    impl_->detach();
    impl_->shown.reset();
}

void MacVideoPresenter::set_visible(bool visible) noexcept {
    PixelBuffer released;
    std::scoped_lock lock(impl_->mutex);
    impl_->stats.visible = visible;
    if (visible && impl_->layer == nil) {
        impl_->layer = [CALayer layer];
        impl_->layer.contentsGravity = kCAGravityResizeAspect;
    } else if (!visible) {
        impl_->detach();
        released = std::move(impl_->shown);
    }
}

void* MacVideoPresenter::layer() const noexcept {
    std::scoped_lock lock(impl_->mutex);
    return (__bridge void*)impl_->layer;
}

std::optional<VideoPresenterError> MacVideoPresenter::present(const PixelBuffer& frame) noexcept {
    if (!frame) {
        return VideoPresenterError{VideoPresenterErrorCode::invalid_source};
    }
    IOSurfaceRef surface = CVPixelBufferGetIOSurface(frame.get());
    if (surface == nullptr) {
        return VideoPresenterError{VideoPresenterErrorCode::not_gpu_backed};
    }

    PixelBuffer previous;
    std::scoped_lock lock(impl_->mutex);
    auto& stats = impl_->stats;
    if (impl_->layer == nil) {
        stats.frames_dropped += 1;
        return std::nullopt;
    }
    if (frame.width() != stats.source_width || frame.height() != stats.source_height) {
        stats.reconfigurations += stats.source_width == 0 ? 0U : 1U;
        stats.source_width = frame.width();
        stats.source_height = frame.height();
    }
    impl_->set_contents((__bridge id)surface);
    previous = std::exchange(impl_->shown, PixelBuffer::retain(frame.get()));
    stats.frames_presented += 1;
    return std::nullopt;
}

VideoPresenterStatistics MacVideoPresenter::statistics() const noexcept {
    std::scoped_lock lock(impl_->mutex);
    return impl_->stats;
}

} // namespace catro::platform::macos

#include <catro/platform/macos/screen_capture.hpp>

#include <catro/video/geometry.hpp>

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <utility>

namespace catro::platform::macos {
namespace {

[[nodiscard]] std::string utf8(NSString* value) {
    if (value == nil || value.UTF8String == nullptr) {
        return {};
    }
    return std::string{value.UTF8String};
}

} // namespace

[[nodiscard]] ScreenCaptureError native_error(
    NSError* error,
    ScreenCaptureErrorCode fallback) noexcept {
    return ScreenCaptureError{
        fallback,
        error == nil ? 0 : static_cast<std::int64_t>(error.code)};
}

} // namespace catro::platform::macos

@interface CatroScreenStreamOutput : NSObject <SCStreamOutput, SCStreamDelegate>
- (instancetype)initWithFrameHandler:
        (catro::platform::macos::ScreenCaptureNativeAdapter::FrameHandler)frameHandler
    stopHandler:
        (catro::platform::macos::ScreenCaptureNativeAdapter::StopHandler)stopHandler;
@end

@implementation CatroScreenStreamOutput {
    catro::platform::macos::ScreenCaptureNativeAdapter::FrameHandler _frameHandler;
    catro::platform::macos::ScreenCaptureNativeAdapter::StopHandler _stopHandler;
    std::atomic_uint64_t _sequence;
}

- (instancetype)initWithFrameHandler:
        (catro::platform::macos::ScreenCaptureNativeAdapter::FrameHandler)frameHandler
    stopHandler:
        (catro::platform::macos::ScreenCaptureNativeAdapter::StopHandler)stopHandler {
    self = [super init];
    if (self != nil) {
        _frameHandler = std::move(frameHandler);
        _stopHandler = std::move(stopHandler);
        _sequence.store(0, std::memory_order_relaxed);
    }
    return self;
}

- (void)stream:(SCStream*)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
                  ofType:(SCStreamOutputType)type {
    (void)stream;
    if (type != SCStreamOutputTypeScreen ||
        sampleBuffer == nullptr ||
        !CMSampleBufferIsValid(sampleBuffer)) {
        return;
    }

    CVImageBufferRef image = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (image == nullptr) {
        return;
    }

    CVPixelBufferRef pixel = static_cast<CVPixelBufferRef>(image);
    CVPixelBufferRetain(pixel);
    std::shared_ptr<void> lease(
        pixel,
        [](void* value) {
            CVPixelBufferRelease(static_cast<CVPixelBufferRef>(value));
        });

    const auto pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
    const auto pts100ns = CMTIME_IS_NUMERIC(pts)
        ? static_cast<std::int64_t>(
              CMTimeConvertScale(pts, 10'000'000, kCMTimeRoundingMethod_Default).value)
        : 0;
    _frameHandler(catro::platform::macos::NativeVideoFrame{
        .lease = std::move(lease),
        .pixel_buffer = pixel,
        .sequence = _sequence.fetch_add(1, std::memory_order_relaxed) + 1,
        .width = static_cast<std::uint32_t>(CVPixelBufferGetWidth(pixel)),
        .height = static_cast<std::uint32_t>(CVPixelBufferGetHeight(pixel)),
        .pixel_format = CVPixelBufferGetPixelFormatType(pixel),
        .pts_100ns = pts100ns,
    });
}

- (void)stream:(SCStream*)stream didStopWithError:(NSError*)error {
    (void)stream;
    _stopHandler(catro::platform::macos::native_error(
        error,
        CGPreflightScreenCaptureAccess()
            ? catro::platform::macos::ScreenCaptureErrorCode::frame_failure
            : catro::platform::macos::ScreenCaptureErrorCode::permission_denied));
}
@end

namespace catro::platform::macos {
namespace {

class ScreenCaptureKitAdapter final : public ScreenCaptureNativeAdapter {
public:
    CaptureEnumerationResult enumerate_sources() noexcept override {
        CaptureEnumerationResult result;
        if ([NSThread isMainThread]) {
            result.error = ScreenCaptureError{
                ScreenCaptureErrorCode::wrong_thread, 0};
            return result;
        }
        if (!CGPreflightScreenCaptureAccess()) {
            result.error = ScreenCaptureError{
                ScreenCaptureErrorCode::permission_denied, 0};
            return result;
        }

        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        __block SCShareableContent* content = nil;
        __block NSError* failure = nil;
        [SCShareableContent
            getShareableContentExcludingDesktopWindows:YES
                                   onScreenWindowsOnly:YES
                                     completionHandler:^(
                                         SCShareableContent* value,
                                         NSError* error) {
                content = value;
                failure = error;
                dispatch_semaphore_signal(done);
            }];
        dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);

        if (failure != nil || content == nil) {
            result.error = native_error(
                failure,
                CGPreflightScreenCaptureAccess()
                    ? ScreenCaptureErrorCode::capture_creation_failed
                    : ScreenCaptureErrorCode::permission_denied);
            return result;
        }

        NSScreen* main = NSScreen.mainScreen;
        const auto main_id = static_cast<CGDirectDisplayID>(
            [main.deviceDescription[@"NSScreenNumber"] unsignedIntValue]);
        result.sources.reserve(content.displays.count + content.windows.count);
        for (SCDisplay* display in content.displays) {
            result.sources.push_back(CaptureSource{
                .kind = CaptureSourceKind::display,
                .native_id = display.displayID,
                .title = "Display " + std::to_string(display.displayID),
                .width = static_cast<std::uint32_t>(display.width),
                .height = static_cast<std::uint32_t>(display.height),
                .primary = display.displayID == main_id,
            });
        }
        for (SCWindow* window in content.windows) {
            if (window.windowID == 0 || window.frame.size.width < 2 ||
                window.frame.size.height < 2) {
                continue;
            }
            result.sources.push_back(CaptureSource{
                .kind = CaptureSourceKind::window,
                .native_id = window.windowID,
                .title = utf8(window.title),
                .application_name = utf8(window.owningApplication.applicationName),
                .width = static_cast<std::uint32_t>(window.frame.size.width),
                .height = static_cast<std::uint32_t>(window.frame.size.height),
            });
        }
        return result;
    }

    std::optional<ScreenCaptureError> start(
        const CaptureSource& source,
        const ScreenCaptureConfig& config,
        FrameHandler on_frame,
        StopHandler on_stop) noexcept override {
        stop();
        if ([NSThread isMainThread]) {
            return ScreenCaptureError{ScreenCaptureErrorCode::wrong_thread, 0};
        }
        const auto extent = video::fit_even_video_extent(
            source.width,
            source.height,
            config.max_width,
            config.max_height);
        if (!extent || config.frame_rate == 0 || source.native_id == 0) {
            return ScreenCaptureError{ScreenCaptureErrorCode::invalid_config, 0};
        }

        const auto enumeration = fetch_content();
        if (enumeration.error || enumeration.content == nil) {
            return enumeration.error.value_or(
                ScreenCaptureError{ScreenCaptureErrorCode::source_unavailable, 0});
        }

        SCContentFilter* filter = nil;
        if (source.kind == CaptureSourceKind::display) {
            for (SCDisplay* display in enumeration.content.displays) {
                if (display.displayID == source.native_id) {
                    filter = [[SCContentFilter alloc]
                        initWithDisplay:display
                      excludingWindows:@[]];
                    break;
                }
            }
        } else {
            for (SCWindow* window in enumeration.content.windows) {
                if (window.windowID == source.native_id) {
                    filter = [[SCContentFilter alloc]
                        initWithDesktopIndependentWindow:window];
                    break;
                }
            }
        }
        if (filter == nil) {
            return ScreenCaptureError{
                ScreenCaptureErrorCode::source_unavailable, 0};
        }

        SCStreamConfiguration* native_config = [SCStreamConfiguration new];
        native_config.width = extent->width;
        native_config.height = extent->height;
        native_config.minimumFrameInterval = CMTimeMake(1, config.frame_rate);
        native_config.queueDepth = 3;
        native_config.showsCursor = config.shows_cursor;
        native_config.pixelFormat =
            kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;

        output_ = [[CatroScreenStreamOutput alloc]
            initWithFrameHandler:std::move(on_frame)
                     stopHandler:std::move(on_stop)];
        queue_ = dispatch_queue_create(
            "com.catro.screen-capture", DISPATCH_QUEUE_SERIAL);
        stream_ = [[SCStream alloc]
            initWithFilter:filter
             configuration:native_config
                  delegate:output_];

        NSError* add_error = nil;
        if (![stream_ addStreamOutput:output_
                                 type:SCStreamOutputTypeScreen
                   sampleHandlerQueue:queue_
                                error:&add_error]) {
            stop();
            return native_error(
                add_error, ScreenCaptureErrorCode::capture_creation_failed);
        }

        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        __block NSError* start_error = nil;
        [stream_ startCaptureWithCompletionHandler:^(NSError* error) {
            start_error = error;
            dispatch_semaphore_signal(done);
        }];
        dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
        if (start_error != nil) {
            stop();
            return native_error(
                start_error,
                CGPreflightScreenCaptureAccess()
                    ? ScreenCaptureErrorCode::capture_creation_failed
                    : ScreenCaptureErrorCode::permission_denied);
        }
        return std::nullopt;
    }

    void stop() noexcept override {
        if (stream_ == nil) {
            output_ = nil;
            queue_ = nil;
            return;
        }
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        [stream_ stopCaptureWithCompletionHandler:^(NSError*) {
            dispatch_semaphore_signal(done);
        }];
        dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
        if (queue_ != nil) {
            dispatch_sync(queue_, ^{});
        }
        NSError* ignored = nil;
        [stream_ removeStreamOutput:output_
                              type:SCStreamOutputTypeScreen
                             error:&ignored];
        stream_ = nil;
        output_ = nil;
        queue_ = nil;
    }

    ~ScreenCaptureKitAdapter() override {
        stop();
    }

private:
    struct ContentResult {
        __strong SCShareableContent* content = nil;
        std::optional<ScreenCaptureError> error;
    };

    [[nodiscard]] ContentResult fetch_content() noexcept {
        ContentResult result;
        if (!CGPreflightScreenCaptureAccess()) {
            result.error = ScreenCaptureError{
                ScreenCaptureErrorCode::permission_denied, 0};
            return result;
        }
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        __block SCShareableContent* content = nil;
        __block NSError* failure = nil;
        [SCShareableContent
            getShareableContentExcludingDesktopWindows:YES
                                   onScreenWindowsOnly:YES
                                     completionHandler:^(
                                         SCShareableContent* value,
                                         NSError* error) {
                content = value;
                failure = error;
                dispatch_semaphore_signal(done);
            }];
        dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
        result.content = content;
        if (failure != nil || content == nil) {
            result.error = native_error(
                failure, ScreenCaptureErrorCode::source_unavailable);
        }
        return result;
    }

    __strong SCStream* stream_ = nil;
    __strong CatroScreenStreamOutput* output_ = nil;
    dispatch_queue_t queue_ = nil;
};

} // namespace

struct MacScreenCapture::Impl {
    explicit Impl(std::unique_ptr<ScreenCaptureNativeAdapter> value)
        : adapter(std::move(value)) {}

    std::unique_ptr<ScreenCaptureNativeAdapter> adapter;
    mutable std::mutex mutex;
    std::condition_variable ready;
    NativeVideoFrame latest;
    ScreenCaptureStatistics stats;
    std::atomic_uint64_t contention_drops{0};
    std::uint64_t generation = 0;

    void on_frame(std::uint64_t expected, NativeVideoFrame frame) noexcept {
        std::unique_lock lock(mutex, std::try_to_lock);
        if (!lock.owns_lock()) {
            contention_drops.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (generation != expected ||
            stats.state != ScreenCaptureState::running ||
            !frame) {
            return;
        }
        ++stats.frames_received;
        if (stats.width != 0 &&
            (stats.width != frame.width || stats.height != frame.height)) {
            ++stats.resize_events;
        }
        stats.width = frame.width;
        stats.height = frame.height;
        if (latest) {
            ++stats.mailbox_overwrites;
        }
        latest = std::move(frame);
        ++stats.frames_published;
        ready.notify_one();
    }

    void on_stop(std::uint64_t expected, ScreenCaptureError error) noexcept {
        std::scoped_lock lock(mutex);
        if (generation != expected ||
            stats.state != ScreenCaptureState::running) {
            return;
        }
        stats.error = error;
        stats.state = error.code == ScreenCaptureErrorCode::source_unavailable
            ? ScreenCaptureState::source_closed
            : ScreenCaptureState::failed;
        ready.notify_all();
    }
};

std::unique_ptr<ScreenCaptureNativeAdapter>
make_screen_capture_native_adapter() {
    return std::make_unique<ScreenCaptureKitAdapter>();
}

MacScreenCapture::MacScreenCapture()
    : MacScreenCapture(make_screen_capture_native_adapter()) {}

MacScreenCapture::MacScreenCapture(
    std::unique_ptr<ScreenCaptureNativeAdapter> adapter)
    : impl_(std::make_unique<Impl>(std::move(adapter))) {}

MacScreenCapture::~MacScreenCapture() {
    stop();
}

CaptureEnumerationResult MacScreenCapture::enumerate_sources() noexcept {
    return impl_->adapter->enumerate_sources();
}

std::optional<ScreenCaptureError> MacScreenCapture::start_source(
    const CaptureSource& source,
    const ScreenCaptureConfig& config) noexcept {
    stop();
    if (source.native_id == 0 || source.width < 2 || source.height < 2 ||
        config.frame_rate == 0 ||
        !video::fit_even_video_extent(
            source.width, source.height, config.max_width, config.max_height)) {
        return ScreenCaptureError{ScreenCaptureErrorCode::invalid_config, 0};
    }

    std::uint64_t current = 0;
    {
        std::scoped_lock lock(impl_->mutex);
        current = ++impl_->generation;
        impl_->stats = {};
        impl_->stats.state = ScreenCaptureState::running;
    }
    const auto error = impl_->adapter->start(
        source,
        config,
        [owner = impl_.get(), current](NativeVideoFrame frame) {
            owner->on_frame(current, std::move(frame));
        },
        [owner = impl_.get(), current](ScreenCaptureError failure) {
            owner->on_stop(current, failure);
        });
    if (error) {
        std::scoped_lock lock(impl_->mutex);
        ++impl_->generation;
        impl_->stats.state = ScreenCaptureState::failed;
        impl_->stats.error = error;
    }
    return error;
}

void MacScreenCapture::stop() noexcept {
    bool was_active = false;
    {
        std::scoped_lock lock(impl_->mutex);
        was_active = impl_->stats.state != ScreenCaptureState::idle;
        ++impl_->generation;
        impl_->stats.state = ScreenCaptureState::idle;
        impl_->stats.error.reset();
        impl_->latest = {};
        impl_->ready.notify_all();
    }
    if (was_active) {
        impl_->adapter->stop();
    }
}

bool MacScreenCapture::wait_for_latest(
    NativeVideoFrame& frame,
    std::chrono::milliseconds timeout) noexcept {
    std::unique_lock lock(impl_->mutex);
    impl_->ready.wait_for(lock, timeout, [this] {
        return static_cast<bool>(impl_->latest) ||
               impl_->stats.state != ScreenCaptureState::running;
    });
    if (!impl_->latest) {
        return false;
    }
    frame = std::move(impl_->latest);
    impl_->latest = {};
    return true;
}

ScreenCaptureStatistics MacScreenCapture::statistics() const noexcept {
    std::scoped_lock lock(impl_->mutex);
    auto result = impl_->stats;
    result.contention_drops =
        impl_->contention_drops.load(std::memory_order_relaxed);
    return result;
}

} // namespace catro::platform::macos

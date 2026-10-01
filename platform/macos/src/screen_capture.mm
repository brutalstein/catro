#include <catro/platform/macos/screen_capture.hpp>

#include <catro/video/geometry.hpp>

#import <CoreGraphics/CoreGraphics.h>
#import <CoreMedia/CoreMedia.h>
#import <Foundation/Foundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <dispatch/dispatch.h>

#include <cstdint>
#include <utility>

using catro::platform::macos::MacScreenCapture;

@interface CatroStreamOutput : NSObject <SCStreamOutput, SCStreamDelegate>
- (instancetype)initWithOwner:(MacScreenCapture::Impl*)owner;
- (void)detach;
@end

namespace catro::platform::macos {
namespace {

constexpr std::int64_t kScreenCaptureUserDeclined = -3801;

std::string utf8(NSString* value) {
    return value == nil ? std::string{} : std::string{value.UTF8String};
}

// Blocks the calling thread until ScreenCaptureKit answers. Completion handlers run on an
// internal queue, so this is safe off the main thread only.
SCShareableContent* fetch_content(std::chrono::milliseconds timeout, std::optional<ScreenCaptureError>& error) {
    __block SCShareableContent* result = nil;
    __block NSError* failure = nil;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    [SCShareableContent getShareableContentExcludingDesktopWindows:YES
                                               onScreenWindowsOnly:YES
                                                 completionHandler:^(SCShareableContent* content, NSError* fault) {
                                                   result = content;
                                                   failure = fault;
                                                   dispatch_semaphore_signal(done);
                                                 }];
    const auto wait = dispatch_time(DISPATCH_TIME_NOW, static_cast<std::int64_t>(timeout.count()) *
                                                           static_cast<std::int64_t>(NSEC_PER_MSEC));
    if (dispatch_semaphore_wait(done, wait) != 0) {
        error = ScreenCaptureError{ScreenCaptureErrorCode::enumeration_failed, 0};
        return nil;
    }
    if (failure != nil || result == nil) {
        const auto code = failure == nil ? std::int64_t{0} : static_cast<std::int64_t>(failure.code);
        error = ScreenCaptureError{code == kScreenCaptureUserDeclined ? ScreenCaptureErrorCode::permission_denied
                                                                      : ScreenCaptureErrorCode::enumeration_failed,
                                   code};
        return nil;
    }
    return result;
}

std::uint32_t display_pixels(CGDirectDisplayID display, bool width) {
    CGDisplayModeRef mode = CGDisplayCopyDisplayMode(display);
    if (mode == nullptr) {
        return static_cast<std::uint32_t>(width ? CGDisplayPixelsWide(display) : CGDisplayPixelsHigh(display));
    }
    const auto pixels = width ? CGDisplayModeGetPixelWidth(mode) : CGDisplayModeGetPixelHeight(mode);
    CGDisplayModeRelease(mode);
    return static_cast<std::uint32_t>(pixels);
}

} // namespace

bool screen_capture_access_granted() noexcept {
    return CGPreflightScreenCaptureAccess();
}

bool request_screen_capture_access() noexcept {
    return CGRequestScreenCaptureAccess();
}

std::optional<ScreenCaptureError> validate(const ScreenCaptureConfig& config) noexcept {
    if (config.max_width < 16 || config.max_height < 16 || config.max_width > 8192 || config.max_height > 8192 ||
        config.frame_rate == 0 || config.frame_rate > 240) {
        return ScreenCaptureError{ScreenCaptureErrorCode::invalid_config, 0};
    }
    return std::nullopt;
}

CaptureSourcesResult enumerate_capture_sources(std::chrono::milliseconds timeout) noexcept {
    if (!screen_capture_access_granted()) {
        return ScreenCaptureError{ScreenCaptureErrorCode::permission_denied, 0};
    }
    @autoreleasepool {
        std::optional<ScreenCaptureError> error;
        SCShareableContent* content = fetch_content(timeout, error);
        if (content == nil) {
            return *error;
        }
        try {
            std::vector<CaptureSource> sources;
            const auto main_display = CGMainDisplayID();
            for (SCDisplay* display in content.displays) {
                CaptureSource source;
                source.kind = CaptureSourceKind::display;
                source.display_id = display.displayID;
                source.width = display_pixels(display.displayID, true);
                source.height = display_pixels(display.displayID, false);
                source.primary = display.displayID == main_display;
                source.title = source.primary ? "Main display" : "Display";
                sources.push_back(std::move(source));
            }
            const auto own_pid = static_cast<std::int32_t>(NSProcessInfo.processInfo.processIdentifier);
            for (SCWindow* window in content.windows) {
                if (window.windowLayer != 0 || window.title.length == 0 || window.frame.size.width < 64 ||
                    window.frame.size.height < 64 || window.owningApplication.processID == own_pid) {
                    continue;
                }
                CaptureSource source;
                source.kind = CaptureSourceKind::window;
                source.window_id = window.windowID;
                source.title = utf8(window.title);
                source.application_name = utf8(window.owningApplication.applicationName);
                source.process_id = window.owningApplication.processID;
                source.width = static_cast<std::uint32_t>(window.frame.size.width);
                source.height = static_cast<std::uint32_t>(window.frame.size.height);
                sources.push_back(std::move(source));
            }
            return sources;
        } catch (...) {
            return ScreenCaptureError{ScreenCaptureErrorCode::enumeration_failed, 0};
        }
    }
}

void LatestFrameMailbox::publish(CaptureFrame frame) noexcept {
    std::optional<CaptureFrame> replaced;
    {
        std::unique_lock lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock()) {
            contention_drops_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (closed_) {
            return;
        }
        if (slot_.has_value()) {
            overwrites_.fetch_add(1, std::memory_order_relaxed);
            replaced = std::move(slot_);
        }
        slot_ = std::move(frame);
        published_.fetch_add(1, std::memory_order_relaxed);
    }
    ready_.notify_one();
    // `replaced` releases its surface here, outside the lock.
}

bool LatestFrameMailbox::wait_for_latest(CaptureFrame& frame, std::chrono::milliseconds timeout) noexcept {
    std::unique_lock lock(mutex_);
    if (!ready_.wait_for(lock, timeout, [&] { return closed_ || slot_.has_value(); }) || closed_) {
        return false;
    }
    frame = std::move(*slot_);
    slot_.reset();
    return true;
}

void LatestFrameMailbox::close() noexcept {
    std::optional<CaptureFrame> dropped;
    {
        std::scoped_lock lock(mutex_);
        closed_ = true;
        dropped = std::move(slot_);
        slot_.reset();
    }
    ready_.notify_all();
}

void LatestFrameMailbox::reopen() noexcept {
    std::scoped_lock lock(mutex_);
    closed_ = false;
}

struct MacScreenCapture::Impl {
    LatestFrameMailbox mailbox;
    SCStream* stream = nil;
    CatroStreamOutput* output = nil;
    dispatch_queue_t queue = nil;

    mutable std::mutex state_mutex;
    ScreenCaptureState state = ScreenCaptureState::idle;
    std::optional<ScreenCaptureError> error;
    std::atomic<std::uint64_t> frames_received{0};
    std::atomic<std::uint64_t> resize_events{0};
    std::atomic<std::uint32_t> width{0};
    std::atomic<std::uint32_t> height{0};
    std::uint64_t sequence = 0;

    void on_frame(CMSampleBufferRef sample) noexcept {
        if (sample == nullptr || !CMSampleBufferIsValid(sample)) {
            return;
        }
        CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sample, false);
        if (attachments == nullptr || CFArrayGetCount(attachments) == 0) {
            return;
        }
        auto* info = (__bridge NSDictionary*)CFArrayGetValueAtIndex(attachments, 0);
        NSNumber* status = info[SCStreamFrameInfoStatus];
        if (status == nil || status.integerValue != SCFrameStatusComplete) {
            return;
        }
        CVImageBufferRef image = CMSampleBufferGetImageBuffer(sample);
        if (image == nullptr) {
            return;
        }
        frames_received.fetch_add(1, std::memory_order_relaxed);

        const auto frame_width = static_cast<std::uint32_t>(CVPixelBufferGetWidth(image));
        const auto frame_height = static_cast<std::uint32_t>(CVPixelBufferGetHeight(image));
        const bool width_changed = width.exchange(frame_width, std::memory_order_relaxed) != frame_width;
        const bool height_changed = height.exchange(frame_height, std::memory_order_relaxed) != frame_height;
        if (width_changed || height_changed) {
            resize_events.fetch_add(1, std::memory_order_relaxed);
        }

        const auto pts = CMSampleBufferGetPresentationTimeStamp(sample);
        CaptureFrame frame;
        frame.buffer = PixelBuffer::retain(image);
        frame.sequence = ++sequence;
        frame.pts_100ns =
            CMTIME_IS_VALID(pts) ? CMTimeConvertScale(pts, 10'000'000, kCMTimeRoundingMethod_Default).value : 0;
        mailbox.publish(std::move(frame));
    }

    void on_stopped(NSError* failure) noexcept {
        const auto code = failure == nil ? std::int64_t{0} : static_cast<std::int64_t>(failure.code);
        {
            std::scoped_lock lock(state_mutex);
            if (code == kScreenCaptureUserDeclined) {
                state = ScreenCaptureState::failed;
                error = ScreenCaptureError{ScreenCaptureErrorCode::permission_denied, code};
            } else {
                state = ScreenCaptureState::source_closed;
                error = ScreenCaptureError{ScreenCaptureErrorCode::frame_failure, code};
            }
        }
        mailbox.close();
    }

    void set_state(ScreenCaptureState value, std::optional<ScreenCaptureError> failure) {
        std::scoped_lock lock(state_mutex);
        state = value;
        error = failure;
    }
};

} // namespace catro::platform::macos

@implementation CatroStreamOutput {
    std::atomic<MacScreenCapture::Impl*> _owner;
}

- (instancetype)initWithOwner:(MacScreenCapture::Impl*)owner {
    self = [super init];
    if (self != nil) {
        _owner.store(owner);
    }
    return self;
}

- (void)detach {
    _owner.store(nullptr);
}

- (void)stream:(SCStream*)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type {
    (void)stream;
    if (type != SCStreamOutputTypeScreen) {
        return;
    }
    if (auto* owner = _owner.load()) {
        owner->on_frame(sampleBuffer);
    }
}

- (void)stream:(SCStream*)stream didStopWithError:(NSError*)error {
    (void)stream;
    if (auto* owner = _owner.load()) {
        owner->on_stopped(error);
    }
}

@end

namespace catro::platform::macos {

MacScreenCapture::MacScreenCapture() : impl_(std::make_unique<Impl>()) {}

MacScreenCapture::~MacScreenCapture() {
    stop();
}

std::optional<ScreenCaptureError> MacScreenCapture::start(const CaptureSource& source,
                                                          const ScreenCaptureConfig& config) {
    stop();
    if (auto invalid = validate(config)) {
        return invalid;
    }
    if (!screen_capture_access_granted()) {
        const ScreenCaptureError denied{ScreenCaptureErrorCode::permission_denied, 0};
        impl_->set_state(ScreenCaptureState::failed, denied);
        return denied;
    }

    std::optional<ScreenCaptureError> failure;
    @autoreleasepool {
        SCShareableContent* content = fetch_content(std::chrono::seconds(3), failure);
        SCContentFilter* filter = nil;
        std::uint32_t source_width = 0;
        std::uint32_t source_height = 0;
        if (source.kind == CaptureSourceKind::display) {
            for (SCDisplay* display in content.displays) {
                if (display.displayID == source.display_id) {
                    filter = [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];
                    source_width = display_pixels(display.displayID, true);
                    source_height = display_pixels(display.displayID, false);
                    break;
                }
            }
        } else {
            for (SCWindow* window in content.windows) {
                if (window.windowID == source.window_id) {
                    filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:window];
                    source_width = static_cast<std::uint32_t>(window.frame.size.width);
                    source_height = static_cast<std::uint32_t>(window.frame.size.height);
                    break;
                }
            }
        }
        const auto extent =
            video::fit_even_video_extent(source_width, source_height, config.max_width, config.max_height);
        if (content != nil && (filter == nil || !extent)) {
            failure = ScreenCaptureError{ScreenCaptureErrorCode::source_unavailable, 0};
        }

        if (!failure) {
            SCStreamConfiguration* settings = [[SCStreamConfiguration alloc] init];
            settings.width = extent->width;
            settings.height = extent->height;
            settings.minimumFrameInterval = CMTimeMake(1, static_cast<std::int32_t>(config.frame_rate));
            settings.pixelFormat = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
            settings.colorMatrix = kCGDisplayStreamYCbCrMatrix_ITU_R_709_2;
            settings.queueDepth = 3;
            settings.showsCursor = config.show_cursor ? YES : NO;

            impl_->mailbox.reopen();
            impl_->frames_received.store(0, std::memory_order_relaxed);
            impl_->resize_events.store(0, std::memory_order_relaxed);
            impl_->width.store(0, std::memory_order_relaxed);
            impl_->height.store(0, std::memory_order_relaxed);
            impl_->queue = dispatch_queue_create("catro.screen-capture", DISPATCH_QUEUE_SERIAL);
            impl_->output = [[CatroStreamOutput alloc] initWithOwner:impl_.get()];
            impl_->stream = [[SCStream alloc] initWithFilter:filter configuration:settings delegate:impl_->output];

            NSError* add_error = nil;
            if (![impl_->stream addStreamOutput:impl_->output
                                           type:SCStreamOutputTypeScreen
                             sampleHandlerQueue:impl_->queue
                                          error:&add_error]) {
                failure = ScreenCaptureError{ScreenCaptureErrorCode::capture_creation_failed,
                                             add_error == nil ? 0 : static_cast<std::int64_t>(add_error.code)};
            } else {
                __block NSError* start_error = nil;
                dispatch_semaphore_t started = dispatch_semaphore_create(0);
                [impl_->stream startCaptureWithCompletionHandler:^(NSError* fault) {
                  start_error = fault;
                  dispatch_semaphore_signal(started);
                }];
                const auto wait =
                    dispatch_time(DISPATCH_TIME_NOW, 5 * static_cast<std::int64_t>(NSEC_PER_SEC));
                if (dispatch_semaphore_wait(started, wait) != 0 || start_error != nil) {
                    const auto code =
                        start_error == nil ? std::int64_t{0} : static_cast<std::int64_t>(start_error.code);
                    failure = ScreenCaptureError{code == kScreenCaptureUserDeclined
                                                     ? ScreenCaptureErrorCode::permission_denied
                                                     : ScreenCaptureErrorCode::capture_creation_failed,
                                                 code};
                }
            }
        }
    }

    if (failure) {
        stop();
        impl_->set_state(ScreenCaptureState::failed, failure);
        return failure;
    }
    impl_->set_state(ScreenCaptureState::running, std::nullopt);
    return std::nullopt;
}

void MacScreenCapture::stop() noexcept {
    if (impl_->stream != nil) {
        @autoreleasepool {
            dispatch_semaphore_t stopped = dispatch_semaphore_create(0);
            [impl_->stream stopCaptureWithCompletionHandler:^(NSError*) {
              dispatch_semaphore_signal(stopped);
            }];
            (void)dispatch_semaphore_wait(
                stopped, dispatch_time(DISPATCH_TIME_NOW, 2 * static_cast<std::int64_t>(NSEC_PER_SEC)));
            [impl_->stream removeStreamOutput:impl_->output type:SCStreamOutputTypeScreen error:nil];
            [impl_->output detach];
            // Drain any callback that loaded the owner before detach; none can start afterwards.
            dispatch_sync(impl_->queue, ^{
                          });
            impl_->stream = nil;
            impl_->output = nil;
            impl_->queue = nil;
        }
    }
    impl_->mailbox.close();
    std::scoped_lock lock(impl_->state_mutex);
    if (impl_->state == ScreenCaptureState::running) {
        impl_->state = ScreenCaptureState::idle;
    }
}

bool MacScreenCapture::wait_for_latest(CaptureFrame& frame, std::chrono::milliseconds timeout) noexcept {
    return impl_->mailbox.wait_for_latest(frame, timeout);
}

ScreenCaptureStatistics MacScreenCapture::statistics() const noexcept {
    ScreenCaptureStatistics result;
    {
        std::scoped_lock lock(impl_->state_mutex);
        result.state = impl_->state;
        result.error = impl_->error;
    }
    result.frames_received = impl_->frames_received.load(std::memory_order_relaxed);
    result.frames_published = impl_->mailbox.published();
    result.mailbox_overwrites = impl_->mailbox.overwrites();
    result.contention_drops = impl_->mailbox.contention_drops();
    result.resize_events = impl_->resize_events.load(std::memory_order_relaxed);
    result.width = impl_->width.load(std::memory_order_relaxed);
    result.height = impl_->height.load(std::memory_order_relaxed);
    return result;
}

} // namespace catro::platform::macos

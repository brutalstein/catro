#include <catro/platform/macos/screen_capture.hpp>

#include <catro/video/geometry.hpp>

#import <AVFoundation/AVFoundation.h>
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cmath>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace catro::platform::macos {
namespace {

[[nodiscard]] std::string utf8(NSString* value) {
    if (value == nil || value.UTF8String == nullptr) {
        return {};
    }
    return std::string{value.UTF8String};
}

[[nodiscard]] constexpr video::VideoExtent oriented_pixels(video::VideoExtent pixels, int rotation) {
    return rotation % 180 == 90 || rotation % 180 == -90
        ? video::VideoExtent{pixels.height, pixels.width} : pixels;
}

static_assert(oriented_pixels({1920, 1080}, 0) == video::VideoExtent{1920, 1080});
static_assert(oriented_pixels({1920, 1080}, 90) == video::VideoExtent{1080, 1920});
static_assert(oriented_pixels({3840, 2160}, 180) == video::VideoExtent{3840, 2160});
static_assert(oriented_pixels({3840, 2160}, 270) == video::VideoExtent{2160, 3840});

[[nodiscard]] video::VideoExtent display_pixels(SCDisplay* display) {
    CGDisplayModeRef mode = CGDisplayCopyDisplayMode(display.displayID);
    if (mode == nullptr) {
        return {static_cast<std::uint32_t>(display.width),
                static_cast<std::uint32_t>(display.height)};
    }
    const video::VideoExtent pixels{
        static_cast<std::uint32_t>(CGDisplayModeGetPixelWidth(mode)),
        static_cast<std::uint32_t>(CGDisplayModeGetPixelHeight(mode))};
    CGDisplayModeRelease(mode);
    // Display mode dimensions are unrotated; ScreenCaptureKit dimensions are oriented.
    return oriented_pixels(pixels, static_cast<int>(std::lround(CGDisplayRotation(display.displayID))));
}

[[nodiscard]] SCDisplay* window_display(CGRect frame, NSArray<SCDisplay*>* displays) {
    SCDisplay* selected = displays.firstObject;
    CGFloat largest_area = 0;
    for (SCDisplay* display in displays) {
        const CGRect intersection = CGRectIntersection(frame, display.frame);
        if (CGRectIsNull(intersection)) {
            continue;
        }
        const CGFloat area = intersection.size.width * intersection.size.height;
        if (area > largest_area) {
            largest_area = area;
            selected = display;
        }
    }
    return selected;
}

[[nodiscard]] video::VideoExtent window_pixels(CGRect frame, SCDisplay* display) {
    // ScreenCaptureKit descriptors use points, but its output configuration uses pixels. Use
    // the owning display's backing resolution so Retina windows aren't capped at half size.
    const auto pixels = display != nil ? display_pixels(display) : video::VideoExtent{};
    const double scale_x = display != nil && display.width > 0
        ? static_cast<double>(pixels.width) / display.width : 1.0;
    const double scale_y = display != nil && display.height > 0
        ? static_cast<double>(pixels.height) / display.height : 1.0;
    return {static_cast<std::uint32_t>(std::lround(frame.size.width * scale_x)),
            static_cast<std::uint32_t>(std::lround(frame.size.height * scale_y))};
}

// Native game detection, like Discord's: the app's own category, or a Steam library install.
[[nodiscard]] bool is_game(SCRunningApplication* app) {
    if (app == nil) {
        return false;
    }
    NSURL* url = [NSRunningApplication runningApplicationWithProcessIdentifier:app.processID].bundleURL;
    if (url == nil) {
        return false;
    }
    if ([url.path containsString:@"/steamapps/common/"]) {
        return true;
    }
    id category = [NSBundle bundleWithURL:url].infoDictionary[@"LSApplicationCategoryType"];
    return [category isKindOfClass:NSString.class] && [(NSString*)category containsString:@"games"];
}

// Built-in, USB and Continuity cameras in system order.
[[nodiscard]] NSArray<AVCaptureDevice*>* cameras() {
    NSMutableArray<AVCaptureDeviceType>* types =
        [NSMutableArray arrayWithObject:AVCaptureDeviceTypeBuiltInWideAngleCamera];
    if (@available(macOS 14.0, *)) {
        [types addObject:AVCaptureDeviceTypeExternal];
        // Needs NSCameraUseContinuityCameraDeviceType in Info.plist.
        [types addObject:AVCaptureDeviceTypeContinuityCamera];
    } else {
        // The macOS 13 name; deprecated only from 14, where the branch above runs instead.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [types addObject:AVCaptureDeviceTypeExternalUnknown];
#pragma clang diagnostic pop
    }
    return [AVCaptureDeviceDiscoverySession
               discoverySessionWithDeviceTypes:types
                                     mediaType:AVMediaTypeVideo
                                      position:AVCaptureDevicePositionUnspecified]
        .devices;
}

[[nodiscard]] std::uint64_t camera_id(AVCaptureDevice* device) {
    const std::uint64_t hash = std::hash<std::string>{}(utf8(device.uniqueID));
    return hash != 0 ? hash : 1;
}

struct CameraMode {
    __strong AVCaptureDeviceFormat* format = nil;
    double fps = 0;
    std::uint64_t pixels = 0;
};

[[nodiscard]] std::optional<CameraMode> camera_mode(
    AVCaptureDevice* device,
    const ScreenCaptureConfig& config) {
    std::optional<CameraMode> best;
    for (AVCaptureDeviceFormat* format in device.formats) {
        const auto dimensions =
            CMVideoFormatDescriptionGetDimensions(format.formatDescription);
        if (dimensions.width <= 0 || dimensions.height <= 0 ||
            static_cast<std::uint32_t>(dimensions.width) > config.max_width ||
            static_cast<std::uint32_t>(dimensions.height) > config.max_height) {
            continue;
        }
        double supported_fps = 0;
        for (AVFrameRateRange* range in format.videoSupportedFrameRateRanges) {
            const double target = std::min<double>(config.frame_rate, range.maxFrameRate);
            if (target + 0.001 >= range.minFrameRate) {
                supported_fps = std::max(supported_fps, target);
            }
        }
        if (supported_fps <= 0) {
            continue;
        }
        const auto pixels = static_cast<std::uint64_t>(dimensions.width) *
                            static_cast<std::uint64_t>(dimensions.height);
        if (!best || std::tie(pixels, supported_fps) >
                         std::tie(best->pixels, best->fps)) {
            best = CameraMode{format, supported_fps, pixels};
        }
    }
    return best;
}

[[nodiscard]] std::vector<CaptureSource> camera_sources() {
    std::vector<CaptureSource> sources;
    for (AVCaptureDevice* device in cameras()) {
        const auto size = CMVideoFormatDescriptionGetDimensions(device.activeFormat.formatDescription);
        if (size.width <= 0 || size.height <= 0) {
            continue;
        }
        sources.push_back(CaptureSource{
            .kind = CaptureSourceKind::camera,
            .native_id = camera_id(device),
            .title = utf8(device.localizedName),
            .width = static_cast<std::uint32_t>(size.width),
            .height = static_cast<std::uint32_t>(size.height),
        });
    }
    return sources;
}

// Off the main thread only: the first use shows the system camera prompt and waits for it.
[[nodiscard]] bool camera_access() {
    switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo]) {
    case AVAuthorizationStatusAuthorized:
        return true;
    case AVAuthorizationStatusNotDetermined: {
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        __block BOOL granted = NO;
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                                 completionHandler:^(BOOL value) {
                                     granted = value;
                                     dispatch_semaphore_signal(done);
                                 }];
        dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
        return granted;
    }
    default:
        return false;
    }
}

} // namespace

[[nodiscard]] ScreenCaptureError native_error(
    NSError* error,
    ScreenCaptureErrorCode fallback) noexcept {
    return ScreenCaptureError{
        fallback,
        error == nil ? 0 : static_cast<std::int64_t>(error.code)};
}

bool interleave_stereo(
    std::span<const std::span<const float>> buffers,
    std::uint32_t channels_per_buffer,
    std::vector<float>& out) {
    if (buffers.empty() || channels_per_buffer == 0) {
        return false;
    }
    if (buffers.size() == 1) {
        const auto samples = buffers.front();
        if (samples.empty() || samples.size() % channels_per_buffer != 0) {
            return false;
        }
        const auto frames = samples.size() / channels_per_buffer;
        const auto right = channels_per_buffer > 1 ? 1U : 0U;
        out.resize(frames * 2);
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const auto* in = samples.data() + frame * channels_per_buffer;
            out[frame * 2] = in[0];
            out[frame * 2 + 1] = in[right];
        }
        return true;
    }
    const auto left = buffers[0];
    const auto right = buffers[1];
    if (channels_per_buffer != 1 || left.empty() || left.size() != right.size()) {
        return false;
    }
    out.resize(left.size() * 2);
    for (std::size_t frame = 0; frame < left.size(); ++frame) {
        out[frame * 2] = left[frame];
        out[frame * 2 + 1] = right[frame];
    }
    return true;
}

} // namespace catro::platform::macos

// Receives ScreenCaptureKit frames and audio, and camera frames from AVCaptureVideoDataOutput.
@interface CatroScreenStreamOutput
    : NSObject <SCStreamOutput, SCStreamDelegate, AVCaptureVideoDataOutputSampleBufferDelegate>
- (instancetype)initWithFrameHandler:
        (catro::platform::macos::ScreenCaptureNativeAdapter::FrameHandler)frameHandler
    audioHandler:
        (catro::platform::macos::ScreenCaptureNativeAdapter::AudioHandler)audioHandler
    stopHandler:
        (catro::platform::macos::ScreenCaptureNativeAdapter::StopHandler)stopHandler;
@end

@implementation CatroScreenStreamOutput {
    catro::platform::macos::ScreenCaptureNativeAdapter::FrameHandler _frameHandler;
    catro::platform::macos::ScreenCaptureNativeAdapter::AudioHandler _audioHandler;
    catro::platform::macos::ScreenCaptureNativeAdapter::StopHandler _stopHandler;
    std::atomic_uint64_t _sequence;
    // Reused on the serial audio queue so steady-state audio never allocates.
    std::vector<std::uint64_t> _audioList;
    std::vector<float> _audioScratch;
}

- (instancetype)initWithFrameHandler:
        (catro::platform::macos::ScreenCaptureNativeAdapter::FrameHandler)frameHandler
    audioHandler:
        (catro::platform::macos::ScreenCaptureNativeAdapter::AudioHandler)audioHandler
    stopHandler:
        (catro::platform::macos::ScreenCaptureNativeAdapter::StopHandler)stopHandler {
    self = [super init];
    if (self != nil) {
        _frameHandler = std::move(frameHandler);
        _audioHandler = std::move(audioHandler);
        _stopHandler = std::move(stopHandler);
        _sequence.store(0, std::memory_order_relaxed);
    }
    return self;
}

- (void)handleAudio:(CMSampleBufferRef)sampleBuffer {
    if (!_audioHandler) {
        return;
    }
    CMFormatDescriptionRef format = CMSampleBufferGetFormatDescription(sampleBuffer);
    const AudioStreamBasicDescription* description =
        format == nullptr ? nullptr : CMAudioFormatDescriptionGetStreamBasicDescription(format);
    if (description == nullptr || description->mFormatID != kAudioFormatLinearPCM ||
        (description->mFormatFlags & kAudioFormatFlagIsFloat) == 0 ||
        description->mBitsPerChannel != 32 ||
        description->mSampleRate != catro::platform::macos::kCaptureAudioSampleRate) {
        return;
    }

    std::size_t list_bytes = 0;
    if (CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(
            sampleBuffer, &list_bytes, nullptr, 0, nullptr, nullptr, 0, nullptr) != noErr ||
        list_bytes == 0) {
        return;
    }
    _audioList.resize((list_bytes + sizeof(std::uint64_t) - 1) / sizeof(std::uint64_t));
    auto* list = reinterpret_cast<AudioBufferList*>(_audioList.data());
    CMBlockBufferRef block = nullptr;
    if (CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(
            sampleBuffer,
            nullptr,
            list,
            list_bytes,
            nullptr,
            nullptr,
            kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment,
            &block) != noErr) {
        return;
    }

    const AudioBuffer* native = list->mBuffers;
    const auto count = std::min<UInt32>(list->mNumberBuffers, 2);
    std::array<std::span<const float>, 2> buffers{};
    for (UInt32 index = 0; index < count; ++index) {
        if (native[index].mData != nullptr) {
            buffers[index] = std::span<const float>{
                static_cast<const float*>(native[index].mData),
                native[index].mDataByteSize / sizeof(float)};
        }
    }
    if (count != 0 &&
        catro::platform::macos::interleave_stereo(
            std::span<const std::span<const float>>{buffers.data(), count},
            native[0].mNumberChannels,
            _audioScratch)) {
        _audioHandler(std::span<const float>{_audioScratch});
    }
    if (block != nullptr) {
        CFRelease(block);
    }
}

- (void)stream:(SCStream*)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
                  ofType:(SCStreamOutputType)type {
    (void)stream;
    if (sampleBuffer == nullptr || !CMSampleBufferIsValid(sampleBuffer)) {
        return;
    }
    if (@available(macOS 13.0, *)) {
        if (type == SCStreamOutputTypeAudio) {
            [self handleAudio:sampleBuffer];
            return;
        }
    }
    if (type == SCStreamOutputTypeScreen) {
        [self handleVideo:sampleBuffer];
    }
}

- (void)captureOutput:(AVCaptureOutput*)output
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
           fromConnection:(AVCaptureConnection*)connection {
    (void)output;
    (void)connection;
    if (sampleBuffer != nullptr && CMSampleBufferIsValid(sampleBuffer)) {
        [self handleVideo:sampleBuffer];
    }
}

- (void)handleVideo:(CMSampleBufferRef)sampleBuffer {
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
        auto camera_list = camera_sources();
        if (!CGPreflightScreenCaptureAccess()) {
            result.sources = std::move(camera_list);
            result.error = ScreenCaptureError{
                ScreenCaptureErrorCode::permission_denied, 0};
            return result;
        }

        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        __block SCShareableContent* content = nil;
        __block NSError* failure = nil;
        [SCShareableContent
            getShareableContentExcludingDesktopWindows:YES
                                   onScreenWindowsOnly:NO
                                     completionHandler:^(
                                         SCShareableContent* value,
                                         NSError* error) {
                content = value;
                failure = error;
                dispatch_semaphore_signal(done);
            }];
        dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);

        if (failure != nil || content == nil) {
            result.sources = std::move(camera_list);
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
            const auto pixels = display_pixels(display);
            result.sources.push_back(CaptureSource{
                .kind = CaptureSourceKind::display,
                .native_id = display.displayID,
                .title = "Display " + std::to_string(display.displayID),
                .width = pixels.width,
                .height = pixels.height,
                .display_id = display.displayID,
                .primary = display.displayID == main_id,
            });
        }
        std::unordered_map<pid_t, bool> games;
        for (SCWindow* window in content.windows) {
            if (window.windowID == 0 || window.frame.size.width < 2 ||
                window.frame.size.height < 2) {
                continue;
            }
            SCRunningApplication* app = window.owningApplication;
            const pid_t pid = app != nil ? app.processID : 0;
            auto found = games.find(pid);
            if (found == games.end()) {
                found = games.emplace(pid, is_game(app)).first;
            }
            // A full-screen game lives in its own Space, so it is off screen while Catro is open,
            // and a borderless one may sit above the normal window layer. Other apps list only
            // their visible normal windows, not menus, panels or status items.
            if ((!window.onScreen || window.windowLayer != 0) && !found->second) {
                continue;
            }
            SCDisplay* display = window_display(window.frame, content.displays);
            const auto pixels = window_pixels(window.frame, display);
            result.sources.push_back(CaptureSource{
                .kind = CaptureSourceKind::window,
                .native_id = window.windowID,
                .title = utf8(window.title),
                .application_name = utf8(app.applicationName),
                .width = pixels.width,
                .height = pixels.height,
                .display_id = display != nil ? display.displayID : 0,
                .game = found->second,
            });
        }
        result.sources.insert(result.sources.end(), camera_list.begin(), camera_list.end());
        return result;
    }

    std::optional<ScreenCaptureError> start(
        const CaptureSource& source,
        const ScreenCaptureConfig& config,
        FrameHandler on_frame,
        AudioHandler on_audio,
        StopHandler on_stop) noexcept override {
        stop();
        // ScreenCaptureKit captures audio from macOS 13; Monterey shares the picture only.
        bool captures_audio = false;
        if (@available(macOS 13.0, *)) {
            captures_audio = static_cast<bool>(on_audio);
        }
        if ([NSThread isMainThread]) {
            return ScreenCaptureError{ScreenCaptureErrorCode::wrong_thread, 0};
        }
        const auto extent = video::fit_encodable_video_extent(
            source.width,
            source.height,
            config.max_width,
            config.max_height);
        if (!extent || config.frame_rate == 0 || source.native_id == 0) {
            return ScreenCaptureError{ScreenCaptureErrorCode::invalid_config, 0};
        }
        if (source.kind == CaptureSourceKind::camera) {
            return start_camera(source, config, std::move(on_frame), std::move(on_stop));
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
        if (@available(macOS 13.0, *)) {
            if (captures_audio) {
                // ScreenCaptureKit mixes audio per application: a window filter yields its owning
                // app, a display filter yields every app. Excluding Catro keeps voice and watched
                // streams out of the share, so viewers never hear themselves echoed back.
                native_config.capturesAudio = YES;
                native_config.sampleRate = kCaptureAudioSampleRate;
                native_config.channelCount = 2;
                native_config.excludesCurrentProcessAudio = YES;
            }
        }

        output_ = [[CatroScreenStreamOutput alloc]
            initWithFrameHandler:std::move(on_frame)
                    audioHandler:std::move(on_audio)
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
        if (@available(macOS 13.0, *)) {
            if (captures_audio) {
                // Audio gets its own high-priority queue so a slow video callback never delays it.
                audio_queue_ = dispatch_queue_create(
                    "com.catro.screen-audio",
                    dispatch_queue_attr_make_with_qos_class(
                        DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INTERACTIVE, 0));
                if (![stream_ addStreamOutput:output_
                                         type:SCStreamOutputTypeAudio
                           sampleHandlerQueue:audio_queue_
                                        error:&add_error]) {
                    stop();
                    return native_error(
                        add_error, ScreenCaptureErrorCode::capture_creation_failed);
                }
            }
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
        if (session_ != nil) {
            for (id observer in observers_) {
                [NSNotificationCenter.defaultCenter removeObserver:observer];
            }
            observers_ = nil;
            [session_ stopRunning];
            dispatch_sync(queue_, ^{});
            session_ = nil;
            output_ = nil;
            queue_ = nil;
            return;
        }
        if (stream_ == nil) {
            output_ = nil;
            queue_ = nil;
            audio_queue_ = nil;
            return;
        }
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        [stream_ stopCaptureWithCompletionHandler:^(NSError*) {
            dispatch_semaphore_signal(done);
        }];
        dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
        NSError* ignored = nil;
        if (queue_ != nil) {
            dispatch_sync(queue_, ^{});
        }
        [stream_ removeStreamOutput:output_
                              type:SCStreamOutputTypeScreen
                             error:&ignored];
        if (audio_queue_ != nil) {
            dispatch_sync(audio_queue_, ^{});
            // audio_queue_ is only created on macOS 13 and later.
            if (@available(macOS 13.0, *)) {
                [stream_ removeStreamOutput:output_
                                      type:SCStreamOutputTypeAudio
                                     error:&ignored];
            }
        }
        stream_ = nil;
        output_ = nil;
        queue_ = nil;
        audio_queue_ = nil;
    }

    ~ScreenCaptureKitAdapter() override {
        stop();
    }

private:
    // Camera frames take the same NV12 path as screen frames. Audio is never captured here; the
    // microphone already reaches the room through voice.
    // ponytail: the preset picks the size and the camera keeps its own frame rate; choose a
    // device format per frame rate if 60 fps webcams matter.
    std::optional<ScreenCaptureError> start_camera(
        const CaptureSource& source,
        const ScreenCaptureConfig& config,
        FrameHandler on_frame,
        StopHandler on_stop) noexcept {
        AVCaptureDevice* device = nil;
        for (AVCaptureDevice* candidate in cameras()) {
            if (camera_id(candidate) == source.native_id) {
                device = candidate;
                break;
            }
        }
        if (device == nil) {
            return ScreenCaptureError{ScreenCaptureErrorCode::source_unavailable, 0};
        }
        if (!camera_access()) {
            return ScreenCaptureError{ScreenCaptureErrorCode::permission_denied, 0};
        }
        NSError* input_error = nil;
        AVCaptureDeviceInput* input =
            [AVCaptureDeviceInput deviceInputWithDevice:device error:&input_error];
        if (input == nil) {
            return native_error(input_error, ScreenCaptureErrorCode::capture_creation_failed);
        }

        // Pick the best real device format inside the policy ceiling instead of assuming a session
        // preset's frame rate. This matters for webcams exposing e.g. 1080p30 and 720p60.
        bool explicit_mode = false;
        if (const auto mode = camera_mode(device, config)) {
            NSError* lock_error = nil;
            if ([device lockForConfiguration:&lock_error]) {
                device.activeFormat = mode->format;
                const auto fps = std::max(1LL, std::llround(mode->fps));
                const CMTime duration = CMTimeMake(1, static_cast<int32_t>(fps));
                device.activeVideoMinFrameDuration = duration;
                device.activeVideoMaxFrameDuration = duration;
                [device unlockForConfiguration];
                explicit_mode = true;
            }
        }

        AVCaptureSession* session = [AVCaptureSession new];
        AVCaptureVideoDataOutput* video = [AVCaptureVideoDataOutput new];
        video.videoSettings = @{
            (__bridge id)kCVPixelBufferPixelFormatTypeKey :
                @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
        };
        video.alwaysDiscardsLateVideoFrames = YES;
        const StopHandler stopped = on_stop;
        output_ = [[CatroScreenStreamOutput alloc]
            initWithFrameHandler:std::move(on_frame)
                    audioHandler:AudioHandler{}
                     stopHandler:std::move(on_stop)];
        queue_ = dispatch_queue_create("com.catro.camera-capture", DISPATCH_QUEUE_SERIAL);
        [video setSampleBufferDelegate:output_ queue:queue_];

        [session beginConfiguration];
        const bool configured = [session canAddInput:input] && [session canAddOutput:video];
        if (configured) {
            [session addInput:input];
            [session addOutput:video];
            if (!explicit_mode) {
                NSArray<AVCaptureSessionPreset>* presets = config.max_height <= 720
                    ? @[ AVCaptureSessionPreset1280x720, AVCaptureSessionPresetHigh ]
                    : @[ AVCaptureSessionPreset1920x1080, AVCaptureSessionPreset1280x720,
                         AVCaptureSessionPresetHigh ];
                for (AVCaptureSessionPreset preset in presets) {
                    if ([session canSetSessionPreset:preset]) {
                        session.sessionPreset = preset;
                        break;
                    }
                }
            }
        }
        [session commitConfiguration];
        if (!configured) {
            output_ = nil;
            queue_ = nil;
            return ScreenCaptureError{ScreenCaptureErrorCode::capture_creation_failed, 0};
        }

        NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
        observers_ = @[
            [center addObserverForName:AVCaptureSessionRuntimeErrorNotification
                                object:session
                                 queue:nil
                            usingBlock:^(NSNotification* note) {
                                stopped(native_error(note.userInfo[AVCaptureSessionErrorKey],
                                                     ScreenCaptureErrorCode::frame_failure));
                            }],
            [center addObserverForName:AVCaptureDeviceWasDisconnectedNotification
                                object:device
                                 queue:nil
                            usingBlock:^(NSNotification*) {
                                stopped(ScreenCaptureError{
                                    ScreenCaptureErrorCode::source_unavailable, 0});
                            }],
        ];
        session_ = session;
        [session startRunning];
        if (!session.running) {
            stop();
            return ScreenCaptureError{ScreenCaptureErrorCode::capture_creation_failed, 0};
        }
        return std::nullopt;
    }

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
                                   onScreenWindowsOnly:NO
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
    dispatch_queue_t audio_queue_ = nil;
    __strong AVCaptureSession* session_ = nil;
    __strong NSArray* observers_ = nil;
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
    const ScreenCaptureConfig& config,
    ScreenCaptureNativeAdapter::AudioHandler on_audio) noexcept {
    stop();
    if (source.native_id == 0 || source.width < 2 || source.height < 2 ||
        config.frame_rate == 0 ||
        !video::fit_encodable_video_extent(
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
        std::move(on_audio),
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

#include "../probe_support.hpp"

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#include <CoreGraphics/CGDirectDisplayMetal.h>
#include <CoreGraphics/CoreGraphics.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

namespace catro::platform::macos {
namespace {

NativeDisplayMode describe(CGDisplayModeRef mode) {
    return {
        .pixel_width = static_cast<std::uint32_t>(CGDisplayModeGetPixelWidth(mode)),
        .pixel_height = static_cast<std::uint32_t>(CGDisplayModeGetPixelHeight(mode)),
        .point_width = static_cast<std::uint32_t>(CGDisplayModeGetWidth(mode)),
        .point_height = static_cast<std::uint32_t>(CGDisplayModeGetHeight(mode)),
        .refresh_hz = CGDisplayModeGetRefreshRate(mode),
    };
}

std::optional<std::vector<NativeDisplayMode>> modes(CGDirectDisplayID display) {
    // Include the unscaled variants of scaled modes; duplicates are removed in translation.
    NSDictionary* options = @{(__bridge NSString*)kCGDisplayShowDuplicateLowResolutionModes : @YES};
    const CFArrayRef all = CGDisplayCopyAllDisplayModes(display, (__bridge CFDictionaryRef)options);
    if (all == nullptr) {
        return std::nullopt;
    }
    std::vector<NativeDisplayMode> result;
    for (CFIndex index = 0; index < CFArrayGetCount(all); ++index) {
        // Get rule: the modes are owned by the array.
        const auto mode = static_cast<CGDisplayModeRef>(const_cast<void*>(CFArrayGetValueAtIndex(all, index)));
        if (mode != nullptr && CGDisplayModeIsUsableForDesktopGUI(mode)) {
            result.push_back(describe(mode));
        }
    }
    CFRelease(all);
    return result;
}

void add_screen_facts(std::vector<NativeDisplay>& displays) {
    for (NSScreen* screen in NSScreen.screens) {
        NSNumber* number = screen.deviceDescription[@"NSScreenNumber"];
        if (number == nil) {
            continue;
        }
        for (auto& display : displays) {
            if (display.id != number.unsignedIntValue) {
                continue;
            }
            display.hdr_supported = screen.maximumPotentialExtendedDynamicRangeColorComponentValue > 1.0;
            display.gamut = [screen canRepresentDisplayGamut:NSDisplayGamutP3] ? caps::ColorGamut::display_p3
                                                                              : caps::ColorGamut::srgb;
            const auto bits = NSBitsPerSampleFromDepth(screen.depth);
            if (bits > 0 && bits <= 255) {
                display.bits_per_channel = static_cast<std::uint8_t>(bits);
            }
        }
    }
}

} // namespace

caps::ProbeFragment run_gpu_display_probe(const caps::ProbeSpec& spec) {
    const auto started = std::chrono::steady_clock::now();
    auto fragment = begin_fragment(spec);
    NativeGpuDisplay native;

    @autoreleasepool {
        native.gpus = enumerate_gpus();
        if (!native.gpus) {
            add_issue(fragment.issues, spec.probe_id, caps::IssueCode::os_failure);
        }

        std::uint32_t count = 0;
        auto error = CGGetActiveDisplayList(0, nullptr, &count);
        std::vector<CGDirectDisplayID> identifiers(count);
        if (error == kCGErrorSuccess && count > 0) {
            error = CGGetActiveDisplayList(count, identifiers.data(), &count);
            identifiers.resize(count);
        }
        if (error == kCGErrorSuccess) {
            std::vector<NativeDisplay> displays;
            for (const auto identifier : identifiers) {
                NativeDisplay display{.id = identifier, .main = CGDisplayIsMain(identifier) != 0};
                if (id<MTLDevice> device = CGDirectDisplayCopyCurrentMetalDevice(identifier)) {
                    display.gpu = device.registryID;
                }
                if (const auto mode = CGDisplayCopyDisplayMode(identifier)) {
                    display.active = describe(mode);
                    CGDisplayModeRelease(mode);
                }
                display.modes = modes(identifier);
                displays.push_back(std::move(display));
            }
            add_screen_facts(displays);
            native.displays = std::move(displays);
        } else {
            fragment.native_error = error;
            add_issue(fragment.issues, spec.probe_id, caps::IssueCode::os_failure);
        }

        // Presence of the framework class and a preflight that never prompts; no capture starts.
        native.capture.screen_capture_kit = NSClassFromString(@"SCShareableContent") != nil;
        native.capture.access_granted = CGPreflightScreenCaptureAccess();
    }

    fragment.gpu_display = translate_gpu_display(native, spec.probe_id, fragment.issues);
    settle_outcome(fragment);
    finish_fragment(fragment, started);
    return fragment;
}

} // namespace catro::platform::macos

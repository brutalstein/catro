#pragma once

#include <catro/platform/macos/pixel_buffer.hpp>

#include <CoreFoundation/CoreFoundation.h>

#include <cstdint>
#include <cstring>

namespace catro::test {

// IOSurface-backed NV12 frame filled with a flat gray level, like a capture or decoder surface.
inline platform::macos::PixelBuffer make_nv12_surface(std::uint32_t width, std::uint32_t height,
                                                      std::uint8_t luma = 96) {
    CFDictionaryRef empty = CFDictionaryCreate(kCFAllocatorDefault, nullptr, nullptr, 0,
                                               &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    const void* keys[] = {kCVPixelBufferIOSurfacePropertiesKey};
    const void* values[] = {empty};
    CFDictionaryRef attributes = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1,
                                                    &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CVPixelBufferRef buffer = nullptr;
    CVPixelBufferCreate(kCFAllocatorDefault, width, height, kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange,
                        attributes, &buffer);
    CFRelease(attributes);
    CFRelease(empty);
    if (buffer != nullptr && CVPixelBufferLockBaseAddress(buffer, 0) == kCVReturnSuccess) {
        std::memset(CVPixelBufferGetBaseAddressOfPlane(buffer, 0), luma,
                    CVPixelBufferGetBytesPerRowOfPlane(buffer, 0) * CVPixelBufferGetHeightOfPlane(buffer, 0));
        std::memset(CVPixelBufferGetBaseAddressOfPlane(buffer, 1), 128,
                    CVPixelBufferGetBytesPerRowOfPlane(buffer, 1) * CVPixelBufferGetHeightOfPlane(buffer, 1));
        CVPixelBufferUnlockBaseAddress(buffer, 0);
    }
    return platform::macos::PixelBuffer::adopt(buffer);
}

} // namespace catro::test

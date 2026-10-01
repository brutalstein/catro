#pragma once

#include <CoreVideo/CoreVideo.h>

#include <cstdint>
#include <utility>

namespace catro::platform::macos {

// Owning, move-only CVPixelBufferRef. Capture, codec, and presentation frames stay IOSurface-backed
// GPU memory; holding this keeps the surface out of its pool until the holder is done with it.
class PixelBuffer final {
public:
    PixelBuffer() = default;

    [[nodiscard]] static PixelBuffer retain(CVPixelBufferRef buffer) noexcept {
        if (buffer != nullptr) {
            CVPixelBufferRetain(buffer);
        }
        return PixelBuffer(buffer);
    }

    // Takes over a +1 reference, such as one returned by CVPixelBufferCreate.
    [[nodiscard]] static PixelBuffer adopt(CVPixelBufferRef buffer) noexcept {
        return PixelBuffer(buffer);
    }

    ~PixelBuffer() {
        reset();
    }

    PixelBuffer(PixelBuffer&& other) noexcept : buffer_(std::exchange(other.buffer_, nullptr)) {}

    PixelBuffer& operator=(PixelBuffer&& other) noexcept {
        if (this != &other) {
            reset();
            buffer_ = std::exchange(other.buffer_, nullptr);
        }
        return *this;
    }

    PixelBuffer(const PixelBuffer&) = delete;
    PixelBuffer& operator=(const PixelBuffer&) = delete;

    void reset() noexcept {
        if (buffer_ != nullptr) {
            CVPixelBufferRelease(buffer_);
            buffer_ = nullptr;
        }
    }

    [[nodiscard]] CVPixelBufferRef get() const noexcept {
        return buffer_;
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return buffer_ != nullptr;
    }

    [[nodiscard]] std::uint32_t width() const noexcept {
        return buffer_ == nullptr ? 0U : static_cast<std::uint32_t>(CVPixelBufferGetWidth(buffer_));
    }

    [[nodiscard]] std::uint32_t height() const noexcept {
        return buffer_ == nullptr ? 0U : static_cast<std::uint32_t>(CVPixelBufferGetHeight(buffer_));
    }

private:
    explicit PixelBuffer(CVPixelBufferRef buffer) noexcept : buffer_(buffer) {}

    CVPixelBufferRef buffer_ = nullptr;
};

} // namespace catro::platform::macos

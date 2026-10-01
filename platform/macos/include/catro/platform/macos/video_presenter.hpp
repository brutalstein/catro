#pragma once

#include <catro/platform/macos/pixel_buffer.hpp>

#include <cstdint>
#include <memory>
#include <optional>

namespace catro::platform::macos {

enum class VideoPresenterErrorCode : std::uint8_t {
    invalid_source,
    // Only IOSurface-backed frames can be shown without a CPU copy.
    not_gpu_backed,
};

struct VideoPresenterError {
    VideoPresenterErrorCode code = VideoPresenterErrorCode::invalid_source;

    friend bool operator==(const VideoPresenterError&, const VideoPresenterError&) = default;
};

[[nodiscard]] constexpr const char* name(VideoPresenterErrorCode code) noexcept {
    switch (code) {
    case VideoPresenterErrorCode::invalid_source:
        return "presenter source frame is missing";
    case VideoPresenterErrorCode::not_gpu_backed:
        return "presenter source frame is not IOSurface-backed";
    }
    return "presenter failure";
}

struct VideoPresenterStatistics {
    bool visible = false;
    std::uint64_t frames_presented = 0;
    // Frames that arrived while the view was hidden; no presentation work is done for them.
    std::uint64_t frames_dropped = 0;
    std::uint64_t reconfigurations = 0;
    std::uint32_t source_width = 0;
    std::uint32_t source_height = 0;
};

// Shows IOSurface frames as Core Animation layer contents, composited by the window server on the
// GPU. The layer exists only while visible; hiding detaches it from its superlayer and releases it.
// set_visible/layer belong to the UI thread; present() may run on one media worker.
class MacVideoPresenter final {
public:
    MacVideoPresenter();
    ~MacVideoPresenter();

    MacVideoPresenter(const MacVideoPresenter&) = delete;
    MacVideoPresenter& operator=(const MacVideoPresenter&) = delete;

    void set_visible(bool visible) noexcept;
    // CALayer* for the shell to host; nullptr while hidden. Owned by the presenter.
    [[nodiscard]] void* layer() const noexcept;
    [[nodiscard]] std::optional<VideoPresenterError> present(const PixelBuffer& frame) noexcept;
    [[nodiscard]] VideoPresenterStatistics statistics() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace catro::platform::macos

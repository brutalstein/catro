#pragma once

#include <catro/platform/macos/screen_capture.hpp>

#include <cstdint>
#include <memory>
#include <optional>

namespace catro::platform::macos {

enum class VideoPresenterErrorCode : std::uint8_t {
    invalid_source,
    wrong_thread,
    surface_unavailable,
    not_visible,
    surface_failed,
    present_failed,
};

struct VideoPresenterError {
    VideoPresenterErrorCode code = VideoPresenterErrorCode::present_failed;
    std::int64_t native_code = 0;

    friend bool operator==(const VideoPresenterError&, const VideoPresenterError&) = default;
};

[[nodiscard]] constexpr const char* name(VideoPresenterErrorCode code) noexcept {
    switch (code) {
    case VideoPresenterErrorCode::invalid_source:
        return "invalid native video frame";
    case VideoPresenterErrorCode::wrong_thread:
        return "presentation surface changes must run on the main thread";
    case VideoPresenterErrorCode::surface_unavailable:
        return "video presentation surface unavailable";
    case VideoPresenterErrorCode::not_visible:
        return "video presenter is not visible";
    case VideoPresenterErrorCode::surface_failed:
        return "native presentation surface failed";
    case VideoPresenterErrorCode::present_failed:
        return "native video presentation failed";
    }
    return "video presentation failure";
}

struct VideoPresenterStatistics {
    std::uint64_t frames_presented = 0;
    std::uint64_t frames_dropped = 0;
    std::uint64_t reconfigurations = 0;
    std::uint64_t surface_replacements = 0;
    std::uint32_t source_width = 0;
    std::uint32_t source_height = 0;
};

class VideoPresenterNativeAdapter {
public:
    virtual ~VideoPresenterNativeAdapter() = default;
    [[nodiscard]] virtual std::optional<VideoPresenterError> replace_surface(
        void* surface) noexcept = 0;
    [[nodiscard]] virtual std::optional<VideoPresenterError> set_visible(
        bool visible) noexcept = 0;
    [[nodiscard]] virtual std::optional<VideoPresenterError> present(
        const NativeVideoFrame& frame) noexcept = 0;
    virtual void reset() noexcept = 0;
};

[[nodiscard]] std::unique_ptr<VideoPresenterNativeAdapter>
make_video_presenter_native_adapter();

class MacVideoPresenter final {
public:
    MacVideoPresenter();
    explicit MacVideoPresenter(std::unique_ptr<VideoPresenterNativeAdapter> adapter);
    ~MacVideoPresenter();

    MacVideoPresenter(const MacVideoPresenter&) = delete;
    MacVideoPresenter& operator=(const MacVideoPresenter&) = delete;

    [[nodiscard]] std::optional<VideoPresenterError> replace_surface(
        void* surface) noexcept;
    [[nodiscard]] std::optional<VideoPresenterError> set_visible(
        bool visible) noexcept;
    [[nodiscard]] std::optional<VideoPresenterError> present(
        const NativeVideoFrame& frame) noexcept;
    void reset() noexcept;

    [[nodiscard]] bool visible() const noexcept;
    [[nodiscard]] void* surface() const noexcept;
    [[nodiscard]] VideoPresenterStatistics statistics() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace catro::platform::macos

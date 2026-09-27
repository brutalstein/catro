#pragma once

#include <d3d11.h>
#include <dxgi1_3.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace catro::platform::windows {

enum class VideoPresenterErrorCode : std::uint8_t {
    invalid_source,
    device_mismatch,
    video_processor_unavailable,
    swap_chain_failed,
    present_failed,
};

struct VideoPresenterError {
    VideoPresenterErrorCode code = VideoPresenterErrorCode::swap_chain_failed;
    std::int64_t native_code = 0;

    friend bool operator==(const VideoPresenterError&, const VideoPresenterError&) = default;
};

[[nodiscard]] constexpr const char* name(VideoPresenterErrorCode code) noexcept {
    switch (code) {
    case VideoPresenterErrorCode::invalid_source:
        return "invalid video presentation source";
    case VideoPresenterErrorCode::device_mismatch:
        return "video presentation source changed D3D11 device";
    case VideoPresenterErrorCode::video_processor_unavailable:
        return "D3D11 video processor cannot present the source format";
    case VideoPresenterErrorCode::swap_chain_failed:
        return "composition swap-chain creation failed";
    case VideoPresenterErrorCode::present_failed:
        return "composition swap-chain presentation failed";
    }
    return "video presentation failure";
}

struct VideoPresenterConfig {
    std::uint32_t max_width = 960;
    std::uint32_t max_height = 540;
    std::uint32_t frame_rate = 30;
};

struct VideoPresenterStatistics {
    std::uint64_t frames_presented = 0;
    std::uint64_t frames_dropped = 0;
    std::uint64_t reconfigurations = 0;
    std::uint32_t source_width = 0;
    std::uint32_t source_height = 0;
    std::uint32_t output_width = 0;
    std::uint32_t output_height = 0;
    DXGI_FORMAT source_format = DXGI_FORMAT_UNKNOWN;
};

// Single-worker-thread GPU presenter. It converts/scales BGRA or NV12 with D3D11 VideoProcessor
// directly into a flip-model composition swap chain. No uncompressed frame is mapped to CPU memory.
// The returned swap chain is attached by the WinUI layer to a SwapChainPanel on the UI thread.
class D3D11CompositionVideoPresenter final {
public:
    explicit D3D11CompositionVideoPresenter(VideoPresenterConfig config = {});
    ~D3D11CompositionVideoPresenter();

    D3D11CompositionVideoPresenter(const D3D11CompositionVideoPresenter&) = delete;
    D3D11CompositionVideoPresenter& operator=(const D3D11CompositionVideoPresenter&) = delete;

    [[nodiscard]] std::optional<VideoPresenterError> present(
        ID3D11Texture2D& source,
        std::uint32_t subresource_index = 0);

    void reset() noexcept;

    [[nodiscard]] Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_chain() const;
    [[nodiscard]] VideoPresenterStatistics statistics() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace catro::platform::windows

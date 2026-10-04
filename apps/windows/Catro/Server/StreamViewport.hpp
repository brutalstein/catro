#pragma once

#include <dxgi1_3.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace winrt::Catro::implementation::stream_viewport {

struct ViewportSize {
    double width = 0.0;
    double height = 0.0;
};

[[nodiscard]] inline ViewportSize fit_viewport(
    std::uint32_t source_width,
    std::uint32_t source_height,
    double max_width,
    double max_height) noexcept {
    if (source_width == 0 || source_height == 0 ||
        !std::isfinite(max_width) || !std::isfinite(max_height) ||
        max_width <= 0.0 || max_height <= 0.0) {
        return {};
    }
    const auto scale = std::min(
        max_width / static_cast<double>(source_width),
        max_height / static_cast<double>(source_height));
    if (!std::isfinite(scale) || scale <= 0.0) {
        return {};
    }
    return {
        std::max(2.0, static_cast<double>(source_width) * scale),
        std::max(2.0, static_cast<double>(source_height) * scale),
    };
}

// The self-preview swap chain stays at most 640x360 to save GPU work, while its viewport grows
// to 960x720. A SwapChainPanel shows one buffer pixel per view pixel, so the compositor scales
// the buffer up to fill the viewport; the scale itself costs no rendering.
inline void fit_swap_chain(IDXGISwapChain1* swap_chain, double width, double height) noexcept {
    DXGI_SWAP_CHAIN_DESC1 desc{};
    if (swap_chain == nullptr || !(width > 0.0) || !(height > 0.0) ||
        FAILED(swap_chain->GetDesc1(&desc)) || desc.Width == 0 || desc.Height == 0) {
        return;
    }
    ::Microsoft::WRL::ComPtr<IDXGISwapChain2> scalable;
    if (FAILED(swap_chain->QueryInterface(IID_PPV_ARGS(&scalable)))) {
        return;
    }
    const auto scale = static_cast<float>(std::min(
        width / static_cast<double>(desc.Width),
        height / static_cast<double>(desc.Height)));
    const DXGI_MATRIX_3X2_F matrix{scale, 0.0f, 0.0f, scale, 0.0f, 0.0f};
    (void)scalable->SetMatrixTransform(&matrix);
}

} // namespace winrt::Catro::implementation::stream_viewport

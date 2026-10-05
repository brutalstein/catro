#include <catro/platform/windows/video_presenter.hpp>

#include <catch2/catch_test_macros.hpp>

#include <d3d11.h>
#include <wrl/client.h>
#include "../../apps/windows/Catro/Server/StreamViewport.hpp"
#include <cmath>
#include <limits>

using namespace catro::platform::windows;

TEST_CASE("stream viewports fit every aspect ratio without exceeding available space") {
    using winrt::Catro::implementation::stream_viewport::fit_viewport;
    for (const auto source : {std::pair{1920U, 1080U}, {1080U, 1920U}, {3440U, 1440U}}) {
        for (const auto box : {std::pair{900.0, 600.0}, {150.0, 80.0}, {1.0, 0.5}}) {
            const auto size = fit_viewport(source.first, source.second, box.first, box.second);
            CHECK(size.width <= box.first + 1e-9);
            CHECK(size.height <= box.second + 1e-9);
            CHECK(std::abs(size.width / size.height - static_cast<double>(source.first) / source.second) < 1e-9);
        }
    }
    CHECK(fit_viewport(1920, 1080, 0, 600).width == 0);
    CHECK(fit_viewport(1920, 1080, 600, std::numeric_limits<double>::quiet_NaN()).height == 0);
}

TEST_CASE("D3D11 composition video presenter shows only the visible part of a padded frame") {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                 D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                                 nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, nullptr))) {
        WARN("no hardware D3D11 device; presenter crop skipped");
        return;
    }
    // CI runners report the Microsoft Basic Render Driver (vendor 0x1414) as hardware, but it has
    // no video processor; the crop needs a real GPU.
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC adapter_desc{};
    if (SUCCEEDED(device.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(&adapter)) &&
        SUCCEEDED(adapter->GetDesc(&adapter_desc)) && adapter_desc.VendorId == 0x1414) {
        WARN("software adapter only; presenter crop skipped");
        return;
    }
    // A decoder surface for 1080p: 1088 coded rows.
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 1920;
    desc.Height = 1088;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    REQUIRE(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)));

    D3D11CompositionVideoPresenter presenter(VideoPresenterConfig{
        .max_width = 3840, .max_height = 2160, .frame_rate = 60});
    const auto error = presenter.present(*texture.Get(), 0, 1920, 1080);
    INFO((error ? name(error->code) : "none"));
    REQUIRE_FALSE(error);
    const auto stats = presenter.statistics();
    CHECK(stats.source_height == 1080);
    CHECK(stats.output_width == 1920);
    CHECK(stats.output_height == 1080);
    REQUIRE(presenter.swap_chain());
    DXGI_SWAP_CHAIN_DESC1 swap{};
    REQUIRE(SUCCEEDED(presenter.swap_chain()->GetDesc1(&swap)));
    CHECK(swap.Height == 1080);
}

TEST_CASE("D3D11 composition video presenter is inert until a GPU frame arrives") {
    D3D11CompositionVideoPresenter presenter;

    CHECK_FALSE(presenter.swap_chain());
    const auto stats = presenter.statistics();
    CHECK(stats.frames_presented == 0);
    CHECK(stats.frames_dropped == 0);
    CHECK(stats.reconfigurations == 0);
    CHECK(stats.source_width == 0);
    CHECK(stats.output_width == 0);

    presenter.reset();
    presenter.reset();
    CHECK_FALSE(presenter.swap_chain());
}

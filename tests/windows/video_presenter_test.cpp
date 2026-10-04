#include <catro/platform/windows/video_presenter.hpp>

#include <catch2/catch_test_macros.hpp>

#include <d3d11.h>
#include <wrl/client.h>

using namespace catro::platform::windows;

TEST_CASE("D3D11 composition video presenter shows only the visible part of a padded frame") {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                 D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                                 nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, nullptr))) {
        WARN("no hardware D3D11 device; presenter crop skipped");
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
    REQUIRE_FALSE(presenter.present(*texture.Get(), 0, 1920, 1080));
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

#include <catro/platform/windows/video_decoder.hpp>
#include <catro/platform/windows/video_encoder.hpp>

#include <catch2/catch_test_macros.hpp>

#include <d3d11.h>
#include <wrl/client.h>

#include <cstddef>
#include <span>

using namespace catro::platform::windows;

// 1080 is not a multiple of 16, so H.264 codes 1088 rows and crops 8. Viewers must get the
// cropped picture: the padding rows are garbage and would also squeeze the aspect ratio.
TEST_CASE("Windows H264 round trip keeps a 1080p picture at 1080 rows") {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                 D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                                 nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, nullptr))) {
        WARN("no hardware D3D11 device; round trip skipped");
        return;
    }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 1920;
    desc.Height = 1080;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    REQUIRE(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)));

    WindowsH264HardwareEncoder encoder;
    HardwareEncoderConfig encoder_config;
    encoder_config.width = 1920;
    encoder_config.height = 1080;
    if (const auto failure = encoder.start(encoder_config, *texture.Get())) {
        WARN("no hardware H.264 encoder; round trip skipped");
        return;
    }
    WindowsH264D3D11Decoder decoder;
    REQUIRE_FALSE(decoder.start(H264DecoderConfig{}));

    GpuCaptureFrame frame;
    frame.texture = texture;
    frame.width = 1920;
    frame.height = 1080;
    frame.format = desc.Format;
    DecodedGpuFrame decoded;
    for (int index = 0; index < 90 && !decoded.texture; ++index) {
        EncodedAccessUnit unit;
        frame.sequence = static_cast<std::uint64_t>(index + 1);
        REQUIRE_FALSE(encoder.encode(frame, unit, index == 0));
        if (!unit.bytes.empty()) {
            REQUIRE_FALSE(decoder.decode(unit.bytes, unit.pts_100ns, decoded));
        }
    }
    REQUIRE(decoded.texture);
    CHECK(decoded.width == 1920);
    CHECK(decoded.height == 1080);
    // The frame's sample lease belongs to the decoder; return it before the decoder shuts down.
    decoded = {};
    decoder.stop();
    encoder.stop();
}

TEST_CASE("Windows H264 decoder is inert until started") {
    WindowsH264D3D11Decoder decoder;
    CHECK_FALSE(decoder.running());

    const auto stats = decoder.statistics();
    CHECK(stats.frames_submitted == 0);
    CHECK(stats.frames_decoded == 0);
    CHECK(stats.compressed_bytes == 0);
    CHECK(stats.input_failures == 0);
    CHECK(stats.output_failures == 0);
    CHECK(stats.gpu_output_failures == 0);
    CHECK(stats.oversized_inputs == 0);
    CHECK(stats.stream_changes == 0);
    CHECK(stats.input_sample_allocations == 0);

    decoder.stop();
    decoder.stop();
    CHECK_FALSE(decoder.running());
}

TEST_CASE("Windows H264 decoder rejects invalid configuration before device creation") {
    WindowsH264D3D11Decoder decoder;
    H264DecoderConfig config;
    config.max_access_unit_bytes = 1;

    const auto failure = decoder.start(config);
    REQUIRE(failure);
    CHECK(failure->code == H264DecoderErrorCode::invalid_config);
    CHECK_FALSE(decoder.running());
}

TEST_CASE("Windows H264 decoder rejects input before startup") {
    WindowsH264D3D11Decoder decoder;
    DecodedGpuFrame output;
    const std::byte byte{0};

    const auto failure = decoder.decode(
        std::span<const std::byte>(&byte, 1),
        0,
        output);
    REQUIRE(failure);
    CHECK(failure->code == H264DecoderErrorCode::input_failed);
    CHECK_FALSE(output.texture);
}

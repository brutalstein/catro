#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace catro::platform::windows {

enum class H264DecoderErrorCode : std::uint8_t {
    invalid_config,
    device_creation_failed,
    media_foundation_startup_failed,
    decoder_activation_failed,
    decoder_not_d3d11,
    media_type_failed,
    stream_start_failed,
    input_too_large,
    input_failed,
    output_failed,
    gpu_output_unavailable,
};

struct H264DecoderError {
    H264DecoderErrorCode code = H264DecoderErrorCode::decoder_activation_failed;
    std::int64_t native_code = 0;

    friend bool operator==(const H264DecoderError&, const H264DecoderError&) = default;
};

[[nodiscard]] constexpr const char* name(H264DecoderErrorCode code) noexcept {
    switch (code) {
    case H264DecoderErrorCode::invalid_config:
        return "invalid H.264 decoder configuration";
    case H264DecoderErrorCode::device_creation_failed:
        return "D3D11 decoder device creation failed";
    case H264DecoderErrorCode::media_foundation_startup_failed:
        return "Media Foundation decoder startup failed";
    case H264DecoderErrorCode::decoder_activation_failed:
        return "Microsoft H.264 decoder activation failed";
    case H264DecoderErrorCode::decoder_not_d3d11:
        return "H.264 decoder is not D3D11-aware";
    case H264DecoderErrorCode::media_type_failed:
        return "H.264 decoder media-type negotiation failed";
    case H264DecoderErrorCode::stream_start_failed:
        return "H.264 decoder stream start failed";
    case H264DecoderErrorCode::input_too_large:
        return "H.264 decoder input exceeded the configured bounded access-unit size";
    case H264DecoderErrorCode::input_failed:
        return "H.264 decoder rejected compressed input";
    case H264DecoderErrorCode::output_failed:
        return "H.264 decoder output failed";
    case H264DecoderErrorCode::gpu_output_unavailable:
        return "H.264 decoder did not produce a D3D11 NV12 surface";
    }
    return "H.264 decoder failure";
}

struct H264DecoderConfig {
    std::size_t max_access_unit_bytes = 4U * 1024U * 1024U;
    std::optional<std::uint64_t> adapter_luid;
};

struct DecodedGpuFrame {
    // The sample lease keeps the decoder-owned texture/subresource alive until presentation is
    // finished with this frame. No uncompressed frame is copied into application CPU memory.
    Microsoft::WRL::ComPtr<IUnknown> sample_lease;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    std::uint32_t subresource_index = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    std::int64_t pts_100ns = 0;
};

struct H264DecoderStatistics {
    std::uint64_t adapter_luid = 0;
    std::string decoder_name;
    bool d3d11_aware = false;
    bool multithread_protected = false;
    bool low_latency_requested = false;
    bool low_latency_applied = false;
    bool hardware_acceleration_requested = false;
    bool hardware_acceleration_applied = false;

    std::uint64_t frames_submitted = 0;
    std::uint64_t frames_decoded = 0;
    std::uint64_t compressed_bytes = 0;
    std::uint64_t input_failures = 0;
    std::uint64_t output_failures = 0;
    std::uint64_t gpu_output_failures = 0;
    std::uint64_t oversized_inputs = 0;
    std::uint64_t stream_changes = 0;
    std::uint64_t input_sample_allocations = 0;

    std::uint64_t decode_total_us = 0;
    std::uint64_t decode_max_us = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// Low-latency Windows H.264 decoder path:
//
//   bounded Annex-B CPU bytes -> Media Foundation H.264 decoder -> D3D11 NV12 texture
//
// Compressed bytes are copied once into a reusable Media Foundation input buffer. Uncompressed
// output must remain GPU-resident; a CPU-backed output is rejected instead of silently degrading.
// The object is single-worker-thread by design so codec queues stay bounded and deterministic.
class WindowsH264D3D11Decoder final {
public:
    WindowsH264D3D11Decoder();
    ~WindowsH264D3D11Decoder();

    WindowsH264D3D11Decoder(const WindowsH264D3D11Decoder&) = delete;
    WindowsH264D3D11Decoder& operator=(const WindowsH264D3D11Decoder&) = delete;

    [[nodiscard]] std::optional<H264DecoderError> start(
        const H264DecoderConfig& config = {});

    // Success with output.texture == nullptr means the decoder needs more compressed input,
    // typically while joining between keyframes/SPS-PPS boundaries.
    [[nodiscard]] std::optional<H264DecoderError> decode(
        std::span<const std::byte> annex_b_access_unit,
        std::int64_t pts_100ns,
        DecodedGpuFrame& output);

    void stop() noexcept;

    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] H264DecoderStatistics statistics() const;
    [[nodiscard]] Microsoft::WRL::ComPtr<ID3D11Device> d3d_device() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace catro::platform::windows

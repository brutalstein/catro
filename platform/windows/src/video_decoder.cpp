#include <catro/platform/windows/video_decoder.hpp>

#include <Windows.h>
#include <codecapi.h>
#include <d3d10.h>
#include <dxgi1_6.h>
#include <icodecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mftransform.h>
#include <wmcodecdsp.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <utility>
#include <variant>

namespace catro::platform::windows {
namespace {

using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;

constexpr std::size_t kMinimumInputCapacity = 64U * 1024U;
constexpr std::size_t kMaximumInputCapacity = 64U * 1024U * 1024U;
constexpr DWORD kMaxOutputTypeCount = 64;

[[nodiscard]] std::uint64_t elapsed_us(Clock::time_point started) noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now() - started).count());
}

[[nodiscard]] std::uint64_t pack_luid(LUID luid) noexcept {
    return (static_cast<std::uint64_t>(
                static_cast<std::uint32_t>(luid.HighPart)) << 32U) |
           static_cast<std::uint64_t>(luid.LowPart);
}

[[nodiscard]] LUID unpack_luid(std::uint64_t packed) noexcept {
    LUID luid{};
    luid.LowPart = static_cast<DWORD>(packed & 0xffffffffULL);
    luid.HighPart = static_cast<LONG>((packed >> 32U) & 0xffffffffULL);
    return luid;
}

struct DecoderDevice {
    ComPtr<ID3D11Device> device;
    std::uint64_t adapter_luid = 0;
};

[[nodiscard]] HRESULT try_create_device(
    IDXGIAdapter1& adapter, DecoderDevice& output) noexcept {
    DXGI_ADAPTER_DESC1 description{};
    auto result = adapter.GetDesc1(&description);
    if (FAILED(result) ||
        (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
        return FAILED(result) ? result : E_FAIL;
    }

    constexpr UINT flags =
        D3D11_CREATE_DEVICE_BGRA_SUPPORT |
        D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    const std::array levels{
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    ComPtr<ID3D11Device> device;
    result = D3D11CreateDevice(
        &adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags,
        levels.data(), static_cast<UINT>(levels.size()),
        D3D11_SDK_VERSION, &device, nullptr, nullptr);
    if (result == E_INVALIDARG) {
        result = D3D11CreateDevice(
            &adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags,
            &levels[1], 1, D3D11_SDK_VERSION,
            &device, nullptr, nullptr);
    }
    if (FAILED(result) || !device) {
        return result;
    }

    output.device = std::move(device);
    output.adapter_luid = pack_luid(description.AdapterLuid);
    return S_OK;
}

[[nodiscard]] std::variant<DecoderDevice, H264DecoderError> create_decoder_device(
    const std::optional<std::uint64_t>& requested_luid) {
    ComPtr<IDXGIFactory1> factory;
    auto result = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(result) || !factory) {
        return H264DecoderError{
            H264DecoderErrorCode::device_creation_failed, result};
    }

    if (requested_luid) {
        ComPtr<IDXGIFactory4> factory4;
        result = factory.As(&factory4);
        if (FAILED(result) || !factory4) {
            return H264DecoderError{
                H264DecoderErrorCode::device_creation_failed, result};
        }

        ComPtr<IDXGIAdapter1> adapter;
        const auto luid = unpack_luid(*requested_luid);
        result = factory4->EnumAdapterByLuid(
            luid, IID_PPV_ARGS(&adapter));
        if (FAILED(result) || !adapter) {
            return H264DecoderError{
                H264DecoderErrorCode::device_creation_failed, result};
        }

        DecoderDevice created;
        result = try_create_device(*adapter.Get(), created);
        if (FAILED(result)) {
            return H264DecoderError{
                H264DecoderErrorCode::device_creation_failed, result};
        }
        return created;
    }

    ComPtr<IDXGIFactory6> factory6;
    if (SUCCEEDED(factory.As(&factory6)) && factory6) {
        for (UINT index = 0;; ++index) {
            ComPtr<IDXGIAdapter1> adapter;
            result = factory6->EnumAdapterByGpuPreference(
                index,
                DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                IID_PPV_ARGS(&adapter));
            if (result == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            if (FAILED(result)) {
                return H264DecoderError{
                    H264DecoderErrorCode::device_creation_failed, result};
            }

            DecoderDevice created;
            if (SUCCEEDED(try_create_device(*adapter.Get(), created))) {
                return created;
            }
        }
    }

    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> adapter;
        result = factory->EnumAdapters1(index, &adapter);
        if (result == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (FAILED(result)) {
            return H264DecoderError{
                H264DecoderErrorCode::device_creation_failed, result};
        }

        DecoderDevice created;
        if (SUCCEEDED(try_create_device(*adapter.Get(), created))) {
            return created;
        }
    }

    return H264DecoderError{H264DecoderErrorCode::device_creation_failed};
}

[[nodiscard]] bool set_codec_bool(
    ICodecAPI& codec, const GUID& key, bool value) noexcept {
    VARIANT setting{};
    setting.vt = VT_BOOL;
    setting.boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
    return SUCCEEDED(codec.SetValue(&key, &setting));
}

} // namespace

struct WindowsH264D3D11Decoder::Impl {
    [[nodiscard]] std::optional<H264DecoderError> start(
        const H264DecoderConfig& next_config) {
        stop();
        stats_ = {};

        if (next_config.max_access_unit_bytes < kMinimumInputCapacity ||
            next_config.max_access_unit_bytes > kMaximumInputCapacity ||
            next_config.max_access_unit_bytes >
                static_cast<std::size_t>(std::numeric_limits<DWORD>::max())) {
            return fail(H264DecoderError{H264DecoderErrorCode::invalid_config});
        }
        config_ = next_config;

        auto device_result = create_decoder_device(config_.adapter_luid);
        if (const auto* error =
                std::get_if<H264DecoderError>(&device_result)) {
            return fail(*error);
        }

        auto created =
            std::get<DecoderDevice>(std::move(device_result));
        device_ = std::move(created.device);
        stats_.adapter_luid = created.adapter_luid;

        ComPtr<ID3D10Multithread> multithread;
        if (SUCCEEDED(device_.As(&multithread)) && multithread) {
            stats_.multithread_protected =
                multithread->SetMultithreadProtected(TRUE) != FALSE;
        }

        const auto apartment =
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) {
            return fail(H264DecoderError{
                H264DecoderErrorCode::media_foundation_startup_failed,
                apartment});
        }
        apartment_initialized_ = SUCCEEDED(apartment);

        auto result = MFStartup(MF_VERSION, MFSTARTUP_LITE);
        if (FAILED(result)) {
            return fail(H264DecoderError{
                H264DecoderErrorCode::media_foundation_startup_failed,
                result});
        }
        media_foundation_started_ = true;

        result = CoCreateInstance(
            __uuidof(CMSH264DecoderMFT), nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&transform_));
        if (FAILED(result) || !transform_) {
            return fail(H264DecoderError{
                H264DecoderErrorCode::decoder_activation_failed,
                result});
        }
        stats_.decoder_name = "Microsoft H.264 Video Decoder MFT";

        ComPtr<IMFAttributes> attributes;
        result = transform_->GetAttributes(&attributes);
        if (FAILED(result) || !attributes) {
            return fail(H264DecoderError{
                H264DecoderErrorCode::decoder_activation_failed,
                result});
        }

        stats_.d3d11_aware =
            MFGetAttributeUINT32(
                attributes.Get(), MF_SA_D3D11_AWARE, FALSE) != FALSE;
        if (!stats_.d3d11_aware) {
            return fail(H264DecoderError{
                H264DecoderErrorCode::decoder_not_d3d11});
        }

        if (MFGetAttributeUINT32(
                attributes.Get(), MF_TRANSFORM_ASYNC, FALSE) != FALSE) {
            // The inbox decoder is synchronous. Reject an unexpected asynchronous substitute
            // rather than introducing an unbounded event queue into this low-latency primitive.
            return fail(H264DecoderError{
                H264DecoderErrorCode::decoder_activation_failed});
        }

        stats_.low_latency_requested = true;
        stats_.hardware_acceleration_requested = true;
        if (ComPtr<ICodecAPI> codec;
            SUCCEEDED(transform_.As(&codec)) && codec) {
            stats_.low_latency_applied =
                set_codec_bool(
                    *codec.Get(), CODECAPI_AVLowLatencyMode, true);
            stats_.hardware_acceleration_applied =
                set_codec_bool(
                    *codec.Get(),
                    CODECAPI_AVDecVideoAcceleration_H264,
                    true);
        }

        UINT reset_token = 0;
        result = MFCreateDXGIDeviceManager(
            &reset_token, &device_manager_);
        if (FAILED(result) || !device_manager_) {
            return fail(H264DecoderError{
                H264DecoderErrorCode::decoder_activation_failed,
                result});
        }
        result =
            device_manager_->ResetDevice(device_.Get(), reset_token);
        if (FAILED(result)) {
            return fail(H264DecoderError{
                H264DecoderErrorCode::decoder_activation_failed,
                result});
        }
        result = transform_->ProcessMessage(
            MFT_MESSAGE_SET_D3D_MANAGER,
            reinterpret_cast<ULONG_PTR>(device_manager_.Get()));
        if (FAILED(result)) {
            return fail(H264DecoderError{
                H264DecoderErrorCode::decoder_not_d3d11,
                result});
        }

        ComPtr<IMFMediaType> input_type;
        result = MFCreateMediaType(&input_type);
        if (FAILED(result) || !input_type ||
            FAILED(input_type->SetGUID(
                MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
            FAILED(input_type->SetGUID(
                MF_MT_SUBTYPE, MFVideoFormat_H264)) ||
            FAILED(transform_->SetInputType(
                0, input_type.Get(), 0))) {
            return fail(H264DecoderError{
                H264DecoderErrorCode::media_type_failed,
                result});
        }

        if (const auto error = negotiate_output_type()) {
            return fail(*error);
        }

        if (FAILED(transform_->ProcessMessage(
                MFT_MESSAGE_COMMAND_FLUSH, 0)) ||
            FAILED(transform_->ProcessMessage(
                MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0)) ||
            FAILED(transform_->ProcessMessage(
                MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0))) {
            return fail(H264DecoderError{
                H264DecoderErrorCode::stream_start_failed});
        }

        running_ = true;
        return std::nullopt;
    }

    [[nodiscard]] std::optional<H264DecoderError> decode(
        std::span<const std::byte> access_unit,
        std::int64_t pts_100ns,
        DecodedGpuFrame& output) {
        output = {};

        if (!running_ || access_unit.empty()) {
            ++stats_.input_failures;
            return H264DecoderError{
                H264DecoderErrorCode::input_failed};
        }
        if (access_unit.size() > config_.max_access_unit_bytes) {
            ++stats_.oversized_inputs;
            ++stats_.input_failures;
            return H264DecoderError{
                H264DecoderErrorCode::input_too_large};
        }

        if (const auto error =
                ensure_input_sample(access_unit.size())) {
            ++stats_.input_failures;
            return error;
        }

        BYTE* destination = nullptr;
        DWORD maximum = 0;
        DWORD current = 0;
        auto result =
            input_buffer_->Lock(&destination, &maximum, &current);
        if (FAILED(result) || destination == nullptr ||
            maximum < access_unit.size()) {
            if (SUCCEEDED(result)) {
                (void)input_buffer_->Unlock();
            }
            ++stats_.input_failures;
            return H264DecoderError{
                H264DecoderErrorCode::input_failed, result};
        }

        std::memcpy(
            destination, access_unit.data(), access_unit.size());
        const auto unlock = input_buffer_->Unlock();
        if (FAILED(unlock) ||
            FAILED(input_buffer_->SetCurrentLength(
                static_cast<DWORD>(access_unit.size()))) ||
            FAILED(input_sample_->SetSampleTime(pts_100ns))) {
            ++stats_.input_failures;
            return H264DecoderError{
                H264DecoderErrorCode::input_failed,
                FAILED(unlock) ? unlock : E_FAIL};
        }

        const auto decode_started = Clock::now();
        result = transform_->ProcessInput(
            0, input_sample_.Get(), 0);
        if (FAILED(result)) {
            ++stats_.input_failures;
            return H264DecoderError{
                H264DecoderErrorCode::input_failed, result};
        }

        ++stats_.frames_submitted;
        stats_.compressed_bytes += access_unit.size();

        if (const auto error = collect_output(output)) {
            if (error->code ==
                H264DecoderErrorCode::gpu_output_unavailable) {
                ++stats_.gpu_output_failures;
            } else {
                ++stats_.output_failures;
            }
            return error;
        }

        const auto decode_us = elapsed_us(decode_started);
        stats_.decode_total_us += decode_us;
        stats_.decode_max_us =
            std::max(stats_.decode_max_us, decode_us);

        if (output.texture) {
            ++stats_.frames_decoded;
            stats_.width = output.width;
            stats_.height = output.height;
        }
        return std::nullopt;
    }

    void stop() noexcept {
        running_ = false;

        if (transform_) {
            (void)transform_->ProcessMessage(
                MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
            (void)transform_->ProcessMessage(
                MFT_MESSAGE_COMMAND_FLUSH, 0);
            (void)transform_->ProcessMessage(
                MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
            (void)transform_->ProcessMessage(
                MFT_MESSAGE_SET_D3D_MANAGER, 0);
        }

        input_sample_.Reset();
        input_buffer_.Reset();
        input_capacity_ = 0;
        device_manager_.Reset();
        transform_.Reset();
        device_.Reset();

        if (media_foundation_started_) {
            (void)MFShutdown();
            media_foundation_started_ = false;
        }
        if (apartment_initialized_) {
            CoUninitialize();
            apartment_initialized_ = false;
        }
    }

    [[nodiscard]] std::optional<H264DecoderError>
    ensure_input_sample(std::size_t required) {
        if (input_sample_ && input_buffer_ &&
            input_capacity_ >= required) {
            if (FAILED(input_buffer_->SetCurrentLength(0))) {
                return H264DecoderError{
                    H264DecoderErrorCode::input_failed};
            }
            return std::nullopt;
        }

        if (required > config_.max_access_unit_bytes) {
            return H264DecoderError{
                H264DecoderErrorCode::input_too_large};
        }

        auto capacity = std::max(
            kMinimumInputCapacity,
            std::bit_ceil(required));
        capacity = std::min(
            capacity, config_.max_access_unit_bytes);
        if (capacity < required ||
            capacity >
                static_cast<std::size_t>(
                    std::numeric_limits<DWORD>::max())) {
            return H264DecoderError{
                H264DecoderErrorCode::input_too_large};
        }

        ComPtr<IMFMediaBuffer> buffer;
        auto result = MFCreateMemoryBuffer(
            static_cast<DWORD>(capacity), &buffer);
        if (FAILED(result) || !buffer) {
            return H264DecoderError{
                H264DecoderErrorCode::input_failed, result};
        }

        ComPtr<IMFSample> sample;
        result = MFCreateSample(&sample);
        if (FAILED(result) || !sample) {
            return H264DecoderError{
                H264DecoderErrorCode::input_failed, result};
        }
        result = sample->AddBuffer(buffer.Get());
        if (FAILED(result)) {
            return H264DecoderError{
                H264DecoderErrorCode::input_failed, result};
        }

        input_buffer_ = std::move(buffer);
        input_sample_ = std::move(sample);
        input_capacity_ = capacity;
        ++stats_.input_sample_allocations;
        return std::nullopt;
    }

    [[nodiscard]] std::optional<H264DecoderError>
    negotiate_output_type() {
        HRESULT last_result = MF_E_INVALIDMEDIATYPE;

        for (DWORD index = 0;
             index < kMaxOutputTypeCount;
             ++index) {
            ComPtr<IMFMediaType> type;
            const auto available =
                transform_->GetOutputAvailableType(
                    0, index, &type);
            if (available == MF_E_NO_MORE_TYPES) {
                break;
            }
            if (FAILED(available) || !type) {
                last_result = available;
                continue;
            }

            GUID subtype{};
            if (FAILED(type->GetGUID(
                    MF_MT_SUBTYPE, &subtype)) ||
                subtype != MFVideoFormat_NV12) {
                continue;
            }

            const auto set =
                transform_->SetOutputType(
                    0, type.Get(), 0);
            if (FAILED(set)) {
                last_result = set;
                continue;
            }

            MFT_OUTPUT_STREAM_INFO info{};
            const auto info_result =
                transform_->GetOutputStreamInfo(0, &info);
            if (FAILED(info_result)) {
                return H264DecoderError{
                    H264DecoderErrorCode::media_type_failed,
                    info_result};
            }
            if ((info.dwFlags &
                 (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES |
                  MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) == 0) {
                return H264DecoderError{
                    H264DecoderErrorCode::gpu_output_unavailable};
            }

            UINT32 width = 0;
            UINT32 height = 0;
            if (SUCCEEDED(MFGetAttributeSize(
                    type.Get(), MF_MT_FRAME_SIZE,
                    &width, &height))) {
                stats_.width = width;
                stats_.height = height;
            }
            return std::nullopt;
        }

        return H264DecoderError{
            H264DecoderErrorCode::media_type_failed,
            last_result};
    }

    [[nodiscard]] std::optional<H264DecoderError>
    collect_output(DecodedGpuFrame& output) {
        for (int attempt = 0; attempt < 3; ++attempt) {
            MFT_OUTPUT_DATA_BUFFER output_buffer{};
            output_buffer.dwStreamID = 0;
            DWORD status = 0;
            const auto result = transform_->ProcessOutput(
                0, 1, &output_buffer, &status);

            if (output_buffer.pEvents != nullptr) {
                output_buffer.pEvents->Release();
                output_buffer.pEvents = nullptr;
            }

            ComPtr<IMFSample> sample;
            if (output_buffer.pSample != nullptr) {
                sample.Attach(output_buffer.pSample);
                output_buffer.pSample = nullptr;
            }

            if (result == MF_E_TRANSFORM_NEED_MORE_INPUT) {
                return std::nullopt;
            }
            if (result == MF_E_TRANSFORM_STREAM_CHANGE) {
                ++stats_.stream_changes;
                if (const auto error =
                        negotiate_output_type()) {
                    return error;
                }
                continue;
            }
            if (FAILED(result)) {
                return H264DecoderError{
                    H264DecoderErrorCode::output_failed,
                    result};
            }
            if (!sample) {
                return H264DecoderError{
                    H264DecoderErrorCode::gpu_output_unavailable};
            }

            ComPtr<IMFMediaBuffer> media_buffer;
            auto buffer_result =
                sample->GetBufferByIndex(
                    0, &media_buffer);
            if (FAILED(buffer_result) || !media_buffer) {
                return H264DecoderError{
                    H264DecoderErrorCode::gpu_output_unavailable,
                    buffer_result};
            }

            ComPtr<IMFDXGIBuffer> dxgi_buffer;
            buffer_result = media_buffer.As(&dxgi_buffer);
            if (FAILED(buffer_result) || !dxgi_buffer) {
                return H264DecoderError{
                    H264DecoderErrorCode::gpu_output_unavailable,
                    buffer_result};
            }

            ComPtr<ID3D11Texture2D> texture;
            buffer_result = dxgi_buffer->GetResource(
                IID_PPV_ARGS(&texture));
            if (FAILED(buffer_result) || !texture) {
                return H264DecoderError{
                    H264DecoderErrorCode::gpu_output_unavailable,
                    buffer_result};
            }

            UINT subresource = 0;
            buffer_result =
                dxgi_buffer->GetSubresourceIndex(
                    &subresource);
            if (FAILED(buffer_result)) {
                return H264DecoderError{
                    H264DecoderErrorCode::gpu_output_unavailable,
                    buffer_result};
            }

            ComPtr<ID3D11Device> output_device;
            texture->GetDevice(&output_device);
            if (!output_device ||
                output_device.Get() != device_.Get()) {
                return H264DecoderError{
                    H264DecoderErrorCode::gpu_output_unavailable};
            }

            D3D11_TEXTURE2D_DESC description{};
            texture->GetDesc(&description);
            if (description.Format != DXGI_FORMAT_NV12 ||
                description.Width == 0 ||
                description.Height == 0) {
                return H264DecoderError{
                    H264DecoderErrorCode::gpu_output_unavailable};
            }

            LONGLONG sample_time = 0;
            (void)sample->GetSampleTime(&sample_time);

            ComPtr<IUnknown> lease;
            if (FAILED(sample.As(&lease)) || !lease) {
                return H264DecoderError{
                    H264DecoderErrorCode::gpu_output_unavailable};
            }

            output.sample_lease = std::move(lease);
            output.texture = std::move(texture);
            output.subresource_index = subresource;
            output.width = description.Width;
            output.height = description.Height;
            output.format = description.Format;
            output.pts_100ns =
                static_cast<std::int64_t>(sample_time);
            return std::nullopt;
        }

        return H264DecoderError{
            H264DecoderErrorCode::output_failed,
            MF_E_TRANSFORM_STREAM_CHANGE};
    }

    [[nodiscard]] std::optional<H264DecoderError> fail(
        H264DecoderError error) {
        stop();
        return error;
    }

    H264DecoderConfig config_{};
    H264DecoderStatistics stats_{};

    ComPtr<ID3D11Device> device_;
    ComPtr<IMFDXGIDeviceManager> device_manager_;
    ComPtr<IMFTransform> transform_;
    ComPtr<IMFSample> input_sample_;
    ComPtr<IMFMediaBuffer> input_buffer_;
    std::size_t input_capacity_ = 0;

    bool running_ = false;
    bool apartment_initialized_ = false;
    bool media_foundation_started_ = false;
};

WindowsH264D3D11Decoder::WindowsH264D3D11Decoder()
    : impl_(std::make_unique<Impl>()) {}

WindowsH264D3D11Decoder::~WindowsH264D3D11Decoder() {
    impl_->stop();
}

std::optional<H264DecoderError>
WindowsH264D3D11Decoder::start(
    const H264DecoderConfig& config) {
    return impl_->start(config);
}

std::optional<H264DecoderError>
WindowsH264D3D11Decoder::decode(
    std::span<const std::byte> annex_b_access_unit,
    std::int64_t pts_100ns,
    DecodedGpuFrame& output) {
    return impl_->decode(
        annex_b_access_unit, pts_100ns, output);
}

void WindowsH264D3D11Decoder::stop() noexcept {
    impl_->stop();
}

bool WindowsH264D3D11Decoder::running() const noexcept {
    return impl_->running_;
}

H264DecoderStatistics
WindowsH264D3D11Decoder::statistics() const {
    return impl_->stats_;
}

ComPtr<ID3D11Device>
WindowsH264D3D11Decoder::d3d_device() const {
    return impl_->device_;
}

} // namespace catro::platform::windows

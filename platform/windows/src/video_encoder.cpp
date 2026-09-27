#include <catro/platform/windows/video_encoder.hpp>

#include <Windows.h>
#include <codecapi.h>
#include <icodecapi.h>
#include <d3d11_1.h>
#include <dxgi1_6.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mftransform.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>

namespace catro::platform::windows {
namespace {

using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

constexpr auto kEventPollSleep = 1ms;
constexpr auto kOutputTimeout = 250ms;
constexpr auto kWarmupOutputTimeout = 1000ms;
constexpr std::uint32_t kFallbackOutputBytes = 2U * 1024U * 1024U;

std::uint64_t pack_luid(LUID luid) noexcept {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(luid.HighPart)) << 32U) |
           static_cast<std::uint64_t>(luid.LowPart);
}

LUID unpack_luid(std::uint64_t packed) noexcept {
    LUID luid{};
    luid.LowPart = static_cast<DWORD>(packed & 0xffffffffULL);
    luid.HighPart = static_cast<LONG>((packed >> 32U) & 0xffffffffULL);
    return luid;
}

std::uint64_t elapsed_us(Clock::time_point started) noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count());
}

std::variant<std::uint64_t, HardwareEncoderError> texture_adapter_luid(ID3D11Texture2D& texture) {
    ComPtr<ID3D11Device> device;
    texture.GetDevice(&device);
    if (!device) {
        return HardwareEncoderError{HardwareEncoderErrorCode::adapter_mismatch};
    }

    ComPtr<IDXGIDevice> dxgi_device;
    if (const auto result = device.As(&dxgi_device); FAILED(result)) {
        return HardwareEncoderError{HardwareEncoderErrorCode::adapter_mismatch, result};
    }

    ComPtr<IDXGIAdapter> adapter;
    if (const auto result = dxgi_device->GetAdapter(&adapter); FAILED(result)) {
        return HardwareEncoderError{HardwareEncoderErrorCode::adapter_mismatch, result};
    }

    DXGI_ADAPTER_DESC description{};
    if (const auto result = adapter->GetDesc(&description); FAILED(result)) {
        return HardwareEncoderError{HardwareEncoderErrorCode::adapter_mismatch, result};
    }
    return pack_luid(description.AdapterLuid);
}

std::string friendly_name(IMFActivate& activate) {
    wchar_t* raw = nullptr;
    UINT32 length = 0;
    if (FAILED(activate.GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &raw, &length)) ||
        raw == nullptr) {
        return {};
    }

    const std::wstring_view wide(raw, length);
    const auto required = WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string output(static_cast<std::size_t>(std::max(required, 0)), '\0');
    if (required > 0) {
        (void)WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                  output.data(), required, nullptr, nullptr);
    }
    CoTaskMemFree(raw);
    return output;
}

std::variant<ComPtr<IMFTransform>, HardwareEncoderError> activate_h264_encoder(
    std::uint64_t adapter_luid, std::string& encoder_name) {
    ComPtr<IMFAttributes> attributes;
    auto result = MFCreateAttributes(&attributes, 1);
    if (FAILED(result)) {
        return HardwareEncoderError{HardwareEncoderErrorCode::hardware_encoder_activation_failed,
                                    result};
    }

    const auto luid = unpack_luid(adapter_luid);
    result = attributes->SetBlob(MFT_ENUM_ADAPTER_LUID,
                                 reinterpret_cast<const UINT8*>(&luid), sizeof(luid));
    if (FAILED(result)) {
        return HardwareEncoderError{HardwareEncoderErrorCode::hardware_encoder_activation_failed,
                                    result};
    }

    const MFT_REGISTER_TYPE_INFO input{MFMediaType_Video, MFVideoFormat_NV12};
    const MFT_REGISTER_TYPE_INFO output{MFMediaType_Video, MFVideoFormat_H264};
    IMFActivate** activations = nullptr;
    UINT32 count = 0;
    result = MFTEnum2(
        MFT_CATEGORY_VIDEO_ENCODER,
        MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
        &input, &output, attributes.Get(), &activations, &count);
    if (FAILED(result)) {
        return HardwareEncoderError{HardwareEncoderErrorCode::hardware_encoder_activation_failed,
                                    result};
    }

    struct ActivationArray {
        IMFActivate** data = nullptr;
        UINT32 count = 0;
        ~ActivationArray() {
            if (data != nullptr) {
                for (UINT32 index = 0; index < count; ++index) {
                    if (data[index] != nullptr) {
                        data[index]->Release();
                    }
                }
                CoTaskMemFree(data);
            }
        }
    } owned{activations, count};

    if (count == 0 || activations == nullptr) {
        return HardwareEncoderError{HardwareEncoderErrorCode::hardware_encoder_not_found};
    }

    for (UINT32 index = 0; index < count; ++index) {
        ComPtr<IMFTransform> transform;
        result = activations[index]->ActivateObject(IID_PPV_ARGS(&transform));
        if (SUCCEEDED(result) && transform) {
            encoder_name = friendly_name(*activations[index]);
            return transform;
        }
    }

    return HardwareEncoderError{HardwareEncoderErrorCode::hardware_encoder_activation_failed,
                                result};
}

ComPtr<IMFMediaType> make_output_type(const HardwareEncoderConfig& config) {
    ComPtr<IMFMediaType> type;
    if (FAILED(MFCreateMediaType(&type))) {
        return {};
    }
    if (FAILED(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
        FAILED(type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264)) ||
        FAILED(type->SetUINT32(MF_MT_AVG_BITRATE, config.bitrate)) ||
        FAILED(type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive)) ||
        FAILED(MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, config.width, config.height)) ||
        FAILED(MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE,
                                   config.frame_rate_numerator,
                                   config.frame_rate_denominator)) ||
        FAILED(MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1)) ||
        FAILED(type->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_Main))) {
        return {};
    }
    return type;
}

ComPtr<IMFMediaType> make_input_type(const HardwareEncoderConfig& config) {
    ComPtr<IMFMediaType> type;
    if (FAILED(MFCreateMediaType(&type))) {
        return {};
    }
    if (FAILED(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
        FAILED(type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12)) ||
        FAILED(type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive)) ||
        FAILED(MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, config.width, config.height)) ||
        FAILED(MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE,
                                   config.frame_rate_numerator,
                                   config.frame_rate_denominator)) ||
        FAILED(MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1)) ||
        FAILED(type->SetUINT32(MF_MT_VIDEO_PRIMARIES, MFVideoPrimaries_BT709)) ||
        FAILED(type->SetUINT32(MF_MT_TRANSFER_FUNCTION, MFVideoTransFunc_709)) ||
        FAILED(type->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709)) ||
        FAILED(type->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235))) {
        return {};
    }
    return type;
}

bool set_codec_uint32(ICodecAPI& codec, const GUID& key, std::uint32_t value) noexcept {
    VARIANT setting{};
    setting.vt = VT_UI4;
    setting.ulVal = value;
    return SUCCEEDED(codec.SetValue(&key, &setting));
}

bool set_codec_bool(ICodecAPI& codec, const GUID& key, bool value) noexcept {
    VARIANT setting{};
    setting.vt = VT_BOOL;
    setting.boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
    return SUCCEEDED(codec.SetValue(&key, &setting));
}

} // namespace

struct WindowsH264HardwareEncoder::Impl {
    std::optional<HardwareEncoderError> start(
        const HardwareEncoderConfig& next_config, ID3D11Texture2D& first_source) {
        stop();
        stats_ = {};

        if (next_config.width == 0 || next_config.height == 0 ||
            (next_config.width & 1U) != 0 || (next_config.height & 1U) != 0 ||
            next_config.width > 7680 || next_config.height > 4320 ||
            next_config.frame_rate_numerator == 0 || next_config.frame_rate_numerator > 120 ||
            next_config.frame_rate_denominator == 0 ||
            next_config.bitrate < 128'000 || next_config.bitrate > 50'000'000 ||
            next_config.gop_frames == 0) {
            return fail(HardwareEncoderError{HardwareEncoderErrorCode::invalid_config});
        }

        auto luid_result = texture_adapter_luid(first_source);
        if (const auto* error = std::get_if<HardwareEncoderError>(&luid_result)) {
            return fail(*error);
        }
        adapter_luid_ = std::get<std::uint64_t>(luid_result);
        if (next_config.adapter_luid && *next_config.adapter_luid != adapter_luid_) {
            return fail(HardwareEncoderError{HardwareEncoderErrorCode::adapter_mismatch});
        }

        first_source.GetDevice(&device_);
        if (!device_) {
            return fail(HardwareEncoderError{HardwareEncoderErrorCode::adapter_mismatch});
        }
        device_->GetImmediateContext(&context_);
        if (!context_) {
            return fail(HardwareEncoderError{HardwareEncoderErrorCode::video_processor_unavailable});
        }

        auto result = device_.As(&video_device_);
        if (FAILED(result) || !video_device_) {
            return fail(HardwareEncoderError{
                HardwareEncoderErrorCode::video_processor_unavailable, result});
        }
        result = context_.As(&video_context_);
        if (FAILED(result) || !video_context_) {
            return fail(HardwareEncoderError{
                HardwareEncoderErrorCode::video_processor_unavailable, result});
        }
        (void)video_context_.As(&video_context1_);

        config_ = next_config;
        frame_duration_100ns_ =
            static_cast<std::int64_t>((10'000'000ULL * config_.frame_rate_denominator) /
                                      config_.frame_rate_numerator);
        if (frame_duration_100ns_ <= 0) {
            return fail(HardwareEncoderError{HardwareEncoderErrorCode::invalid_config});
        }

        if (const auto error = create_conversion_surface(first_source)) {
            return fail(*error);
        }

        const auto apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) {
            return fail(HardwareEncoderError{
                HardwareEncoderErrorCode::media_foundation_startup_failed, apartment});
        }
        apartment_initialized_ = SUCCEEDED(apartment);

        result = MFStartup(MF_VERSION, MFSTARTUP_LITE);
        if (FAILED(result)) {
            return fail(HardwareEncoderError{
                HardwareEncoderErrorCode::media_foundation_startup_failed, result});
        }
        media_foundation_started_ = true;

        auto transform_result = activate_h264_encoder(adapter_luid_, stats_.encoder_name);
        if (const auto* error = std::get_if<HardwareEncoderError>(&transform_result)) {
            return fail(*error);
        }
        transform_ = std::get<ComPtr<IMFTransform>>(std::move(transform_result));

        ComPtr<IMFAttributes> transform_attributes;
        result = transform_->GetAttributes(&transform_attributes);
        if (FAILED(result) || !transform_attributes) {
            return fail(HardwareEncoderError{
                HardwareEncoderErrorCode::hardware_encoder_activation_failed, result});
        }

        asynchronous_ =
            MFGetAttributeUINT32(transform_attributes.Get(), MF_TRANSFORM_ASYNC, FALSE) != FALSE;
        if (asynchronous_) {
            result = transform_attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
            if (FAILED(result)) {
                return fail(HardwareEncoderError{
                    HardwareEncoderErrorCode::hardware_encoder_activation_failed, result});
            }
            result = transform_.As(&events_);
            if (FAILED(result) || !events_) {
                return fail(HardwareEncoderError{
                    HardwareEncoderErrorCode::hardware_encoder_activation_failed, result});
            }
        }

        d3d11_aware_ =
            MFGetAttributeUINT32(transform_attributes.Get(), MF_SA_D3D11_AWARE, FALSE) != FALSE;
        if (!d3d11_aware_) {
            return fail(HardwareEncoderError{
                HardwareEncoderErrorCode::hardware_encoder_not_d3d11});
        }

        UINT reset_token = 0;
        result = MFCreateDXGIDeviceManager(&reset_token, &device_manager_);
        if (FAILED(result) || !device_manager_) {
            return fail(HardwareEncoderError{
                HardwareEncoderErrorCode::hardware_encoder_activation_failed, result});
        }
        result = device_manager_->ResetDevice(device_.Get(), reset_token);
        if (FAILED(result)) {
            return fail(HardwareEncoderError{
                HardwareEncoderErrorCode::hardware_encoder_activation_failed, result});
        }
        result = transform_->ProcessMessage(
            MFT_MESSAGE_SET_D3D_MANAGER,
            reinterpret_cast<ULONG_PTR>(device_manager_.Get()));
        if (FAILED(result)) {
            return fail(HardwareEncoderError{
                HardwareEncoderErrorCode::hardware_encoder_not_d3d11, result});
        }

        const auto output_type = make_output_type(config_);
        const auto input_type = make_input_type(config_);
        if (!output_type || !input_type ||
            FAILED(transform_->SetOutputType(0, output_type.Get(), 0)) ||
            FAILED(transform_->SetInputType(0, input_type.Get(), 0))) {
            return fail(HardwareEncoderError{HardwareEncoderErrorCode::media_type_failed});
        }

        if (ComPtr<ICodecAPI> codec; SUCCEEDED(transform_.As(&codec)) && codec) {
            stats_.low_latency_requested = true;
            stats_.low_latency_applied =
                set_codec_bool(*codec.Get(), CODECAPI_AVLowLatencyMode, true);
            (void)set_codec_uint32(*codec.Get(), CODECAPI_AVEncCommonMeanBitRate, config_.bitrate);
            (void)set_codec_uint32(*codec.Get(), CODECAPI_AVEncMPVGOPSize, config_.gop_frames);
        }

        MFT_OUTPUT_STREAM_INFO stream_info{};
        result = transform_->GetOutputStreamInfo(0, &stream_info);
        if (FAILED(result)) {
            return fail(HardwareEncoderError{
                HardwareEncoderErrorCode::hardware_encoder_activation_failed, result});
        }
        output_stream_info_ = stream_info;

        if (const auto error = create_input_sample()) {
            return fail(*error);
        }

        if (FAILED(transform_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0)) ||
            FAILED(transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0)) ||
            FAILED(transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0))) {
            return fail(HardwareEncoderError{HardwareEncoderErrorCode::stream_start_failed});
        }

        stats_.adapter_luid = adapter_luid_;
        stats_.asynchronous = asynchronous_;
        stats_.d3d11_aware = d3d11_aware_;
        running_ = true;

        if (asynchronous_) {
            if (const auto error = acquire_input_slot(kOutputTimeout)) {
                return fail(*error);
            }
        }
        return std::nullopt;
    }

    std::optional<HardwareEncoderError> encode(
        const GpuCaptureFrame& source, EncodedAccessUnit& output) {
        if (!running_ || !source.texture) {
            return HardwareEncoderError{HardwareEncoderErrorCode::input_failed};
        }

        const auto frame_luid = texture_adapter_luid(*source.texture.Get());
        if (const auto* error = std::get_if<HardwareEncoderError>(&frame_luid)) {
            return *error;
        }
        if (std::get<std::uint64_t>(frame_luid) != adapter_luid_) {
            return HardwareEncoderError{HardwareEncoderErrorCode::adapter_mismatch};
        }

        if (const auto error = ensure_converter(*source.texture.Get())) {
            ++stats_.conversion_failures;
            return *error;
        }

        const auto conversion_started = Clock::now();
        if (const auto error = convert(*source.texture.Get())) {
            ++stats_.conversion_failures;
            return *error;
        }
        const auto conversion_us = elapsed_us(conversion_started);
        stats_.conversion_total_us += conversion_us;
        stats_.conversion_max_us = std::max(stats_.conversion_max_us, conversion_us);

        if (asynchronous_) {
            if (const auto error = acquire_input_slot(kOutputTimeout)) {
                ++stats_.input_failures;
                return *error;
            }
        }

        if (!input_sample_) {
            ++stats_.input_failures;
            return HardwareEncoderError{HardwareEncoderErrorCode::input_failed};
        }

        const auto pts = static_cast<std::int64_t>(stats_.frames_submitted) * frame_duration_100ns_;
        if (FAILED(input_sample_->SetSampleTime(pts)) ||
            FAILED(input_sample_->SetSampleDuration(frame_duration_100ns_))) {
            ++stats_.input_failures;
            return HardwareEncoderError{HardwareEncoderErrorCode::input_failed};
        }

        const auto encode_started = Clock::now();
        auto result = transform_->ProcessInput(0, input_sample_.Get(), 0);
        if (FAILED(result)) {
            ++stats_.input_failures;
            return HardwareEncoderError{HardwareEncoderErrorCode::input_failed, result};
        }
        if (asynchronous_ && pending_input_requests_ > 0) {
            --pending_input_requests_;
        }
        ++stats_.frames_submitted;

        const auto output_timeout =
            stats_.frames_submitted == 1 ? kWarmupOutputTimeout : kOutputTimeout;
        if (const auto error = collect_output(output, output_timeout)) {
            if (error->code == HardwareEncoderErrorCode::output_timeout) {
                ++stats_.output_timeouts;
            } else {
                ++stats_.output_failures;
            }
            return error;
        }

        const auto encode_us = elapsed_us(encode_started);
        stats_.encode_total_us += encode_us;
        stats_.encode_max_us = std::max(stats_.encode_max_us, encode_us);
        output.sequence = source.sequence;
        ++stats_.frames_encoded;
        stats_.encoded_bytes += output.bytes.size();
        if (output.keyframe) {
            ++stats_.keyframes;
        }
        return std::nullopt;
    }

    void stop() noexcept {
        running_ = false;

        if (transform_) {
            (void)transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
            (void)transform_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
            (void)transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
        }

        events_.Reset();
        transform_.Reset();
        input_sample_.Reset();
        input_surface_buffer_.Reset();
        caller_output_sample_.Reset();
        caller_output_buffer_.Reset();
        device_manager_.Reset();
        processor_.Reset();
        processor_enumerator_.Reset();
        nv12_output_view_.Reset();
        nv12_.Reset();
        video_context1_.Reset();
        video_context_.Reset();
        video_device_.Reset();
        context_.Reset();
        device_.Reset();

        source_width_ = 0;
        source_height_ = 0;
        pending_input_requests_ = 0;
        pending_output_events_ = 0;
        output_stream_info_ = {};
        asynchronous_ = false;
        d3d11_aware_ = false;

        if (media_foundation_started_) {
            (void)MFShutdown();
            media_foundation_started_ = false;
        }
        if (apartment_initialized_) {
            CoUninitialize();
            apartment_initialized_ = false;
        }
    }

    std::optional<HardwareEncoderError> create_conversion_surface(ID3D11Texture2D& source) {
        D3D11_TEXTURE2D_DESC source_description{};
        source.GetDesc(&source_description);
        if (source_description.Width == 0 || source_description.Height == 0 ||
            source_description.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
            return HardwareEncoderError{
                HardwareEncoderErrorCode::video_processor_unavailable};
        }

        source_width_ = source_description.Width;
        source_height_ = source_description.Height;
        if (static_cast<std::uint64_t>(source_width_) * config_.height !=
            static_cast<std::uint64_t>(source_height_) * config_.width) {
            // Screen sharing must not stretch UI/text. The media policy chooses an aspect-matched
            // encode size; letterbox/crop policy belongs above this primitive.
            return HardwareEncoderError{HardwareEncoderErrorCode::invalid_config};
        }

        D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
        content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        content.InputFrameRate.Numerator = config_.frame_rate_numerator;
        content.InputFrameRate.Denominator = config_.frame_rate_denominator;
        content.InputWidth = source_width_;
        content.InputHeight = source_height_;
        content.OutputFrameRate.Numerator = config_.frame_rate_numerator;
        content.OutputFrameRate.Denominator = config_.frame_rate_denominator;
        content.OutputWidth = config_.width;
        content.OutputHeight = config_.height;
        content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

        auto result = video_device_->CreateVideoProcessorEnumerator(
            &content, &processor_enumerator_);
        if (FAILED(result) || !processor_enumerator_) {
            return HardwareEncoderError{
                HardwareEncoderErrorCode::video_processor_unavailable, result};
        }

        UINT bgra_support = 0;
        UINT nv12_support = 0;
        if (FAILED(processor_enumerator_->CheckVideoProcessorFormat(
                DXGI_FORMAT_B8G8R8A8_UNORM, &bgra_support)) ||
            FAILED(processor_enumerator_->CheckVideoProcessorFormat(
                DXGI_FORMAT_NV12, &nv12_support)) ||
            (bgra_support & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT) == 0 ||
            (nv12_support & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT) == 0) {
            return HardwareEncoderError{
                HardwareEncoderErrorCode::video_processor_unavailable};
        }

        result = video_device_->CreateVideoProcessor(
            processor_enumerator_.Get(), 0, &processor_);
        if (FAILED(result) || !processor_) {
            return HardwareEncoderError{
                HardwareEncoderErrorCode::video_processor_unavailable, result};
        }

        D3D11_TEXTURE2D_DESC target{};
        target.Width = config_.width;
        target.Height = config_.height;
        target.MipLevels = 1;
        target.ArraySize = 1;
        target.Format = DXGI_FORMAT_NV12;
        target.SampleDesc.Count = 1;
        target.Usage = D3D11_USAGE_DEFAULT;
        target.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_VIDEO_ENCODER;

        result = device_->CreateTexture2D(&target, nullptr, &nv12_);
        if (FAILED(result) || !nv12_) {
            return HardwareEncoderError{
                HardwareEncoderErrorCode::conversion_surface_failed, result};
        }

        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC output_view{};
        output_view.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        output_view.Texture2D.MipSlice = 0;
        result = video_device_->CreateVideoProcessorOutputView(
            nv12_.Get(), processor_enumerator_.Get(), &output_view, &nv12_output_view_);
        if (FAILED(result) || !nv12_output_view_) {
            return HardwareEncoderError{
                HardwareEncoderErrorCode::conversion_surface_failed, result};
        }

        if (video_context1_) {
            video_context1_->VideoProcessorSetStreamColorSpace1(
                processor_.Get(), 0, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
            video_context1_->VideoProcessorSetOutputColorSpace1(
                processor_.Get(), DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709);
            stats_.explicit_bt709_conversion = true;
        }

        return std::nullopt;
    }

    std::optional<HardwareEncoderError> ensure_converter(ID3D11Texture2D& source) {
        D3D11_TEXTURE2D_DESC description{};
        source.GetDesc(&description);
        if (description.Width == source_width_ && description.Height == source_height_) {
            return std::nullopt;
        }

        input_sample_.Reset();
        input_surface_buffer_.Reset();
        processor_.Reset();
        processor_enumerator_.Reset();
        nv12_output_view_.Reset();
        nv12_.Reset();

        if (const auto error = create_conversion_surface(source)) {
            return error;
        }
        if (media_foundation_started_) {
            return create_input_sample();
        }
        return std::nullopt;
    }

    std::optional<HardwareEncoderError> create_input_sample() {
        input_sample_.Reset();
        input_surface_buffer_.Reset();

        auto result = MFCreateDXGISurfaceBuffer(
            __uuidof(ID3D11Texture2D), nv12_.Get(), 0, FALSE, &input_surface_buffer_);
        if (FAILED(result) || !input_surface_buffer_) {
            return HardwareEncoderError{HardwareEncoderErrorCode::input_failed, result};
        }

        result = MFCreateSample(&input_sample_);
        if (FAILED(result) || !input_sample_) {
            input_surface_buffer_.Reset();
            return HardwareEncoderError{HardwareEncoderErrorCode::input_failed, result};
        }

        result = input_sample_->AddBuffer(input_surface_buffer_.Get());
        if (FAILED(result)) {
            input_sample_.Reset();
            input_surface_buffer_.Reset();
            return HardwareEncoderError{HardwareEncoderErrorCode::input_failed, result};
        }

        ++stats_.input_sample_allocations;
        return std::nullopt;
    }

    std::optional<HardwareEncoderError> convert(ID3D11Texture2D& source) {
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_view_description{};
        input_view_description.FourCC = 0;
        input_view_description.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        input_view_description.Texture2D.MipSlice = 0;
        input_view_description.Texture2D.ArraySlice = 0;

        ComPtr<ID3D11VideoProcessorInputView> input_view;
        auto result = video_device_->CreateVideoProcessorInputView(
            &source, processor_enumerator_.Get(), &input_view_description, &input_view);
        if (FAILED(result) || !input_view) {
            return HardwareEncoderError{
                HardwareEncoderErrorCode::video_processor_unavailable, result};
        }

        const RECT source_rect{
            0, 0, static_cast<LONG>(source_width_), static_cast<LONG>(source_height_)};
        const RECT destination_rect{
            0, 0, static_cast<LONG>(config_.width), static_cast<LONG>(config_.height)};
        video_context_->VideoProcessorSetStreamFrameFormat(
            processor_.Get(), 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
        video_context_->VideoProcessorSetStreamSourceRect(
            processor_.Get(), 0, TRUE, &source_rect);
        video_context_->VideoProcessorSetStreamDestRect(
            processor_.Get(), 0, TRUE, &destination_rect);
        video_context_->VideoProcessorSetOutputTargetRect(
            processor_.Get(), TRUE, &destination_rect);

        D3D11_VIDEO_PROCESSOR_STREAM stream{};
        stream.Enable = TRUE;
        stream.pInputSurface = input_view.Get();

        result = video_context_->VideoProcessorBlt(
            processor_.Get(), nv12_output_view_.Get(), 0, 1, &stream);
        if (FAILED(result)) {
            return HardwareEncoderError{
                HardwareEncoderErrorCode::video_processor_unavailable, result};
        }
        return std::nullopt;
    }

    void observe_event(MediaEventType type) noexcept {
        if (type == METransformNeedInput) {
            ++pending_input_requests_;
        } else if (type == METransformHaveOutput) {
            ++pending_output_events_;
        }
    }

    std::optional<HardwareEncoderError> pump_event_until(
        bool input, std::chrono::milliseconds timeout) {
        const auto deadline = Clock::now() + timeout;
        while (Clock::now() < deadline) {
            if ((input && pending_input_requests_ > 0) ||
                (!input && pending_output_events_ > 0)) {
                return std::nullopt;
            }

            ComPtr<IMFMediaEvent> event;
            const auto result = events_->GetEvent(MF_EVENT_FLAG_NO_WAIT, &event);
            if (result == MF_E_NO_EVENTS_AVAILABLE) {
                std::this_thread::sleep_for(kEventPollSleep);
                continue;
            }
            if (FAILED(result) || !event) {
                return HardwareEncoderError{
                    input ? HardwareEncoderErrorCode::input_failed
                          : HardwareEncoderErrorCode::output_failed,
                    result};
            }

            MediaEventType type = MEUnknown;
            if (FAILED(event->GetType(&type))) {
                return HardwareEncoderError{
                    input ? HardwareEncoderErrorCode::input_failed
                          : HardwareEncoderErrorCode::output_failed};
            }
            observe_event(type);
            if (type == MEError) {
                HRESULT status = S_OK;
                (void)event->GetStatus(&status);
                return HardwareEncoderError{
                    input ? HardwareEncoderErrorCode::input_failed
                          : HardwareEncoderErrorCode::output_failed,
                    status};
            }
        }
        return HardwareEncoderError{
            input ? HardwareEncoderErrorCode::input_failed
                  : HardwareEncoderErrorCode::output_timeout};
    }

    std::optional<HardwareEncoderError> acquire_input_slot(
        std::chrono::milliseconds timeout) {
        if (!asynchronous_) {
            return std::nullopt;
        }
        return pump_event_until(true, timeout);
    }

    std::variant<ComPtr<IMFSample>, HardwareEncoderError> make_output_sample() {
        if ((output_stream_info_.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) != 0) {
            return ComPtr<IMFSample>{};
        }

        if (!caller_output_sample_) {
            ComPtr<IMFSample> sample;
            auto result = MFCreateSample(&sample);
            if (FAILED(result) || !sample) {
                return HardwareEncoderError{HardwareEncoderErrorCode::output_failed, result};
            }

            const auto bytes =
                std::max<std::uint32_t>(output_stream_info_.cbSize, kFallbackOutputBytes);
            ComPtr<IMFMediaBuffer> buffer;
            result = MFCreateMemoryBuffer(bytes, &buffer);
            if (FAILED(result) || !buffer) {
                return HardwareEncoderError{HardwareEncoderErrorCode::output_failed, result};
            }
            result = sample->AddBuffer(buffer.Get());
            if (FAILED(result)) {
                return HardwareEncoderError{HardwareEncoderErrorCode::output_failed, result};
            }

            caller_output_sample_ = std::move(sample);
            caller_output_buffer_ = std::move(buffer);
            ++stats_.output_sample_allocations;
        }

        if (!caller_output_buffer_ ||
            FAILED(caller_output_buffer_->SetCurrentLength(0))) {
            return HardwareEncoderError{HardwareEncoderErrorCode::output_failed};
        }
        return caller_output_sample_;
    }

    std::optional<HardwareEncoderError> collect_output(
        EncodedAccessUnit& output, std::chrono::milliseconds timeout) {
        if (asynchronous_) {
            if (const auto error = pump_event_until(false, timeout)) {
                return error;
            }
        }

        auto sample_result = make_output_sample();
        if (const auto* error = std::get_if<HardwareEncoderError>(&sample_result)) {
            return *error;
        }

        auto sample = std::get<ComPtr<IMFSample>>(std::move(sample_result));
        MFT_OUTPUT_DATA_BUFFER data{};
        data.dwStreamID = 0;
        data.pSample = sample.Get();
        DWORD status = 0;

        auto result = transform_->ProcessOutput(0, 1, &data, &status);
        if (result == MF_E_TRANSFORM_NEED_MORE_INPUT && !asynchronous_) {
            return HardwareEncoderError{HardwareEncoderErrorCode::output_timeout, result};
        }
        if (FAILED(result)) {
            if (data.pEvents != nullptr) {
                data.pEvents->Release();
            }
            return HardwareEncoderError{HardwareEncoderErrorCode::output_failed, result};
        }
        if (asynchronous_ && pending_output_events_ > 0) {
            --pending_output_events_;
        }

        if (data.pSample != nullptr && data.pSample != sample.Get()) {
            // ProcessOutput transfers ownership of a transform-provided sample to the caller.
            sample.Attach(data.pSample);
        }
        if (data.pEvents != nullptr) {
            data.pEvents->Release();
        }
        if (!sample) {
            return HardwareEncoderError{HardwareEncoderErrorCode::output_failed};
        }

        ComPtr<IMFMediaBuffer> contiguous;
        result = sample->ConvertToContiguousBuffer(&contiguous);
        if (FAILED(result) || !contiguous) {
            return HardwareEncoderError{HardwareEncoderErrorCode::output_failed, result};
        }

        BYTE* bytes = nullptr;
        DWORD current_length = 0;
        result = contiguous->Lock(&bytes, nullptr, &current_length);
        if (FAILED(result) || bytes == nullptr) {
            return HardwareEncoderError{HardwareEncoderErrorCode::output_failed, result};
        }

        try {
            output.bytes.resize(current_length);
            if (current_length > 0) {
                std::memcpy(output.bytes.data(), bytes, current_length);
            }
        } catch (...) {
            contiguous->Unlock();
            return HardwareEncoderError{HardwareEncoderErrorCode::output_failed, E_OUTOFMEMORY};
        }
        contiguous->Unlock();

        LONGLONG pts = 0;
        LONGLONG duration = 0;
        (void)sample->GetSampleTime(&pts);
        (void)sample->GetSampleDuration(&duration);
        output.pts_100ns = pts;
        output.duration_100ns = duration;
        output.keyframe =
            MFGetAttributeUINT32(sample.Get(), MFSampleExtension_CleanPoint, FALSE) != FALSE;
        return std::nullopt;
    }

    std::optional<HardwareEncoderError> fail(HardwareEncoderError error) {
        stop();
        return error;
    }

    HardwareEncoderConfig config_{};
    HardwareEncoderStatistics stats_{};
    bool running_ = false;
    bool media_foundation_started_ = false;
    bool apartment_initialized_ = false;
    bool asynchronous_ = false;
    bool d3d11_aware_ = false;
    std::uint64_t adapter_luid_ = 0;
    std::uint32_t source_width_ = 0;
    std::uint32_t source_height_ = 0;
    std::int64_t frame_duration_100ns_ = 0;
    std::uint32_t pending_input_requests_ = 0;
    std::uint32_t pending_output_events_ = 0;

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11VideoDevice> video_device_;
    ComPtr<ID3D11VideoContext> video_context_;
    ComPtr<ID3D11VideoContext1> video_context1_;
    ComPtr<ID3D11VideoProcessorEnumerator> processor_enumerator_;
    ComPtr<ID3D11VideoProcessor> processor_;
    ComPtr<ID3D11Texture2D> nv12_;
    ComPtr<ID3D11VideoProcessorOutputView> nv12_output_view_;

    ComPtr<IMFDXGIDeviceManager> device_manager_;
    ComPtr<IMFTransform> transform_;
    ComPtr<IMFMediaEventGenerator> events_;
    ComPtr<IMFSample> input_sample_;
    ComPtr<IMFMediaBuffer> input_surface_buffer_;
    ComPtr<IMFSample> caller_output_sample_;
    ComPtr<IMFMediaBuffer> caller_output_buffer_;
    MFT_OUTPUT_STREAM_INFO output_stream_info_{};
};

WindowsH264HardwareEncoder::WindowsH264HardwareEncoder()
    : impl_(std::make_unique<Impl>()) {}

WindowsH264HardwareEncoder::~WindowsH264HardwareEncoder() {
    stop();
}

std::optional<HardwareEncoderError> WindowsH264HardwareEncoder::start(
    const HardwareEncoderConfig& config, ID3D11Texture2D& first_source) {
    return impl_->start(config, first_source);
}

std::optional<HardwareEncoderError> WindowsH264HardwareEncoder::encode(
    const GpuCaptureFrame& source, EncodedAccessUnit& output) {
    return impl_->encode(source, output);
}

void WindowsH264HardwareEncoder::stop() noexcept {
    impl_->stop();
}

bool WindowsH264HardwareEncoder::running() const noexcept {
    return impl_->running_;
}

HardwareEncoderStatistics WindowsH264HardwareEncoder::statistics() const {
    return impl_->stats_;
}

} // namespace catro::platform::windows

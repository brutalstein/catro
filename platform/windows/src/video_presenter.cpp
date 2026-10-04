#include <catro/platform/windows/video_presenter.hpp>

#include <catro/video/geometry.hpp>

#include <d3d11_1.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

namespace catro::platform::windows {
namespace {

using Microsoft::WRL::ComPtr;

constexpr std::size_t kInputViewCache = 4;

struct CachedInputView {
    ID3D11Texture2D* texture = nullptr;
    std::uint32_t subresource = 0;
    ComPtr<ID3D11VideoProcessorInputView> view;
};

[[nodiscard]] bool supported_input_format(DXGI_FORMAT format) noexcept {
    return format == DXGI_FORMAT_B8G8R8A8_UNORM ||
           format == DXGI_FORMAT_NV12;
}

} // namespace

struct D3D11CompositionVideoPresenter::Impl {
    explicit Impl(VideoPresenterConfig config) : config_(config) {}

    [[nodiscard]] std::optional<VideoPresenterError> present(
        ID3D11Texture2D& source,
        std::uint32_t subresource_index,
        std::uint32_t visible_width,
        std::uint32_t visible_height) {
        D3D11_TEXTURE2D_DESC description{};
        source.GetDesc(&description);
        // From here on the description names the visible picture, not the padded texture.
        if (visible_width != 0 && visible_height != 0) {
            description.Width = std::min<UINT>(description.Width, visible_width);
            description.Height = std::min<UINT>(description.Height, visible_height);
        }
        if (description.Width == 0 || description.Height == 0 ||
            !supported_input_format(description.Format) ||
            description.MipLevels == 0 ||
            subresource_index >= description.MipLevels * description.ArraySize) {
            return VideoPresenterError{VideoPresenterErrorCode::invalid_source};
        }

        ComPtr<ID3D11Device> source_device;
        source.GetDevice(&source_device);
        if (!source_device) {
            return VideoPresenterError{VideoPresenterErrorCode::invalid_source};
        }

        const bool needs_reconfigure =
            !device_ ||
            device_.Get() != source_device.Get() ||
            description.Width != stats_.source_width ||
            description.Height != stats_.source_height ||
            description.Format != stats_.source_format;

        if (needs_reconfigure) {
            if (const auto error = configure(
                    *source_device.Get(), description)) {
                return error;
            }
        }

        auto input_view = input_view_for(source, subresource_index);
        if (!input_view) {
            return VideoPresenterError{
                VideoPresenterErrorCode::video_processor_unavailable};
        }

        const auto back_index = swap_chain3_->GetCurrentBackBufferIndex();
        if (back_index >= output_views_.size() ||
            !output_views_[back_index]) {
            return VideoPresenterError{
                VideoPresenterErrorCode::swap_chain_failed};
        }

        const RECT source_rect{
            0,
            0,
            static_cast<LONG>(stats_.source_width),
            static_cast<LONG>(stats_.source_height),
        };
        const RECT output_rect{
            0,
            0,
            static_cast<LONG>(stats_.output_width),
            static_cast<LONG>(stats_.output_height),
        };

        video_context_->VideoProcessorSetStreamFrameFormat(
            processor_.Get(), 0,
            D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
        video_context_->VideoProcessorSetStreamSourceRect(
            processor_.Get(), 0, TRUE, &source_rect);
        video_context_->VideoProcessorSetStreamDestRect(
            processor_.Get(), 0, TRUE, &output_rect);
        video_context_->VideoProcessorSetOutputTargetRect(
            processor_.Get(), TRUE, &output_rect);

        if (video_context1_) {
            const auto input_color =
                stats_.source_format == DXGI_FORMAT_NV12
                    ? DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709
                    : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
            video_context1_->VideoProcessorSetStreamColorSpace1(
                processor_.Get(), 0, input_color);
            video_context1_->VideoProcessorSetOutputColorSpace1(
                processor_.Get(),
                DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
        }

        D3D11_VIDEO_PROCESSOR_STREAM stream{};
        stream.Enable = TRUE;
        stream.pInputSurface = input_view.Get();

        auto result = video_context_->VideoProcessorBlt(
            processor_.Get(),
            output_views_[back_index].Get(),
            0,
            1,
            &stream);
        if (FAILED(result)) {
            return VideoPresenterError{
                VideoPresenterErrorCode::video_processor_unavailable,
                result};
        }

        result = swap_chain_->Present(
            0, DXGI_PRESENT_DO_NOT_WAIT);
        if (result == DXGI_ERROR_WAS_STILL_DRAWING) {
            ++stats_.frames_dropped;
            return std::nullopt;
        }
        if (FAILED(result)) {
            return VideoPresenterError{
                VideoPresenterErrorCode::present_failed,
                result};
        }

        ++stats_.frames_presented;
        return std::nullopt;
    }

    [[nodiscard]] std::optional<VideoPresenterError> configure(
        ID3D11Device& next_device,
        const D3D11_TEXTURE2D_DESC& source) {
        reset_gpu();

        if (config_.max_width < 2 || config_.max_height < 2 ||
            config_.frame_rate == 0 || config_.frame_rate > 240) {
            return VideoPresenterError{
                VideoPresenterErrorCode::invalid_source};
        }

        const auto extent = video::fit_even_video_extent(
            source.Width,
            source.Height,
            config_.max_width,
            config_.max_height);
        if (!extent) {
            return VideoPresenterError{
                VideoPresenterErrorCode::invalid_source};
        }

        device_ = &next_device;
        device_->GetImmediateContext(&context_);
        if (!context_) {
            reset_gpu();
            return VideoPresenterError{
                VideoPresenterErrorCode::video_processor_unavailable};
        }

        auto result = device_.As(&video_device_);
        if (FAILED(result) || !video_device_) {
            reset_gpu();
            return VideoPresenterError{
                VideoPresenterErrorCode::video_processor_unavailable,
                result};
        }
        result = context_.As(&video_context_);
        if (FAILED(result) || !video_context_) {
            reset_gpu();
            return VideoPresenterError{
                VideoPresenterErrorCode::video_processor_unavailable,
                result};
        }
        (void)video_context_.As(&video_context1_);

        D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
        content.InputFrameFormat =
            D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        content.InputFrameRate.Numerator = config_.frame_rate;
        content.InputFrameRate.Denominator = 1;
        content.InputWidth = source.Width;
        content.InputHeight = source.Height;
        content.OutputFrameRate.Numerator = config_.frame_rate;
        content.OutputFrameRate.Denominator = 1;
        content.OutputWidth = extent->width;
        content.OutputHeight = extent->height;
        content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

        result = video_device_->CreateVideoProcessorEnumerator(
            &content, &processor_enumerator_);
        if (FAILED(result) || !processor_enumerator_) {
            reset_gpu();
            return VideoPresenterError{
                VideoPresenterErrorCode::video_processor_unavailable,
                result};
        }

        UINT input_support = 0;
        UINT output_support = 0;
        if (FAILED(processor_enumerator_->CheckVideoProcessorFormat(
                source.Format, &input_support)) ||
            FAILED(processor_enumerator_->CheckVideoProcessorFormat(
                DXGI_FORMAT_B8G8R8A8_UNORM, &output_support)) ||
            (input_support &
             D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT) == 0 ||
            (output_support &
             D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT) == 0) {
            reset_gpu();
            return VideoPresenterError{
                VideoPresenterErrorCode::video_processor_unavailable};
        }

        result = video_device_->CreateVideoProcessor(
            processor_enumerator_.Get(), 0, &processor_);
        if (FAILED(result) || !processor_) {
            reset_gpu();
            return VideoPresenterError{
                VideoPresenterErrorCode::video_processor_unavailable,
                result};
        }

        ComPtr<IDXGIDevice> dxgi_device;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<IDXGIFactory2> factory;
        if (FAILED(device_.As(&dxgi_device)) ||
            FAILED(dxgi_device->GetAdapter(&adapter)) ||
            FAILED(adapter->GetParent(IID_PPV_ARGS(&factory))) ||
            !factory) {
            reset_gpu();
            return VideoPresenterError{
                VideoPresenterErrorCode::swap_chain_failed};
        }

        DXGI_SWAP_CHAIN_DESC1 swap_desc{};
        swap_desc.Width = extent->width;
        swap_desc.Height = extent->height;
        swap_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        swap_desc.SampleDesc.Count = 1;
        swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_desc.BufferCount = 2;
        swap_desc.Scaling = DXGI_SCALING_STRETCH;
        swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        swap_desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

        result = factory->CreateSwapChainForComposition(
            device_.Get(), &swap_desc, nullptr, &swap_chain_);
        if (FAILED(result) || !swap_chain_) {
            reset_gpu();
            return VideoPresenterError{
                VideoPresenterErrorCode::swap_chain_failed,
                result};
        }

        result = swap_chain_.As(&swap_chain3_);
        if (FAILED(result) || !swap_chain3_) {
            reset_gpu();
            return VideoPresenterError{
                VideoPresenterErrorCode::swap_chain_failed,
                result};
        }

        for (UINT index = 0;
             index < static_cast<UINT>(output_views_.size());
             ++index) {
            ComPtr<ID3D11Texture2D> back_buffer;
            result = swap_chain_->GetBuffer(
                index, IID_PPV_ARGS(&back_buffer));
            if (FAILED(result) || !back_buffer) {
                reset_gpu();
                return VideoPresenterError{
                    VideoPresenterErrorCode::swap_chain_failed,
                    result};
            }

            D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC output_desc{};
            output_desc.ViewDimension =
                D3D11_VPOV_DIMENSION_TEXTURE2D;
            output_desc.Texture2D.MipSlice = 0;
            result = video_device_->CreateVideoProcessorOutputView(
                back_buffer.Get(),
                processor_enumerator_.Get(),
                &output_desc,
                &output_views_[index]);
            if (FAILED(result) || !output_views_[index]) {
                reset_gpu();
                return VideoPresenterError{
                    VideoPresenterErrorCode::video_processor_unavailable,
                    result};
            }
        }

        stats_.source_width = source.Width;
        stats_.source_height = source.Height;
        stats_.source_format = source.Format;
        stats_.output_width = extent->width;
        stats_.output_height = extent->height;
        ++stats_.reconfigurations;
        return std::nullopt;
    }

    [[nodiscard]] ComPtr<ID3D11VideoProcessorInputView> input_view_for(
        ID3D11Texture2D& source,
        std::uint32_t subresource) {
        for (auto& cached : input_views_) {
            if (cached.texture == &source &&
                cached.subresource == subresource &&
                cached.view) {
                return cached.view;
            }
        }

        D3D11_TEXTURE2D_DESC description{};
        source.GetDesc(&description);
        if (description.MipLevels == 0) {
            return {};
        }

        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_desc{};
        input_desc.FourCC = 0;
        input_desc.ViewDimension =
            D3D11_VPIV_DIMENSION_TEXTURE2D;
        input_desc.Texture2D.MipSlice =
            subresource % description.MipLevels;
        input_desc.Texture2D.ArraySlice =
            subresource / description.MipLevels;

        ComPtr<ID3D11VideoProcessorInputView> view;
        if (FAILED(video_device_->CreateVideoProcessorInputView(
                &source,
                processor_enumerator_.Get(),
                &input_desc,
                &view)) ||
            !view) {
            return {};
        }

        auto& slot = input_views_[next_input_slot_];
        slot.texture = &source;
        slot.subresource = subresource;
        slot.view = view;
        next_input_slot_ =
            (next_input_slot_ + 1U) % input_views_.size();
        return view;
    }

    void reset_gpu() noexcept {
        for (auto& input : input_views_) {
            input = {};
        }
        next_input_slot_ = 0;
        for (auto& output : output_views_) {
            output.Reset();
        }
        swap_chain3_.Reset();
        swap_chain_.Reset();
        processor_.Reset();
        processor_enumerator_.Reset();
        video_context1_.Reset();
        video_context_.Reset();
        video_device_.Reset();
        context_.Reset();
        device_.Reset();

        stats_.source_width = 0;
        stats_.source_height = 0;
        stats_.output_width = 0;
        stats_.output_height = 0;
        stats_.source_format = DXGI_FORMAT_UNKNOWN;
    }

    void reset() noexcept {
        reset_gpu();
        stats_ = {};
    }

    VideoPresenterConfig config_{};
    VideoPresenterStatistics stats_{};

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11VideoDevice> video_device_;
    ComPtr<ID3D11VideoContext> video_context_;
    ComPtr<ID3D11VideoContext1> video_context1_;
    ComPtr<ID3D11VideoProcessorEnumerator> processor_enumerator_;
    ComPtr<ID3D11VideoProcessor> processor_;
    ComPtr<IDXGISwapChain1> swap_chain_;
    ComPtr<IDXGISwapChain3> swap_chain3_;

    std::array<CachedInputView, kInputViewCache> input_views_{};
    std::size_t next_input_slot_ = 0;
    std::array<ComPtr<ID3D11VideoProcessorOutputView>, 2>
        output_views_{};
};

D3D11CompositionVideoPresenter::D3D11CompositionVideoPresenter(
    VideoPresenterConfig config)
    : impl_(std::make_unique<Impl>(config)) {}

D3D11CompositionVideoPresenter::~D3D11CompositionVideoPresenter() {
    impl_->reset();
}

std::optional<VideoPresenterError>
D3D11CompositionVideoPresenter::present(
    ID3D11Texture2D& source,
    std::uint32_t subresource_index,
    std::uint32_t visible_width,
    std::uint32_t visible_height) {
    return impl_->present(source, subresource_index, visible_width, visible_height);
}

void D3D11CompositionVideoPresenter::reset() noexcept {
    impl_->reset();
}

ComPtr<IDXGISwapChain1>
D3D11CompositionVideoPresenter::swap_chain() const {
    return impl_->swap_chain_;
}

VideoPresenterStatistics
D3D11CompositionVideoPresenter::statistics() const noexcept {
    return impl_->stats_;
}

} // namespace catro::platform::windows

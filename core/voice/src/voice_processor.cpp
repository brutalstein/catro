#include <catro/voice/voice_processor.hpp>

#include "api/audio/audio_processing.h"
#include "api/scoped_refptr.h"

#include <utility>

namespace catro::voice {
namespace {

// AudioProcessing works on 10 ms chunks; a 20 ms codec frame is two of them.
constexpr std::size_t kChunkSamples = kFrameSamples / 2;

[[nodiscard]] webrtc::AudioProcessing::Config native_config(const VoiceProcessingConfig& config) noexcept {
    webrtc::AudioProcessing::Config native;
    native.high_pass_filter.enabled = true;
    native.echo_canceller.enabled = config.echo_cancellation;
    native.noise_suppression.enabled = config.noise_suppression;
    native.noise_suppression.level = webrtc::AudioProcessing::Config::NoiseSuppression::kHigh;
    native.gain_controller2.enabled = config.automatic_gain;
    native.gain_controller2.adaptive_digital.enabled = config.automatic_gain;
    return native;
}

} // namespace

struct VoiceProcessor::Impl {
    rtc::scoped_refptr<webrtc::AudioProcessing> apm;
    webrtc::StreamConfig stream{static_cast<int>(kSampleRate), 1};
};

VoiceProcessor::VoiceProcessor(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

VoiceProcessor::~VoiceProcessor() = default;

std::unique_ptr<VoiceProcessor> VoiceProcessor::create(const VoiceProcessingConfig& config) noexcept {
    try {
        auto impl = std::make_unique<Impl>();
        impl->apm = webrtc::AudioProcessingBuilder().SetConfig(native_config(config)).Create();
        if (!impl->apm) {
            return nullptr;
        }
        return std::unique_ptr<VoiceProcessor>(new VoiceProcessor(std::move(impl)));
    } catch (...) {
        return nullptr;
    }
}

void VoiceProcessor::set_config(const VoiceProcessingConfig& config) noexcept {
    impl_->apm->ApplyConfig(native_config(config));
}

void VoiceProcessor::analyze_render(const PcmFrame& frame) noexcept {
    for (std::size_t offset = 0; offset < frame.size(); offset += kChunkSamples) {
        const float* source = frame.data() + offset;
        // Reverse output is unused; AudioProcessing only analyzes the far-end signal here.
        float scratch[kChunkSamples];
        float* destination = scratch;
        (void)impl_->apm->ProcessReverseStream(&source, impl_->stream, impl_->stream, &destination);
    }
}

void VoiceProcessor::process_capture(PcmFrame& frame) noexcept {
    for (std::size_t offset = 0; offset < frame.size(); offset += kChunkSamples) {
        float* samples = frame.data() + offset;
        const float* source = samples;
        // In place: on failure AudioProcessing leaves the input untouched, so voice still flows.
        (void)impl_->apm->ProcessStream(&source, impl_->stream, impl_->stream, &samples);
    }
}

} // namespace catro::voice

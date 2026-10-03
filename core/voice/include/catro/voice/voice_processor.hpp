#pragma once

#include <catro/voice/audio_bridge.hpp>

#include <memory>

namespace catro::voice {

struct VoiceProcessingConfig {
    bool echo_cancellation = true;
    bool noise_suppression = true;
    bool automatic_gain = true;

    friend bool operator==(const VoiceProcessingConfig&, const VoiceProcessingConfig&) = default;
};

// WebRTC AudioProcessing (AEC3, noise suppression, AGC2, high-pass filter) for one 48 kHz mono
// voice. Runs on voice worker threads, never in an audio callback: render and capture may be
// called from different threads, and set_config from any thread.
class VoiceProcessor final {
public:
    // nullptr if the processing module cannot be created; voice then flows unprocessed.
    [[nodiscard]] static std::unique_ptr<VoiceProcessor> create(const VoiceProcessingConfig& config) noexcept;
    ~VoiceProcessor();

    VoiceProcessor(const VoiceProcessor&) = delete;
    VoiceProcessor& operator=(const VoiceProcessor&) = delete;

    void set_config(const VoiceProcessingConfig& config) noexcept;
    // Far-end reference: exactly what is about to be played on the speakers.
    void analyze_render(const PcmFrame& frame) noexcept;
    // Near-end microphone frame, cleaned in place before encoding.
    void process_capture(PcmFrame& frame) noexcept;

private:
    struct Impl;
    explicit VoiceProcessor(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

} // namespace catro::voice

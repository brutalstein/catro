#pragma once

#include <catro/audio/engine.hpp>

namespace catro::platform::macos {

// CoreAudio AUHAL streams. Endpoints are the capability system's "coreaudio:<uid>:input|output"
// identifiers; the default is the system default input or output device.
//
// Microphone access: a denied or restricted status fails capture with permission_denied. An
// undetermined status lets the stream open; starting it makes macOS show its own prompt, and the
// stream delivers silence until the user answers. Nothing here prompts on its own.
class CoreAudioPlatform final : public audio::AudioPlatform {
public:
    [[nodiscard]] audio::OpenResult open_capture(const std::optional<capabilities::AudioEndpointId>& device,
                                                 audio::CaptureSink& sink, audio::StreamFailure failure) override;
    [[nodiscard]] audio::OpenResult open_render(const std::optional<capabilities::AudioEndpointId>& device,
                                                audio::RenderSource& source, audio::StreamFailure failure) override;
    [[nodiscard]] std::optional<capabilities::AudioEndpointId> default_device(audio::DeviceDirection direction) override;
};

} // namespace catro::platform::macos

#pragma once

#include <catro/audio/engine.hpp>

namespace catro::platform::windows {

// WASAPI shared-mode, event-driven streams. Each stream owns an MTA thread registered with MMCSS
// ("Pro Audio"); activation, initialization, the real-time loop, and shutdown all run on it.
// Endpoints are the capability system's "mmdevice:<id>" identifiers; the default is the
// communications endpoint.
class WasapiAudioPlatform final : public audio::AudioPlatform {
public:
    [[nodiscard]] audio::OpenResult open_capture(const std::optional<capabilities::AudioEndpointId>& device,
                                                 audio::CaptureSink& sink, audio::StreamFailure failure) override;
    [[nodiscard]] audio::OpenResult open_render(const std::optional<capabilities::AudioEndpointId>& device,
                                                audio::RenderSource& source, audio::StreamFailure failure) override;
    [[nodiscard]] std::optional<capabilities::AudioEndpointId> default_device(audio::DeviceDirection direction) override;
};

} // namespace catro::platform::windows

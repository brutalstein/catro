#include <catro/platform/macos/capability_service.hpp>

#include <chrono>

namespace catro::platform::macos {

namespace caps = catro::capabilities;

caps::ProbeFragment run_system_probe(const caps::ProbeSpec& spec);
caps::ProbeFragment run_runtime_probe(const caps::ProbeSpec& spec);
caps::ProbeFragment run_gpu_display_probe(const caps::ProbeSpec& spec);
caps::ProbeFragment run_encoder_probe(const caps::ProbeSpec& spec);
caps::ProbeFragment run_audio_probe(const caps::ProbeSpec& spec);

caps::ProbeFragment run_passive_probe(const caps::ProbeSpec& spec) {
    if (spec.access != caps::ProbeAccess::passive) {
        return {
            .probe_id = spec.probe_id,
            .family = spec.family,
            .revision = spec.revision,
            .duration = std::chrono::microseconds{1},
            .outcome = caps::ProbeOutcome::permission_unavailable,
            .issues = {{spec.probe_id, caps::IssueCode::permission_unavailable}},
        };
    }
    switch (spec.family) {
    case caps::ProbeFamily::system:
        return run_system_probe(spec);
    case caps::ProbeFamily::runtime:
        return run_runtime_probe(spec);
    case caps::ProbeFamily::gpu_display:
        return run_gpu_display_probe(spec);
    case caps::ProbeFamily::encoders:
        return run_encoder_probe(spec);
    case caps::ProbeFamily::audio:
        return run_audio_probe(spec);
    }
    return {};
}

} // namespace catro::platform::macos

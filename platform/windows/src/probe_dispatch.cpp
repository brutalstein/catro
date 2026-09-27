#include <catro/platform/windows/capability_service.hpp>

#include <chrono>

namespace catro::platform::windows {

namespace caps = catro::capabilities;

caps::ProbeFragment run_system_probe(const caps::ProbeSpec& spec);
caps::ProbeFragment run_runtime_probe(const caps::ProbeSpec& spec);

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
    case caps::ProbeFamily::encoders:
    case caps::ProbeFamily::audio:
        return {
            .probe_id = spec.probe_id,
            .family = spec.family,
            .revision = spec.revision,
            .duration = std::chrono::microseconds{1},
            .outcome = caps::ProbeOutcome::api_unavailable,
            .issues = {{spec.probe_id, caps::IssueCode::api_unavailable}},
        };
    }
    return {};
}

} // namespace catro::platform::windows

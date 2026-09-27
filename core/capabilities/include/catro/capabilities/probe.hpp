#pragma once

#include <catro/capabilities/model.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace catro::capabilities {

enum class ProbeAccess {
    passive,
    intrusive,
};

enum class ProbeDomain {
    platform_hardware,
    runtime,
    gpu_display,
    encoders,
    audio,
};

inline constexpr std::chrono::milliseconds kSystemProbeBudget{500};
inline constexpr std::chrono::milliseconds kRuntimeProbeBudget{500};
inline constexpr std::chrono::milliseconds kGpuDisplayProbeBudget{1500};
inline constexpr std::chrono::milliseconds kEncoderProbeBudget{1500};
inline constexpr std::chrono::milliseconds kAudioProbeBudget{1000};
inline constexpr std::chrono::milliseconds kInitialPublicationBudget{2000};

struct ProbeSpec {
    std::string probe_id;
    ProbeFamily family = ProbeFamily::system;
    ProbeAccess access = ProbeAccess::passive;
    ProbeDomain output = ProbeDomain::platform_hardware;
    std::uint32_t revision = 1;
    std::chrono::milliseconds hard_budget{0};

    friend bool operator==(const ProbeSpec&, const ProbeSpec&) = default;
};

struct SystemProbeFacts {
    PlatformIdentity platform;
    HardwareCapabilities hardware;

    friend bool operator==(const SystemProbeFacts&, const SystemProbeFacts&) = default;
};

struct RuntimeProbeFacts {
    Observed<PowerSource> power_source;
    Observed<bool> battery_present;
    Observed<bool> low_power_mode;
    Observed<ThermalPressure> thermal;
    Observed<MemoryPressure> memory_pressure;
    Observed<bool> remote_session;
    Observed<bool> headless;

    friend bool operator==(const RuntimeProbeFacts&, const RuntimeProbeFacts&) = default;
};

struct GpuDisplayProbeFacts {
    std::vector<GpuCapability> gpus;
    std::vector<DisplayCapability> displays;
    std::vector<CapturePathCapability> capture_paths;
    std::vector<TransferPathCapability> transfer_paths;
    std::vector<DisplayState> display_states;
    std::vector<CapturePermissionState> capture_permissions;

    friend bool operator==(const GpuDisplayProbeFacts&, const GpuDisplayProbeFacts&) = default;
};

struct EncoderProbeFacts {
    std::vector<EncoderCapability> encoders;

    friend bool operator==(const EncoderProbeFacts&, const EncoderProbeFacts&) = default;
};

struct AudioProbeFacts {
    std::vector<AudioEndpointCapability> endpoints;
    std::vector<AudioEndpointState> states;

    friend bool operator==(const AudioProbeFacts&, const AudioProbeFacts&) = default;
};

// Versioned, typed helper-process payload. Exactly one family payload is present for a
// successful or partial result. Failed helpers return metadata only; the coordinator supplies
// explicit unknown facts and never asks the shared model to interpret JSON.
struct ProbeFragment {
    std::string schema_id{kSchemaId};
    SchemaVersion schema_version{kSchemaVersion};
    std::string probe_id;
    ProbeFamily family = ProbeFamily::system;
    std::uint32_t revision = 1;
    std::chrono::microseconds duration{0};
    ProbeOutcome outcome = ProbeOutcome::success;
    std::optional<std::int64_t> native_error;
    std::optional<SystemProbeFacts> system;
    std::optional<RuntimeProbeFacts> runtime;
    std::optional<GpuDisplayProbeFacts> gpu_display;
    std::optional<EncoderProbeFacts> encoders;
    std::optional<AudioProbeFacts> audio;
    std::vector<ProbeIssue> issues;

    friend bool operator==(const ProbeFragment&, const ProbeFragment&) = default;
};

} // namespace catro::capabilities

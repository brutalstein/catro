#include <catro/capabilities/policy.hpp>

#include <optional>

namespace catro::capabilities {
namespace {

// Only validly known facts participate; unknown and unavailable evidence never implies a value.
template <class T>
std::optional<T> known_value(const Observed<T>& observed) {
    return observed.knowledge() == Knowledge::known ? observed.value() : std::nullopt;
}

bool known_true(const Observed<bool>& observed) {
    return known_value(observed).value_or(false);
}

} // namespace

ProfileDecision derive_operating_profile(const CapabilitySnapshot& snapshot, OperatingPreference preference) {
    const auto& runtime = snapshot.runtime;
    const auto thermal = known_value(runtime.thermal);
    const auto power = known_value(runtime.power_source);

    if (thermal == ThermalPressure::serious || thermal == ThermalPressure::critical) {
        return {OperatingProfile::thermal_constrained, ProfileRule::thermal_pressure};
    }
    if (known_true(runtime.headless) || known_true(runtime.remote_session) ||
        known_value(runtime.memory_pressure) == MemoryPressure::critical) {
        return {OperatingProfile::safe_local_envelope, ProfileRule::constrained_session};
    }

    const bool warm = thermal == ThermalPressure::fair;
    switch (preference) {
    case OperatingPreference::performance:
        return {warm ? OperatingProfile::balanced : OperatingProfile::performance, ProfileRule::explicit_preference};
    case OperatingPreference::balanced:
        return {OperatingProfile::balanced, ProfileRule::explicit_preference};
    case OperatingPreference::efficiency:
        return {OperatingProfile::efficiency, ProfileRule::explicit_preference};
    case OperatingPreference::automatic:
        break;
    }

    if (known_true(runtime.low_power_mode) || power == PowerSource::battery) {
        return {OperatingProfile::efficiency, ProfileRule::battery_or_low_power};
    }
    if (!power) {
        return {OperatingProfile::safe_local_envelope, ProfileRule::unknown_power_source};
    }
    const auto role = known_value(snapshot.hardware.platform_role);
    if (!warm && (role == PlatformRole::desktop || role == PlatformRole::server)) {
        return {OperatingProfile::performance, ProfileRule::mains_power_desktop};
    }
    return {OperatingProfile::balanced, ProfileRule::mains_power};
}

} // namespace catro::capabilities

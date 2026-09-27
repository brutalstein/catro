#pragma once

#include <catro/capabilities/probe.hpp>
#include <catro/capabilities/validation.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace catro::capabilities {

enum class RefreshReason {
    startup,
    diagnostics,
    audio,
    display,
    power,
    thermal,
    memory_pressure,
    session,
};

struct ProbeSchedule {
    OperatingSystem operating_system = OperatingSystem::windows;
    std::uint64_t generation = 1;
    UtcTimestamp captured_at{};
    std::uint32_t probe_revision = 1;
    RefreshReason reason = RefreshReason::startup;
    std::chrono::milliseconds publication_budget{kInitialPublicationBudget};
    std::vector<ProbeSpec> probes;
    // Present for a selective refresh. Scheduled families replace their old domains; all other
    // families remain immutable and retain their original observation generation.
    std::optional<CapabilitySnapshot> previous;
};

class RunningProbe {
public:
    virtual ~RunningProbe() = default;

    // Returns a complete typed fragment, or nullopt when the absolute deadline expires.
    [[nodiscard]] virtual std::optional<ProbeFragment>
    wait_until(std::chrono::steady_clock::time_point deadline) = 0;
    virtual void terminate() noexcept = 0;
};

class ProbeExecutor {
public:
    virtual ~ProbeExecutor() = default;
    [[nodiscard]] virtual std::unique_ptr<RunningProbe> start(const ProbeSpec& spec) = 0;
};

struct SnapshotPublication {
    std::optional<CapabilitySnapshot> snapshot;
    ValidationReport validation;
};

[[nodiscard]] ProbeSchedule make_initial_probe_schedule(OperatingSystem operating_system, std::uint64_t generation,
                                                        UtcTimestamp captured_at);

// Starts every helper before waiting, enforces the smaller of each family deadline and the
// global publication deadline, rejects invalid fragments, and publishes only a validated
// immutable generation.
[[nodiscard]] SnapshotPublication collect_snapshot(const ProbeSchedule& schedule, ProbeExecutor& executor);

} // namespace catro::capabilities

#pragma once

#include <catro/capabilities/model.hpp>
#include <catro/capabilities/validation.hpp>

#include <bitset>
#include <cstddef>
#include <optional>
#include <vector>

namespace catro::capabilities {

enum class ChangeDomain {
    gpu,
    encoder,
    display,
    capture,
    audio_input,
    audio_output,
    capture_permission,
    power,
    thermal,
    memory_pressure,
    platform_session,
    validation,
};

inline constexpr std::size_t kChangeDomainCount = 12;

// Classified difference between two snapshot generations. Holds no history.
struct ChangeSet {
    std::bitset<kChangeDomainCount> domains;
    // Sorted, unique identifiers of added, removed, or changed entries. Snapshot-scoped
    // identifiers cannot be compared across generations, so they mark the domain only.
    std::vector<GpuId> gpus;
    std::vector<EncoderId> encoders;
    std::vector<DisplayId> displays;
    std::vector<CapturePathId> capture_paths;
    std::vector<AudioEndpointId> audio_endpoints;

    [[nodiscard]] bool contains(ChangeDomain domain) const {
        return domains.test(static_cast<std::size_t>(domain));
    }

    [[nodiscard]] bool empty() const noexcept { return domains.none(); }

    friend bool operator==(const ChangeSet&, const ChangeSet&) = default;
};

// Order-insensitive: inventories are matched by typed ID, never by enumeration position.
// Probe durations and re-run generations are not changes; probe outcomes and issues are.
[[nodiscard]] ChangeSet diff_snapshots(const CapabilitySnapshot& previous, const CapabilitySnapshot& next);

struct SnapshotUpdate {
    CapabilitySnapshot snapshot;
    ChangeSet changes;
};

struct SnapshotUpdateResult {
    std::optional<SnapshotUpdate> update;
    ValidationReport validation;
};

// Builds an update only when the replacement validates and its generation is newer than the
// previous one. A null previous snapshot denotes the first publication.
[[nodiscard]] SnapshotUpdateResult make_snapshot_update(const CapabilitySnapshot* previous, CapabilitySnapshot next);

} // namespace catro::capabilities

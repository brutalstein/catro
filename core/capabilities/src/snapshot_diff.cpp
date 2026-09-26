#include <catro/capabilities/snapshot_diff.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace catro::capabilities {
namespace {

// Pairs entries by key rather than position and calls on_changed(before, after) for every key
// that was added (before == nullptr), removed (after == nullptr), or changed.
template <class Item, class KeyOf, class OnChanged>
void diff_by_key(const std::vector<Item>& before, const std::vector<Item>& after, KeyOf key_of, OnChanged on_changed) {
    using Key = std::remove_cvref_t<std::invoke_result_t<KeyOf&, const Item&>>;
    std::map<Key, const Item*> lhs;
    std::map<Key, const Item*> rhs;
    for (const auto& item : before) {
        lhs.emplace(key_of(item), &item);
    }
    for (const auto& item : after) {
        rhs.emplace(key_of(item), &item);
    }

    const Item* const none = nullptr;
    auto left = lhs.begin();
    auto right = rhs.begin();
    while (left != lhs.end() || right != rhs.end()) {
        if (right == rhs.end() || (left != lhs.end() && left->first < right->first)) {
            on_changed(left->second, none);
            ++left;
        } else if (left == lhs.end() || right->first < left->first) {
            on_changed(none, right->second);
            ++right;
        } else {
            if (!(*left->second == *right->second)) {
                on_changed(left->second, right->second);
            }
            ++left;
            ++right;
        }
    }
}

template <class Item>
const Item& either(const Item* before, const Item* after) {
    return before != nullptr ? *before : *after;
}

template <class Tag>
void sort_unique(std::vector<ScopedId<Tag>>& ids) {
    std::ranges::sort(ids);
    const auto duplicates = std::ranges::unique(ids);
    ids.erase(duplicates.begin(), duplicates.end());
}

// Probe durations and the generation of a re-run are bookkeeping, not capability changes.
auto probe_state(const CapabilitySnapshot& snapshot) {
    using Record = std::tuple<std::string, ProbeFamily, std::uint32_t, ProbeOutcome, std::optional<std::int64_t>, std::uint32_t>;
    std::vector<Record> records;
    for (const auto& record : snapshot.probes) {
        records.emplace_back(record.probe_id, record.family, record.revision, record.outcome, record.native_error,
                             record.fact_count);
    }
    std::ranges::sort(records);

    std::vector<std::pair<std::string, IssueCode>> issues;
    for (const auto& issue : snapshot.issues) {
        issues.emplace_back(issue.probe_id, issue.code);
    }
    std::ranges::sort(issues);
    return std::pair{std::move(records), std::move(issues)};
}

class Differ {
public:
    Differ(const CapabilitySnapshot& before, const CapabilitySnapshot& after) : before_(before), after_(after) {}

    ChangeSet run() {
        diff_devices();
        diff_runtime();

        const auto& lhs = before_.runtime;
        const auto& rhs = after_.runtime;
        if (lhs.power_source != rhs.power_source || lhs.battery_present != rhs.battery_present ||
            lhs.low_power_mode != rhs.low_power_mode) {
            mark(ChangeDomain::power);
        }
        if (lhs.thermal != rhs.thermal) {
            mark(ChangeDomain::thermal);
        }
        if (lhs.memory_pressure != rhs.memory_pressure) {
            mark(ChangeDomain::memory_pressure);
        }
        if (before_.platform != after_.platform || before_.hardware != after_.hardware ||
            lhs.remote_session != rhs.remote_session || lhs.headless != rhs.headless) {
            mark(ChangeDomain::platform_session);
        }
        if (probe_state(before_) != probe_state(after_)) {
            mark(ChangeDomain::validation);
        }

        sort_unique(changes_.gpus);
        sort_unique(changes_.encoders);
        sort_unique(changes_.displays);
        sort_unique(changes_.capture_paths);
        sort_unique(changes_.audio_endpoints);
        return std::move(changes_);
    }

private:
    void mark(ChangeDomain domain) { changes_.domains.set(static_cast<std::size_t>(domain)); }

    // ponytail: snapshot-scoped IDs are paired by value, so reassigned indexes can over-report a
    // domain change; they never under-report. Platforms should prefer wider documented scopes.
    template <class Tag>
    static void affected(std::vector<ScopedId<Tag>>& ids, const ScopedId<Tag>& id) {
        if (id.scope != IdentityScope::snapshot) {
            ids.push_back(id);
        }
    }

    void audio_changed(const AudioEndpointId& id) {
        bool direction_known = false;
        for (const auto* snapshot : {&before_, &after_}) {
            for (const auto& endpoint : snapshot->devices.audio_endpoints) {
                if (endpoint.id == id) {
                    mark(endpoint.direction == AudioDirection::input ? ChangeDomain::audio_input : ChangeDomain::audio_output);
                    direction_known = true;
                }
            }
        }
        if (!direction_known) {
            mark(ChangeDomain::audio_input);
            mark(ChangeDomain::audio_output);
        }
        affected(changes_.audio_endpoints, id);
    }

    void diff_devices() {
        const auto& lhs = before_.devices;
        const auto& rhs = after_.devices;

        diff_by_key(lhs.gpus, rhs.gpus, std::mem_fn(&GpuCapability::id), [this](const auto* a, const auto* b) {
            mark(ChangeDomain::gpu);
            affected(changes_.gpus, either(a, b).id);
        });
        diff_by_key(lhs.encoders, rhs.encoders, std::mem_fn(&EncoderCapability::id), [this](const auto* a, const auto* b) {
            mark(ChangeDomain::encoder);
            affected(changes_.encoders, either(a, b).id);
        });
        diff_by_key(lhs.displays, rhs.displays, std::mem_fn(&DisplayCapability::id), [this](const auto* a, const auto* b) {
            mark(ChangeDomain::display);
            affected(changes_.displays, either(a, b).id);
        });
        diff_by_key(lhs.capture_paths, rhs.capture_paths, std::mem_fn(&CapturePathCapability::id),
                    [this](const auto* a, const auto* b) {
                        mark(ChangeDomain::capture);
                        affected(changes_.capture_paths, either(a, b).id);
                    });
        diff_by_key(
            lhs.transfer_paths, rhs.transfer_paths,
            [](const TransferPathCapability& transfer) {
                return std::tuple{transfer.source, transfer.destination, transfer.source_gpu};
            },
            [this](const auto* a, const auto* b) {
                mark(ChangeDomain::capture);
                affected(changes_.capture_paths, either(a, b).source);
            });
        diff_by_key(lhs.audio_endpoints, rhs.audio_endpoints, std::mem_fn(&AudioEndpointCapability::id),
                    [this](const auto* a, const auto* b) { audio_changed(either(a, b).id); });
    }

    void diff_runtime() {
        const auto& lhs = before_.runtime;
        const auto& rhs = after_.runtime;

        diff_by_key(lhs.displays, rhs.displays, std::mem_fn(&DisplayState::display), [this](const auto* a, const auto* b) {
            mark(ChangeDomain::display);
            affected(changes_.displays, either(a, b).display);
        });
        diff_by_key(lhs.audio_endpoints, rhs.audio_endpoints, std::mem_fn(&AudioEndpointState::endpoint),
                    [this](const auto* a, const auto* b) { audio_changed(either(a, b).endpoint); });
        diff_by_key(lhs.capture_permissions, rhs.capture_permissions, std::mem_fn(&CapturePermissionState::path),
                    [this](const auto* a, const auto* b) {
                        mark(ChangeDomain::capture_permission);
                        affected(changes_.capture_paths, either(a, b).path);
                    });
    }

    const CapabilitySnapshot& before_;
    const CapabilitySnapshot& after_;
    ChangeSet changes_;
};

} // namespace

ChangeSet diff_snapshots(const CapabilitySnapshot& previous, const CapabilitySnapshot& next) {
    return Differ(previous, next).run();
}

SnapshotUpdateResult make_snapshot_update(const CapabilitySnapshot* previous, CapabilitySnapshot next) {
    auto report = validate(next);
    if (previous != nullptr && next.header.generation <= previous->header.generation) {
        report.errors.push_back({ValidationCode::invalid_generation, "header.generation"});
        std::ranges::sort(report.errors);
        const auto duplicates = std::ranges::unique(report.errors);
        report.errors.erase(duplicates.begin(), duplicates.end());
    }
    if (!report.ok()) {
        return {std::nullopt, std::move(report)};
    }

    static const CapabilitySnapshot nothing_published;
    const auto& baseline = previous != nullptr ? *previous : nothing_published;
    auto changes = diff_snapshots(baseline, next);
    return {SnapshotUpdate{std::move(next), std::move(changes)}, std::move(report)};
}

} // namespace catro::capabilities

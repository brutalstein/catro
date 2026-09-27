#include <catro/capabilities/probe_coordinator.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace catro::capabilities {
namespace {

constexpr std::array kFamilies{ProbeFamily::system, ProbeFamily::runtime, ProbeFamily::gpu_display,
                               ProbeFamily::encoders, ProbeFamily::audio};

constexpr std::size_t family_index(ProbeFamily family) {
    return static_cast<std::size_t>(family);
}

constexpr ProbeDomain domain_for(ProbeFamily family) {
    return static_cast<ProbeDomain>(family);
}

std::string_view platform_name(OperatingSystem os) {
    return os == OperatingSystem::windows ? "windows" : "macos";
}

std::string probe_id(OperatingSystem os, ProbeFamily family) {
    constexpr std::array suffixes{"system.v1", "runtime.v1", "gpu_display.v1", "encoders.v1", "audio.v1"};
    return std::string(platform_name(os)) + "." + suffixes[family_index(family)];
}

constexpr std::chrono::milliseconds budget_for(ProbeFamily family) {
    constexpr std::array budgets{kSystemProbeBudget, kRuntimeProbeBudget, kGpuDisplayProbeBudget,
                                 kEncoderProbeBudget, kAudioProbeBudget};
    return budgets[family_index(family)];
}

IssueCode issue_for(ProbeOutcome outcome) {
    switch (outcome) {
    case ProbeOutcome::api_unavailable:
        return IssueCode::api_unavailable;
    case ProbeOutcome::permission_unavailable:
        return IssueCode::permission_unavailable;
    case ProbeOutcome::timeout:
        return IssueCode::timeout;
    case ProbeOutcome::os_failure:
        return IssueCode::os_failure;
    case ProbeOutcome::malformed_output:
        return IssueCode::malformed_output;
    case ProbeOutcome::helper_terminated:
        return IssueCode::helper_terminated;
    case ProbeOutcome::partial:
    case ProbeOutcome::success:
        return IssueCode::not_reported;
    }
    return IssueCode::not_reported;
}

Provenance missing_provenance(std::string_view id, IssueCode issue) {
    return Provenance{std::string(id), EvidenceMethod::measured, Confidence::degraded, issue};
}

template <class T>
Observed<T> missing_fact(std::string_view id, IssueCode issue) {
    auto provenance = missing_provenance(id, issue);
    if (issue == IssueCode::api_unavailable || issue == IssueCode::permission_unavailable) {
        return Observed<T>::unavailable(std::move(provenance));
    }
    return Observed<T>::unknown(std::move(provenance));
}

SystemProbeFacts missing_system(OperatingSystem os, std::string_view id, IssueCode issue) {
    return {
        .platform = {os, missing_fact<OsVersion>(id, issue)},
        .hardware = {
            .cpu = {
                .native_architecture = missing_fact<CpuArchitecture>(id, issue),
                .process_architecture = missing_fact<CpuArchitecture>(id, issue),
                .translation = missing_fact<TranslationState>(id, issue),
                .physical_cores = missing_fact<std::uint32_t>(id, issue),
                .logical_cores = missing_fact<std::uint32_t>(id, issue),
                .performance_cores = missing_fact<std::uint32_t>(id, issue),
                .efficiency_cores = missing_fact<std::uint32_t>(id, issue),
                .simd = missing_fact<std::vector<SimdFeature>>(id, issue),
            },
            .installed_memory = missing_fact<Bytes>(id, issue),
            .platform_role = missing_fact<PlatformRole>(id, issue),
        },
    };
}

RuntimeProbeFacts missing_runtime(std::string_view id, IssueCode issue) {
    return {
        missing_fact<PowerSource>(id, issue),
        missing_fact<bool>(id, issue),
        missing_fact<bool>(id, issue),
        missing_fact<ThermalPressure>(id, issue),
        missing_fact<MemoryPressure>(id, issue),
        missing_fact<bool>(id, issue),
        missing_fact<bool>(id, issue),
    };
}

std::size_t payload_count(const ProbeFragment& fragment) {
    return static_cast<std::size_t>(fragment.system.has_value()) + static_cast<std::size_t>(fragment.runtime.has_value()) +
           static_cast<std::size_t>(fragment.gpu_display.has_value()) +
           static_cast<std::size_t>(fragment.encoders.has_value()) + static_cast<std::size_t>(fragment.audio.has_value());
}

bool has_family_payload(const ProbeFragment& fragment) {
    switch (fragment.family) {
    case ProbeFamily::system:
        return fragment.system.has_value();
    case ProbeFamily::runtime:
        return fragment.runtime.has_value();
    case ProbeFamily::gpu_display:
        return fragment.gpu_display.has_value();
    case ProbeFamily::encoders:
        return fragment.encoders.has_value();
    case ProbeFamily::audio:
        return fragment.audio.has_value();
    }
    return false;
}

bool usable_fragment(const ProbeFragment& fragment, const ProbeSpec& spec) {
    if (fragment.schema_id != kSchemaId || !is_supported(fragment.schema_version) || fragment.probe_id != spec.probe_id ||
        fragment.family != spec.family || fragment.revision != spec.revision || fragment.duration.count() < 0) {
        return false;
    }
    if (std::ranges::any_of(fragment.issues,
                            [&](const ProbeIssue& issue) { return issue.probe_id != spec.probe_id; })) {
        return false;
    }
    const bool returns_facts = fragment.outcome == ProbeOutcome::success || fragment.outcome == ProbeOutcome::partial;
    return returns_facts ? payload_count(fragment) == 1 && has_family_payload(fragment) : payload_count(fragment) == 0;
}

void clear_payload(ProbeFragment& fragment) {
    fragment.system.reset();
    fragment.runtime.reset();
    fragment.gpu_display.reset();
    fragment.encoders.reset();
    fragment.audio.reset();
}

ProbeFragment failed_fragment(const ProbeSpec& spec, ProbeOutcome outcome, std::chrono::microseconds duration) {
    return {
        .probe_id = spec.probe_id,
        .family = spec.family,
        .revision = spec.revision,
        .duration = duration,
        .outcome = outcome,
    };
}

void reject_fragment(ProbeFragment& fragment) {
    clear_payload(fragment);
    fragment.outcome = ProbeOutcome::malformed_output;
    fragment.issues.clear();
    fragment.issues.push_back({fragment.probe_id, IssueCode::malformed_output});
}

std::uint32_t fact_count(const ProbeFragment& fragment) {
    if (fragment.system) {
        return 10;
    }
    if (fragment.runtime) {
        return 7;
    }
    if (fragment.gpu_display) {
        const auto& facts = *fragment.gpu_display;
        return static_cast<std::uint32_t>(facts.gpus.size() + facts.displays.size() + facts.capture_paths.size() +
                                          facts.transfer_paths.size() + facts.display_states.size() +
                                          facts.capture_permissions.size());
    }
    if (fragment.encoders) {
        return static_cast<std::uint32_t>(fragment.encoders->encoders.size());
    }
    if (fragment.audio) {
        return static_cast<std::uint32_t>(fragment.audio->endpoints.size() + fragment.audio->states.size());
    }
    return 0;
}

struct Run {
    ProbeSpec spec;
    std::chrono::steady_clock::time_point started;
    std::unique_ptr<RunningProbe> process;
    ProbeFragment fragment;
};

void add_issue(std::vector<ProbeIssue>& issues, const ProbeIssue& issue) {
    if (std::ranges::find(issues, issue) == issues.end()) {
        issues.push_back(issue);
    }
}

CapabilitySnapshot build_snapshot(const ProbeSchedule& schedule, const std::vector<Run>& runs) {
    CapabilitySnapshot snapshot = schedule.previous.value_or(CapabilitySnapshot{});
    snapshot.header.schema_id = kSchemaId;
    snapshot.header.schema_version = kSchemaVersion;
    snapshot.header.generation = schedule.generation;
    snapshot.header.captured_at = schedule.captured_at;
    snapshot.header.probe_revision = schedule.probe_revision;

    std::set<ProbeFamily> refreshed;
    std::set<std::string> old_probe_ids;
    for (const auto& run : runs) {
        refreshed.insert(run.spec.family);
        for (const auto& record : snapshot.probes) {
            if (record.family == run.spec.family) {
                old_probe_ids.insert(record.probe_id);
            }
        }
    }
    std::erase_if(snapshot.probes, [&](const ProbeRecord& record) { return refreshed.contains(record.family); });
    std::erase_if(snapshot.issues,
                  [&](const ProbeIssue& issue) { return old_probe_ids.contains(issue.probe_id); });

    for (const auto& run : runs) {
        const auto& fragment = run.fragment;
        snapshot.probes.push_back({
            .probe_id = run.spec.probe_id,
            .family = run.spec.family,
            .revision = run.spec.revision,
            .generation = schedule.generation,
            .duration = fragment.duration,
            .outcome = fragment.outcome,
            .native_error = fragment.native_error,
            .fact_count = fact_count(fragment),
        });
        for (const auto& issue : fragment.issues) {
            add_issue(snapshot.issues, issue);
        }
        if (fragment.outcome != ProbeOutcome::success && fragment.issues.empty()) {
            add_issue(snapshot.issues, {run.spec.probe_id, issue_for(fragment.outcome)});
        }

        switch (run.spec.family) {
        case ProbeFamily::system: {
            const auto facts = fragment.system.value_or(
                missing_system(schedule.operating_system, run.spec.probe_id, issue_for(fragment.outcome)));
            snapshot.platform = facts.platform;
            snapshot.hardware = facts.hardware;
            break;
        }
        case ProbeFamily::runtime: {
            const auto facts = fragment.runtime.value_or(missing_runtime(run.spec.probe_id, issue_for(fragment.outcome)));
            snapshot.runtime.power_source = facts.power_source;
            snapshot.runtime.battery_present = facts.battery_present;
            snapshot.runtime.low_power_mode = facts.low_power_mode;
            snapshot.runtime.thermal = facts.thermal;
            snapshot.runtime.memory_pressure = facts.memory_pressure;
            snapshot.runtime.remote_session = facts.remote_session;
            snapshot.runtime.headless = facts.headless;
            break;
        }
        case ProbeFamily::gpu_display:
            if (fragment.gpu_display) {
                snapshot.devices.gpus = fragment.gpu_display->gpus;
                snapshot.devices.displays = fragment.gpu_display->displays;
                snapshot.devices.capture_paths = fragment.gpu_display->capture_paths;
                snapshot.devices.transfer_paths = fragment.gpu_display->transfer_paths;
                snapshot.runtime.displays = fragment.gpu_display->display_states;
                snapshot.runtime.capture_permissions = fragment.gpu_display->capture_permissions;
            } else {
                snapshot.devices.gpus.clear();
                snapshot.devices.displays.clear();
                snapshot.devices.capture_paths.clear();
                snapshot.devices.transfer_paths.clear();
                snapshot.runtime.displays.clear();
                snapshot.runtime.capture_permissions.clear();
            }
            break;
        case ProbeFamily::encoders:
            snapshot.devices.encoders = fragment.encoders ? fragment.encoders->encoders : std::vector<EncoderCapability>{};
            break;
        case ProbeFamily::audio:
            if (fragment.audio) {
                snapshot.devices.audio_endpoints = fragment.audio->endpoints;
                snapshot.runtime.audio_endpoints = fragment.audio->states;
            } else {
                snapshot.devices.audio_endpoints.clear();
                snapshot.runtime.audio_endpoints.clear();
            }
            break;
        }
    }
    return snapshot;
}

std::optional<ProbeFamily> family_for_path(std::string_view path) {
    if (path.starts_with("platform") || path.starts_with("hardware")) {
        return ProbeFamily::system;
    }
    if (path.starts_with("devices.encoders")) {
        return ProbeFamily::encoders;
    }
    if (path.starts_with("devices.audio_endpoints") || path.starts_with("runtime.audio_endpoints")) {
        return ProbeFamily::audio;
    }
    if (path.starts_with("devices.gpus") || path.starts_with("devices.displays") ||
        path.starts_with("devices.capture_paths") || path.starts_with("devices.transfer_paths") ||
        path.starts_with("runtime.displays") || path.starts_with("runtime.capture_permissions")) {
        return ProbeFamily::gpu_display;
    }
    if (path.starts_with("runtime")) {
        return ProbeFamily::runtime;
    }
    return std::nullopt;
}

ValidationReport invalid_schedule(const ProbeSchedule& schedule) {
    ValidationReport report;
    if (schedule.generation == 0 || (schedule.previous && schedule.generation <= schedule.previous->header.generation)) {
        report.errors.push_back({ValidationCode::invalid_generation, "schedule.generation"});
    }
    if (schedule.publication_budget.count() <= 0) {
        report.errors.push_back({ValidationCode::invalid_quantity, "schedule.publication_budget"});
    }
    std::set<std::string> ids;
    std::set<ProbeFamily> families;
    for (const auto& spec : schedule.probes) {
        const auto path = "schedule.probes[" + spec.probe_id + "]";
        if (spec.probe_id.empty() || spec.probe_id.size() > kMaxIdentifierBytes) {
            report.errors.push_back({ValidationCode::invalid_identifier, path});
        }
        if (!ids.insert(spec.probe_id).second || !families.insert(spec.family).second) {
            report.errors.push_back({ValidationCode::duplicate_id, path});
        }
        if (spec.access != ProbeAccess::passive || spec.output != domain_for(spec.family)) {
            report.errors.push_back({ValidationCode::contradictory_evidence, path});
        }
        if (spec.revision == 0 || spec.hard_budget.count() <= 0) {
            report.errors.push_back({ValidationCode::invalid_quantity, path});
        }
    }
    if (!schedule.previous && families.size() != kFamilies.size()) {
        report.errors.push_back({ValidationCode::missing_probe_reference, "schedule.probes"});
    }
    if (schedule.previous) {
        auto previous = validate(*schedule.previous);
        report.errors.insert(report.errors.end(), previous.errors.begin(), previous.errors.end());
    }
    std::ranges::sort(report.errors);
    const auto last = std::ranges::unique(report.errors).begin();
    report.errors.erase(last, report.errors.end());
    return report;
}

} // namespace

ProbeSchedule make_initial_probe_schedule(OperatingSystem operating_system, std::uint64_t generation,
                                          UtcTimestamp captured_at) {
    ProbeSchedule schedule{
        .operating_system = operating_system,
        .generation = generation,
        .captured_at = captured_at,
    };
    for (const auto family : kFamilies) {
        schedule.probes.push_back({probe_id(operating_system, family), family, ProbeAccess::passive, domain_for(family), 1,
                                   budget_for(family)});
    }
    return schedule;
}

SnapshotPublication collect_snapshot(const ProbeSchedule& schedule, ProbeExecutor& executor) {
    auto schedule_validation = invalid_schedule(schedule);
    if (!schedule_validation.ok()) {
        return {std::nullopt, std::move(schedule_validation)};
    }

    const auto collection_started = std::chrono::steady_clock::now();
    const auto publication_deadline = collection_started + schedule.publication_budget;
    auto specs = schedule.probes;
    std::ranges::sort(specs, [](const ProbeSpec& lhs, const ProbeSpec& rhs) {
        return std::tie(lhs.family, lhs.probe_id) < std::tie(rhs.family, rhs.probe_id);
    });

    std::vector<Run> runs;
    runs.reserve(specs.size());
    for (const auto& spec : specs) {
        Run run{.spec = spec, .started = std::chrono::steady_clock::now(), .fragment = failed_fragment(spec, ProbeOutcome::helper_terminated, {})};
        try {
            run.process = executor.start(spec);
        } catch (...) {
            run.process.reset();
        }
        runs.push_back(std::move(run));
    }

    for (auto& run : runs) {
        if (!run.process) {
            continue;
        }
        const auto family_deadline = run.started + run.spec.hard_budget;
        const auto deadline = std::min(family_deadline, publication_deadline);
        std::optional<ProbeFragment> fragment;
        try {
            fragment = run.process->wait_until(deadline);
        } catch (...) {
            fragment = failed_fragment(run.spec, ProbeOutcome::helper_terminated,
                                       std::chrono::duration_cast<std::chrono::microseconds>(
                                           std::chrono::steady_clock::now() - run.started));
        }
        if (!fragment) {
            run.process->terminate();
            run.fragment = failed_fragment(run.spec, ProbeOutcome::timeout,
                                           std::chrono::duration_cast<std::chrono::microseconds>(deadline - run.started));
        } else if (!usable_fragment(*fragment, run.spec)) {
            run.fragment = failed_fragment(run.spec, ProbeOutcome::malformed_output,
                                           std::max(fragment->duration, std::chrono::microseconds{0}));
        } else {
            run.fragment = std::move(*fragment);
        }
    }

    for (std::size_t attempt = 0; attempt <= kFamilies.size(); ++attempt) {
        auto snapshot = build_snapshot(schedule, runs);
        auto validation = validate(snapshot);
        if (validation.ok()) {
            return {std::move(snapshot), std::move(validation)};
        }

        std::set<ProbeFamily> rejected;
        for (const auto& error : validation.errors) {
            if (const auto family = family_for_path(error.path)) {
                rejected.insert(*family);
            }
        }
        bool changed = false;
        for (auto& run : runs) {
            if (rejected.contains(run.spec.family) && payload_count(run.fragment) != 0) {
                reject_fragment(run.fragment);
                changed = true;
            }
        }
        if (!changed) {
            return {std::nullopt, std::move(validation)};
        }
    }

    auto snapshot = build_snapshot(schedule, runs);
    return {std::nullopt, validate(snapshot)};
}

} // namespace catro::capabilities

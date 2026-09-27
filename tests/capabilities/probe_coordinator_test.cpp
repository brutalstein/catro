#include <catro/capabilities/probe.hpp>
#include <catro/capabilities/probe_coordinator.hpp>
#include <catro/capabilities/validation.hpp>

#include "fixtures/capability_fixtures.hpp"
#include "helpers/fake_probe_executor.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

using namespace std::chrono_literals;
using namespace catro::capabilities;
namespace fx = catro::fixtures;
namespace test = catro::tests;

namespace {

ProbeFragment fragment_for(const CapabilitySnapshot& snapshot, ProbeFamily family) {
    const auto& record = *std::ranges::find(snapshot.probes, family, &ProbeRecord::family);
    ProbeFragment fragment{
        .probe_id = record.probe_id,
        .family = family,
        .revision = record.revision,
        .duration = 1ms,
        .outcome = ProbeOutcome::success,
    };
    switch (family) {
    case ProbeFamily::system:
        fragment.system = SystemProbeFacts{snapshot.platform, snapshot.hardware};
        break;
    case ProbeFamily::runtime:
        fragment.runtime = RuntimeProbeFacts{
            snapshot.runtime.power_source,
            snapshot.runtime.battery_present,
            snapshot.runtime.low_power_mode,
            snapshot.runtime.thermal,
            snapshot.runtime.memory_pressure,
            snapshot.runtime.remote_session,
            snapshot.runtime.headless,
        };
        break;
    case ProbeFamily::gpu_display:
        fragment.gpu_display = GpuDisplayProbeFacts{
            snapshot.devices.gpus,
            snapshot.devices.displays,
            snapshot.devices.capture_paths,
            snapshot.runtime.displays,
            snapshot.runtime.capture_permissions,
        };
        break;
    case ProbeFamily::encoders:
        fragment.encoders = EncoderProbeFacts{snapshot.devices.encoders, snapshot.devices.transfer_paths};
        break;
    case ProbeFamily::audio:
        fragment.audio = AudioProbeFacts{snapshot.devices.audio_endpoints, snapshot.runtime.audio_endpoints};
        break;
    }
    return fragment;
}

ProbeSchedule schedule_for(const CapabilitySnapshot& snapshot) {
    auto schedule = make_initial_probe_schedule(snapshot.platform.os, 2, snapshot.header.captured_at + 1s);
    for (auto& spec : schedule.probes) {
        spec.hard_budget = 100ms;
    }
    schedule.publication_budget = 200ms;
    return schedule;
}

void complete_all(test::FakeProbeExecutor& executor, const CapabilitySnapshot& snapshot,
                  std::chrono::milliseconds delay = 0ms) {
    for (const auto family : {ProbeFamily::system, ProbeFamily::runtime, ProbeFamily::gpu_display,
                              ProbeFamily::encoders, ProbeFamily::audio}) {
        auto fragment = fragment_for(snapshot, family);
        const auto id = fragment.probe_id;
        executor.set(id, {.fragment = std::move(fragment), .ready_after = delay});
    }
}

const ProbeRecord& record_for(const CapabilitySnapshot& snapshot, ProbeFamily family) {
    const auto found = std::ranges::find(snapshot.probes, family, &ProbeRecord::family);
    REQUIRE(found != snapshot.probes.end());
    return *found;
}

} // namespace

TEST_CASE("the initial schedule fixes stable probe IDs and hard budgets") {
    const auto windows = make_initial_probe_schedule(OperatingSystem::windows, 1, UtcTimestamp{});
    REQUIRE(windows.publication_budget == 2000ms);
    REQUIRE((windows.probes == std::vector<ProbeSpec>{
        {"windows.system.v1", ProbeFamily::system, ProbeAccess::passive, ProbeDomain::platform_hardware, 1, 500ms},
        {"windows.runtime.v1", ProbeFamily::runtime, ProbeAccess::passive, ProbeDomain::runtime, 1, 500ms},
        {"windows.gpu_display.v1", ProbeFamily::gpu_display, ProbeAccess::passive, ProbeDomain::gpu_display, 1, 1500ms},
        {"windows.encoders.v1", ProbeFamily::encoders, ProbeAccess::passive, ProbeDomain::encoders, 1, 1500ms},
        {"windows.audio.v1", ProbeFamily::audio, ProbeAccess::passive, ProbeDomain::audio, 1, 1000ms},
    }));

    const auto mac = make_initial_probe_schedule(OperatingSystem::macos, 1, UtcTimestamp{});
    REQUIRE(mac.probes.front().probe_id == "macos.system.v1");
    REQUIRE(mac.probes.back().probe_id == "macos.audio.v1");
}

TEST_CASE("all probe helpers start before the coordinator waits") {
    const auto source = fx::valid_snapshot();
    auto schedule = schedule_for(source);
    test::FakeProbeExecutor executor;
    complete_all(executor, source, 30ms);

    const auto started = std::chrono::steady_clock::now();
    const auto publication = collect_snapshot(schedule, executor);
    const auto elapsed = std::chrono::steady_clock::now() - started;

    REQUIRE(executor.launch_count_at_first_wait() == 5);
    REQUIRE(elapsed < 120ms);
    REQUIRE(publication.snapshot);
    REQUIRE(publication.validation.ok());
}

TEST_CASE("every terminal probe outcome produces one record") {
    constexpr std::array outcomes{
        ProbeOutcome::success,
        ProbeOutcome::partial,
        ProbeOutcome::api_unavailable,
        ProbeOutcome::permission_unavailable,
        ProbeOutcome::malformed_output,
        ProbeOutcome::helper_terminated,
        ProbeOutcome::timeout,
    };
    const auto source = fx::valid_snapshot();

    for (const auto outcome : outcomes) {
        INFO(static_cast<int>(outcome));
        auto schedule = schedule_for(source);
        for (auto& spec : schedule.probes) {
            spec.hard_budget = 20ms;
        }
        schedule.publication_budget = 40ms;
        test::FakeProbeExecutor executor;
        complete_all(executor, source);

        auto audio = fragment_for(source, ProbeFamily::audio);
        audio.outcome = outcome;
        if (outcome == ProbeOutcome::partial) {
            audio.issues.push_back({audio.probe_id, IssueCode::not_reported});
        } else if (outcome != ProbeOutcome::success && outcome != ProbeOutcome::timeout) {
            audio.audio.reset();
        }
        if (outcome == ProbeOutcome::timeout) {
            executor.set(audio.probe_id, {.never_completes = true});
        } else {
            const auto id = audio.probe_id;
            executor.set(id, {.fragment = std::move(audio)});
        }

        const auto publication = collect_snapshot(schedule, executor);
        REQUIRE(publication.snapshot);
        REQUIRE(publication.validation.ok());
        REQUIRE(record_for(*publication.snapshot, ProbeFamily::audio).outcome == outcome);
    }
}

TEST_CASE("the global deadline publishes explicit unknowns for a blocked family") {
    const auto source = fx::valid_snapshot();
    auto schedule = schedule_for(source);
    for (auto& spec : schedule.probes) {
        spec.hard_budget = 200ms;
    }
    schedule.publication_budget = 40ms;
    test::FakeProbeExecutor executor;
    complete_all(executor, source, 1ms);
    executor.set(std::string(fx::kSystemProbe), {.never_completes = true});

    const auto started = std::chrono::steady_clock::now();
    const auto publication = collect_snapshot(schedule, executor);
    const auto elapsed = std::chrono::steady_clock::now() - started;

    REQUIRE(elapsed < 150ms);
    REQUIRE(publication.snapshot);
    REQUIRE(publication.validation.ok());
    REQUIRE(record_for(*publication.snapshot, ProbeFamily::system).outcome == ProbeOutcome::timeout);
    REQUIRE(publication.snapshot->platform.version.knowledge() == Knowledge::unknown);
    REQUIRE(publication.snapshot->platform.version.provenance().issue == IssueCode::timeout);
    REQUIRE(publication.snapshot->hardware.installed_memory.knowledge() == Knowledge::unknown);
    REQUIRE(std::ranges::find(executor.terminated(), fx::kSystemProbe) != executor.terminated().end());
}

TEST_CASE("valid fragments merge without rewriting fact provenance") {
    const auto source = fx::valid_snapshot();
    test::FakeProbeExecutor executor;
    complete_all(executor, source);

    const auto publication = collect_snapshot(schedule_for(source), executor);

    REQUIRE(publication.snapshot);
    REQUIRE(publication.validation.ok());
    REQUIRE(publication.snapshot->devices.gpus.front().name.provenance() == source.devices.gpus.front().name.provenance());
    REQUIRE(publication.snapshot->devices.encoders.front().name.provenance() ==
            source.devices.encoders.front().name.provenance());
    REQUIRE(publication.snapshot->runtime.power_source.provenance() == source.runtime.power_source.provenance());
}

TEST_CASE("a fragment with duplicate IDs is rejected without poisoning publication") {
    const auto source = fx::valid_snapshot();
    test::FakeProbeExecutor executor;
    complete_all(executor, source);
    auto audio = fragment_for(source, ProbeFamily::audio);
    audio.audio->endpoints.push_back(audio.audio->endpoints.front());
    const auto audio_id = audio.probe_id;
    executor.set(audio_id, {.fragment = std::move(audio)});

    const auto publication = collect_snapshot(schedule_for(source), executor);

    REQUIRE(publication.snapshot);
    REQUIRE(publication.validation.ok());
    REQUIRE(publication.snapshot->devices.audio_endpoints.empty());
    REQUIRE(record_for(*publication.snapshot, ProbeFamily::audio).outcome == ProbeOutcome::malformed_output);
}

TEST_CASE("a cross-fragment dangling reference rejects the referencing fragment") {
    auto source = fx::valid_snapshot();
    test::FakeProbeExecutor executor;
    complete_all(executor, source);
    auto gpu_display = fragment_for(source, ProbeFamily::gpu_display);
    const auto gpu_display_id = gpu_display.probe_id;
    executor.set(gpu_display_id, {.fragment = std::move(gpu_display)});
    auto encoders = fragment_for(source, ProbeFamily::encoders);
    auto& gpu = encoders.encoders->encoders.front().gpu;
    gpu = Observed<GpuId>::known(GpuId{"missing-gpu", IdentityScope::os_session}, gpu.provenance());
    const auto encoder_id = encoders.probe_id;
    executor.set(encoder_id, {.fragment = std::move(encoders)});

    const auto publication = collect_snapshot(schedule_for(source), executor);

    REQUIRE(publication.snapshot);
    REQUIRE(publication.validation.ok());
    REQUIRE_FALSE(publication.snapshot->devices.gpus.empty());
    REQUIRE(publication.snapshot->devices.encoders.empty());
    REQUIRE(record_for(*publication.snapshot, ProbeFamily::encoders).outcome == ProbeOutcome::malformed_output);
}

#include <catro/capabilities/validation.hpp>
#include <catro/platform/windows/capability_service.hpp>
#include <catro/reporting/canonical_json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using namespace catro::capabilities;
using namespace catro::platform::windows;
using namespace catro::reporting;

namespace {

template <class T>
const T& require_known(const Observed<T>& fact) {
    REQUIRE(fact.knowledge() == Knowledge::known);
    REQUIRE(fact.value());
    REQUIRE_FALSE(fact.provenance().issue);
    return *fact.value();
}

template <class T>
void require_explicit(const Observed<T>& fact) {
    if (fact.knowledge() == Knowledge::known) {
        REQUIRE(fact.value());
    } else {
        REQUIRE_FALSE(fact.value());
        REQUIRE(fact.provenance().issue);
    }
}

ProbeSpec system_spec() {
    return {"windows.system.v1", ProbeFamily::system, ProbeAccess::passive,
            ProbeDomain::platform_hardware, 1, kSystemProbeBudget};
}

ProbeSpec runtime_spec() {
    return {"windows.runtime.v1", ProbeFamily::runtime, ProbeAccess::passive,
            ProbeDomain::runtime, 1, kRuntimeProbeBudget};
}

} // namespace

TEST_CASE("the Windows system probe reports measured topology and explicit uncertainty") {
    const auto fragment = run_passive_probe(system_spec());
    REQUIRE((fragment.outcome == ProbeOutcome::success || fragment.outcome == ProbeOutcome::partial));
    REQUIRE(fragment.duration > 0us);
    REQUIRE(fragment.system);
    REQUIRE(fragment.probe_id == "windows.system.v1");

    const auto& facts = *fragment.system;
    const auto& version = require_known(facts.platform.version);
    REQUIRE(version.major > 0);
    REQUIRE(require_known(facts.hardware.cpu.physical_cores) > 0);
    REQUIRE(require_known(facts.hardware.cpu.logical_cores) >= require_known(facts.hardware.cpu.physical_cores));
    REQUIRE(require_known(facts.hardware.installed_memory).value > 0);
    require_known(facts.hardware.cpu.native_architecture);
    require_known(facts.hardware.cpu.process_architecture);
    require_known(facts.hardware.cpu.translation);
    require_explicit(facts.hardware.cpu.performance_cores);
    require_explicit(facts.hardware.cpu.efficiency_cores);
    require_explicit(facts.hardware.platform_role);

    const auto encoded = to_canonical_json(fragment);
    const auto parsed = parse_probe_fragment(encoded);
    REQUIRE(parsed.ok());
    REQUIRE(*parsed.fragment == fragment);
}

TEST_CASE("the Windows runtime probe reports power and session facts without inventing state") {
    const auto fragment = run_passive_probe(runtime_spec());
    REQUIRE((fragment.outcome == ProbeOutcome::success || fragment.outcome == ProbeOutcome::partial));
    REQUIRE(fragment.duration > 0us);
    REQUIRE(fragment.runtime);

    const auto& facts = *fragment.runtime;
    require_explicit(facts.power_source);
    require_explicit(facts.battery_present);
    require_explicit(facts.low_power_mode);
    require_explicit(facts.thermal);
    require_explicit(facts.memory_pressure);
    require_known(facts.remote_session);
    require_known(facts.headless);
}

TEST_CASE("the Windows capability service publishes generations and stops cleanly") {
    CapabilityService service{std::filesystem::path(CATRO_CAPABILITY_PROBE_HELPER)};
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<SnapshotUpdate> updates;

    service.start([&](SnapshotUpdate update) {
        {
            const std::scoped_lock lock(mutex);
            updates.push_back(std::move(update));
        }
        changed.notify_all();
    });

    {
        std::unique_lock lock(mutex);
        REQUIRE(changed.wait_for(lock, 5s, [&] { return !updates.empty(); }));
        REQUIRE(updates.front().snapshot.header.generation == 1);
        REQUIRE(validate(updates.front().snapshot).ok());
        // The coordinator records a family whose real-hardware facts failed validation as malformed.
        for (const auto& record : updates.front().snapshot.probes) {
            INFO(record.probe_id);
            CHECK(record.outcome != ProbeOutcome::malformed_output);
        }
    }

    service.refresh(RefreshReason::power);
    {
        std::unique_lock lock(mutex);
        REQUIRE(changed.wait_for(lock, 5s, [&] { return updates.size() >= 2; }));
        REQUIRE(updates.back().snapshot.header.generation == 2);
        REQUIRE(validate(updates.back().snapshot).ok());
    }

    service.stop();
    const auto count = updates.size();
    service.refresh(RefreshReason::diagnostics);
    std::this_thread::sleep_for(150ms);
    const std::scoped_lock lock(mutex);
    REQUIRE(updates.size() == count);
}

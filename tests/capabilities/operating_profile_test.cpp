#include <catro/capabilities/policy.hpp>

#include "fixtures/capability_fixtures.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace catro::capabilities;
namespace fx = catro::fixtures;

namespace {

ProfileDecision automatic(const CapabilitySnapshot& snapshot) {
    return derive_operating_profile(snapshot, OperatingPreference::automatic);
}

} // namespace

TEST_CASE("battery plus low-power mode derives efficiency") {
    REQUIRE(automatic(fx::intel_laptop_on_battery()) ==
            ProfileDecision{OperatingProfile::efficiency, ProfileRule::battery_or_low_power});
}

TEST_CASE("battery alone derives efficiency under automatic preference") {
    auto snapshot = fx::hybrid_laptop();
    snapshot.runtime.power_source = fx::known(PowerSource::battery, fx::measured(fx::kRuntimeProbe));
    REQUIRE(automatic(snapshot).profile == OperatingProfile::efficiency);
}

TEST_CASE("serious thermal pressure overrides performance preference") {
    REQUIRE(derive_operating_profile(fx::hot_apple_silicon(), OperatingPreference::performance) ==
            ProfileDecision{OperatingProfile::thermal_constrained, ProfileRule::thermal_pressure});
}

TEST_CASE("critical thermal pressure constrains every preference") {
    auto snapshot = fx::apple_silicon_macbook();
    snapshot.runtime.thermal = fx::known(ThermalPressure::critical, fx::measured(fx::kMacRuntimeProbe));
    for (const auto preference : {OperatingPreference::automatic, OperatingPreference::performance,
                                  OperatingPreference::balanced, OperatingPreference::efficiency}) {
        REQUIRE(derive_operating_profile(snapshot, preference).profile == OperatingProfile::thermal_constrained);
    }
}

TEST_CASE("headless, remote, and critical-memory sessions use the safe local envelope") {
    REQUIRE(automatic(fx::headless_session()) ==
            ProfileDecision{OperatingProfile::safe_local_envelope, ProfileRule::constrained_session});

    auto remote = fx::valid_snapshot();
    remote.runtime.remote_session = fx::known(true, fx::measured(fx::kRuntimeProbe));
    REQUIRE(derive_operating_profile(remote, OperatingPreference::performance).profile ==
            OperatingProfile::safe_local_envelope);

    auto starved = fx::valid_snapshot();
    starved.runtime.memory_pressure = fx::known(MemoryPressure::critical, fx::measured(fx::kRuntimeProbe));
    REQUIRE(automatic(starved).profile == OperatingProfile::safe_local_envelope);
}

TEST_CASE("explicit preference outranks power source") {
    REQUIRE(derive_operating_profile(fx::valid_snapshot(), OperatingPreference::efficiency) ==
            ProfileDecision{OperatingProfile::efficiency, ProfileRule::explicit_preference});
    REQUIRE(derive_operating_profile(fx::intel_laptop_on_battery(), OperatingPreference::performance) ==
            ProfileDecision{OperatingProfile::performance, ProfileRule::explicit_preference});
    REQUIRE(derive_operating_profile(fx::valid_snapshot(), OperatingPreference::balanced).profile ==
            OperatingProfile::balanced);
}

TEST_CASE("fair thermal pressure tempers performance to balanced") {
    auto snapshot = fx::apple_silicon_macbook();
    snapshot.runtime.thermal = fx::known(ThermalPressure::fair, fx::measured(fx::kMacRuntimeProbe));
    REQUIRE(derive_operating_profile(snapshot, OperatingPreference::performance).profile == OperatingProfile::balanced);

    auto desktop = fx::valid_snapshot();
    desktop.runtime.thermal = fx::known(ThermalPressure::fair, fx::measured(fx::kRuntimeProbe));
    REQUIRE(automatic(desktop) == ProfileDecision{OperatingProfile::balanced, ProfileRule::mains_power});
}

TEST_CASE("mains power derives performance only for a known desktop role") {
    REQUIRE(automatic(fx::valid_snapshot()) ==
            ProfileDecision{OperatingProfile::performance, ProfileRule::mains_power_desktop});
    REQUIRE(automatic(fx::hybrid_laptop()) == ProfileDecision{OperatingProfile::balanced, ProfileRule::mains_power});

    auto unknown_role = fx::valid_snapshot();
    unknown_role.hardware.platform_role =
        fx::unknown<PlatformRole>(fx::kSystemProbe, IssueCode::not_reported);
    REQUIRE(automatic(unknown_role).profile == OperatingProfile::balanced);
}

TEST_CASE("unavailable thermal evidence is not treated as pressure") {
    REQUIRE(fx::hybrid_laptop().runtime.thermal.knowledge() == Knowledge::unavailable);
    REQUIRE(automatic(fx::hybrid_laptop()).rule == ProfileRule::mains_power);
}

TEST_CASE("an unknown power source yields the safe local envelope") {
    auto snapshot = fx::valid_snapshot();
    snapshot.runtime.power_source = fx::unknown<PowerSource>(fx::kRuntimeProbe, IssueCode::timeout);
    REQUIRE(automatic(snapshot) ==
            ProfileDecision{OperatingProfile::safe_local_envelope, ProfileRule::unknown_power_source});
}

#include <catro/capabilities/snapshot_diff.hpp>

#include "fixtures/capability_fixtures.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <vector>

using namespace catro::capabilities;
namespace fx = catro::fixtures;

namespace {

std::vector<ChangeDomain> domains_of(const ChangeSet& changes) {
    std::vector<ChangeDomain> domains;
    for (std::size_t index = 0; index < kChangeDomainCount; ++index) {
        const auto domain = static_cast<ChangeDomain>(index);
        if (changes.contains(domain)) {
            domains.push_back(domain);
        }
    }
    return domains;
}

CapabilitySnapshot next_generation(CapabilitySnapshot snapshot) {
    ++snapshot.header.generation;
    return snapshot;
}

} // namespace

TEST_CASE("identical snapshots produce no changes") {
    const auto snapshot = fx::valid_snapshot();
    REQUIRE(diff_snapshots(snapshot, snapshot).empty());
}

TEST_CASE("enumeration order alone is not a change") {
    const auto before = fx::valid_snapshot();
    auto after = before;
    std::ranges::reverse(after.devices.encoders);
    std::ranges::reverse(after.devices.audio_endpoints);
    std::ranges::reverse(after.devices.transfer_paths);
    std::ranges::reverse(after.runtime.audio_endpoints);
    std::ranges::reverse(after.probes);

    REQUIRE(diff_snapshots(before, after).empty());
}

TEST_CASE("an audio default-role change reports only audio output") {
    const auto before = fx::valid_snapshot();
    auto after = before;
    auto& speakers = *std::ranges::find(after.runtime.audio_endpoints, fx::speakers(), &AudioEndpointState::endpoint);
    speakers.default_roles = fx::known(std::vector{AudioRole::multimedia}, fx::measured(fx::kAudioProbe));

    const auto changes = diff_snapshots(before, after);
    REQUIRE(domains_of(changes) == std::vector{ChangeDomain::audio_output});
    REQUIRE(changes.audio_endpoints == std::vector{fx::speakers()});
    REQUIRE(changes.gpus.empty());
    REQUIRE(changes.displays.empty());
}

TEST_CASE("a thermal transition reports only thermal") {
    const auto before = fx::valid_snapshot();
    auto after = before;
    after.runtime.thermal = fx::known(ThermalPressure::serious, fx::measured(fx::kRuntimeProbe));

    const auto changes = diff_snapshots(before, after);
    REQUIRE(domains_of(changes) == std::vector{ChangeDomain::thermal});
    REQUIRE(changes.audio_endpoints.empty());
    REQUIRE(changes.displays.empty());
}

TEST_CASE("a display mode change reports the affected display") {
    const auto before = fx::valid_snapshot();
    auto after = before;
    after.runtime.displays.front().active_mode = fx::known(
        DisplayMode{.pixels = {2560, 1440}, .logical = {2560, 1440}, .refresh_rate = Rational{60000, 1001}},
        fx::measured(fx::kGpuDisplayProbe));

    const auto changes = diff_snapshots(before, after);
    REQUIRE(domains_of(changes) == std::vector{ChangeDomain::display});
    REQUIRE(changes.displays == std::vector{fx::desktop_display()});
}

TEST_CASE("added and removed devices are affected") {
    const auto before = fx::valid_snapshot();
    auto after = before;
    after.devices.encoders.pop_back();
    after.devices.transfer_paths.pop_back();

    const auto changes = diff_snapshots(before, after);
    REQUIRE(domains_of(changes) == std::vector{ChangeDomain::encoder, ChangeDomain::capture});
    REQUIRE(changes.encoders == std::vector{fx::software_h264()});
    REQUIRE(changes.capture_paths == std::vector{fx::display_capture()});
}

TEST_CASE("snapshot-scoped identifiers mark the domain without claiming identity") {
    auto before = fx::valid_snapshot();
    before.devices.gpus.push_back(GpuCapability{
        .id = GpuId{"adapter-index-1", IdentityScope::snapshot},
        .vendor_id = fx::unknown<std::uint32_t>(fx::kGpuDisplayProbe, IssueCode::not_reported),
    });
    auto after = before;
    after.devices.gpus.back().vendor_id = fx::known(0x9999U, fx::advertised(fx::kGpuDisplayProbe));

    const auto changes = diff_snapshots(before, after);
    REQUIRE(domains_of(changes) == std::vector{ChangeDomain::gpu});
    REQUIRE(changes.gpus.empty());
}

TEST_CASE("probe timing and re-run generations are not changes, outcomes are") {
    const auto before = fx::valid_snapshot();
    auto after = before;
    for (auto& record : after.probes) {
        record.duration += std::chrono::microseconds{500};
        record.generation = 2;
    }
    after.header.generation = 2;
    REQUIRE(diff_snapshots(before, after).empty());

    after.probes.front().outcome = ProbeOutcome::partial;
    REQUIRE(domains_of(diff_snapshots(before, after)) == std::vector{ChangeDomain::validation});
}

TEST_CASE("power and session facts classify separately") {
    const auto before = fx::valid_snapshot();
    auto after = before;
    after.runtime.low_power_mode = fx::known(true, fx::measured(fx::kRuntimeProbe));
    after.runtime.remote_session = fx::known(true, fx::measured(fx::kRuntimeProbe));

    REQUIRE(domains_of(diff_snapshots(before, after)) ==
            std::vector{ChangeDomain::power, ChangeDomain::platform_session});
}

TEST_CASE("updates are built only from valid, newer replacements") {
    const auto previous = fx::valid_snapshot();

    auto invalid = next_generation(previous);
    invalid.devices.encoders.front().gpu = fx::known(GpuId{"missing", IdentityScope::snapshot}, fx::advertised(fx::kEncoderProbe));
    const auto rejected = make_snapshot_update(&previous, invalid);
    REQUIRE_FALSE(rejected.update.has_value());
    REQUIRE(rejected.validation.contains(ValidationCode::missing_gpu_reference));

    const auto stale = make_snapshot_update(&previous, previous);
    REQUIRE_FALSE(stale.update.has_value());
    REQUIRE(stale.validation.contains(ValidationCode::invalid_generation));

    auto hotter = next_generation(previous);
    for (auto& record : hotter.probes) {
        record.generation = hotter.header.generation;
    }
    hotter.runtime.thermal = fx::known(ThermalPressure::fair, fx::measured(fx::kRuntimeProbe));
    const auto accepted = make_snapshot_update(&previous, hotter);
    REQUIRE(accepted.update.has_value());
    REQUIRE(accepted.update->snapshot == hotter);
    REQUIRE(domains_of(accepted.update->changes) == std::vector{ChangeDomain::thermal});
}

TEST_CASE("the first publication reports every populated domain") {
    const auto first = make_snapshot_update(nullptr, fx::valid_snapshot());
    REQUIRE(first.update.has_value());
    const auto& changes = first.update->changes;
    REQUIRE(changes.contains(ChangeDomain::gpu));
    REQUIRE(changes.contains(ChangeDomain::display));
    REQUIRE(changes.contains(ChangeDomain::audio_input));
    REQUIRE(changes.contains(ChangeDomain::audio_output));
    REQUIRE(changes.contains(ChangeDomain::validation));
    REQUIRE(changes.gpus == std::vector{fx::desktop_gpu()});
}

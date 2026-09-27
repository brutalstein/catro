#include "fixtures/capability_fixtures.hpp"

#include <DiagnosticsViewModel.hpp>
#include <catro/capabilities/policy.hpp>
#include <catro/reporting/report.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

using namespace catro::capabilities;
using namespace catro::app;
using namespace std::chrono_literals;
namespace fx = catro::fixtures;
namespace reporting = catro::reporting;

namespace {

reporting::CapabilityReport report_for(CapabilitySnapshot snapshot) {
    return reporting::make_report(std::move(snapshot), fx::display_request());
}

bool mentions(const DiagnosticsModel& model, const std::string& text) {
    return std::ranges::any_of(model.sections, [&](const DiagnosticsSection& section) {
        return std::ranges::any_of(section.rows, [&](const DiagnosticsRow& row) {
            return row.label.find(text) != std::string::npos || row.value.find(text) != std::string::npos;
        });
    });
}

} // namespace

TEST_CASE("sections come in a fixed order, plan first") {
    const auto model = build_diagnostics(report_for(fx::valid_snapshot()), {});
    std::vector<std::string> ids;
    for (const auto& section : model.sections) {
        ids.push_back(section.id);
        CHECK_FALSE(section.title.empty());
    }
    CHECK(ids == std::vector<std::string>{"plan", "fallbacks", "decisions", "profile", "probes", "devices", "system",
                                          "runtime", "snapshot"});
    for (const auto& section : model.sections) {
        CHECK_FALSE(section.rows.empty());
    }
}

TEST_CASE("the headline follows the plan and probe outcomes") {
    SECTION("planned with every probe successful") {
        auto snapshot = fx::valid_snapshot();
        for (auto& record : snapshot.probes) {
            record.outcome = ProbeOutcome::success;
        }
        const auto model = build_diagnostics(report_for(std::move(snapshot)), {});
        CHECK(model.headline == "Planned");
        CHECK(model.tone == Tone::positive);
        CHECK(model.detail.find("Start: ") != std::string::npos);
    }

    SECTION("a timed-out probe turns a plan into a caution") {
        const auto model = build_diagnostics(report_for(fx::partial_probe_failure()), {});
        CHECK(model.tone != Tone::positive);
        CHECK(std::ranges::any_of(model.probes, [](const ProbeHealth& probe) { return probe.tone == Tone::critical; }));
    }

    SECTION("an unsupported policy version is invalid input") {
        const auto snapshot = fx::valid_snapshot();
        const reporting::CapabilityReport report{snapshot,
                                                 derive_media_plan(snapshot, fx::display_request(), PolicyVersion{9, 0, 0})};
        const auto model = build_diagnostics(report, {});
        CHECK(model.headline == "Invalid input");
        CHECK(model.tone == Tone::critical);
        CHECK_FALSE(model.detail.empty());
    }
}

TEST_CASE("probe health is sorted with locale-independent durations") {
    auto snapshot = fx::valid_snapshot();
    REQUIRE(snapshot.probes.size() >= 2);
    std::ranges::reverse(snapshot.probes);
    snapshot.probes[0].duration = 1450us;
    snapshot.probes[0].outcome = ProbeOutcome::timeout;
    const auto timed_out = snapshot.probes[0].probe_id;
    const auto model = build_diagnostics(report_for(std::move(snapshot)), {});

    CHECK(std::ranges::is_sorted(model.probes, {}, &ProbeHealth::probe_id));
    const auto found = std::ranges::find(model.probes, timed_out, &ProbeHealth::probe_id);
    REQUIRE(found != model.probes.end());
    CHECK(found->duration == "1.5 ms");
    CHECK(found->outcome == "timed out");
    CHECK(found->tone == Tone::critical);
}

TEST_CASE("changes are listed only after the first publication") {
    ChangeSet changes;
    changes.domains.set(static_cast<std::size_t>(ChangeDomain::encoder));
    changes.domains.set(static_cast<std::size_t>(ChangeDomain::power));
    changes.encoders.push_back({"windows:encoder:x", IdentityScope::persistent});

    auto first = fx::valid_snapshot();
    first.header.generation = 1;
    CHECK(build_diagnostics(report_for(std::move(first)), changes).changes.empty());

    auto later = fx::valid_snapshot();
    later.header.generation = 2;
    const auto model = build_diagnostics(report_for(std::move(later)), changes);
    CHECK(model.generation == 2);
    CHECK(model.changes == std::vector<std::string>{"encoder", "power", "encoder: windows:encoder:x"});
}

TEST_CASE("device names are redacted on screen and in the text export, never in JSON") {
    const auto snapshot = fx::valid_snapshot();
    REQUIRE_FALSE(snapshot.devices.gpus.empty());
    const auto name = snapshot.devices.gpus.front().name.value();
    REQUIRE(name);
    const auto report = report_for(snapshot);

    CHECK_FALSE(mentions(build_diagnostics(report, {}), *name));
    CHECK(export_report(report, ExportFormat::human).find(*name) == std::string::npos);
    CHECK(export_report(report, ExportFormat::json).find(*name) != std::string::npos);
}

TEST_CASE("an oversized section is bounded on screen and complete in the export") {
    auto snapshot = fx::valid_snapshot();
    REQUIRE_FALSE(snapshot.devices.audio_endpoints.empty());
    const auto endpoint = snapshot.devices.audio_endpoints.front();
    for (int index = 0; index < 400; ++index) {
        auto copy = endpoint;
        copy.id.value += ":copy" + std::to_string(index);
        snapshot.devices.audio_endpoints.push_back(std::move(copy));
    }
    const auto report = report_for(std::move(snapshot));
    const auto model = build_diagnostics(report, {});
    const auto devices = std::ranges::find(model.sections, "devices", &DiagnosticsSection::id);
    REQUIRE(devices != model.sections.end());
    CHECK(devices->rows.size() == kMaxSectionRows);
    CHECK(devices->omitted_rows > 0);
    CHECK(export_report(report, ExportFormat::json).find(":copy399") != std::string::npos);
}

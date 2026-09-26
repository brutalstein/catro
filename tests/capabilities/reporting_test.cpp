#include <catro/reporting/canonical_json.hpp>
#include <catro/reporting/human_report.hpp>
#include <catro/reporting/report.hpp>

#include "fixtures/capability_fixtures.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace catro::capabilities;
using namespace catro::reporting;
namespace fx = catro::fixtures;

namespace {

CapabilityReport report_for(CapabilitySnapshot snapshot, const MediaDecisionRequest& request = fx::display_request()) {
    return make_report(std::move(snapshot), request);
}

bool has(std::string_view text, std::string_view needle) {
    return text.find(needle) != std::string_view::npos;
}

std::size_t position(std::string_view text, std::string_view needle, std::size_t from = 0) {
    const auto found = text.find(needle, from);
    REQUIRE(found != std::string_view::npos);
    return found;
}

// Replaces the first occurrence, which must exist.
std::string edited(std::string text, std::string_view needle, std::string_view replacement) {
    const auto found = position(text, needle);
    text.replace(found, needle.size(), replacement);
    return text;
}

ReportParseError parse_error(std::string_view text) {
    const auto result = parse_report(text);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error);
    return *result.error;
}

MediaDecisionRequest hdr_request() {
    auto request = fx::display_request();
    request.quality.hdr = true;
    return request;
}

std::vector<CapabilityReport> every_report() {
    std::vector<CapabilityReport> reports;
    for (auto snapshot : {fx::valid_snapshot(), fx::high_end_desktop(), fx::intel_laptop_on_battery(), fx::hybrid_laptop(),
                          fx::apple_silicon_macbook(), fx::hot_apple_silicon(), fx::older_intel_mac(),
                          fx::headless_session(), fx::partial_probe_failure(), fx::software_only(), fx::missing_gpu_driver(),
                          fx::unknown_codec_limits(), fx::no_microphone(), fx::remote_session(),
                          fx::mixed_refresh_desktop(), fx::crowded_desktop(40)}) {
        reports.push_back(report_for(std::move(snapshot)));
    }
    reports.push_back(report_for(fx::hdr_desktop(), hdr_request()));
    auto mixed = fx::display_request();
    mixed.display = fx::extra_display(2);
    reports.push_back(report_for(fx::mixed_refresh_desktop(), mixed));
    // A plan that rejected its input still round-trips.
    reports.push_back({fx::valid_snapshot(), derive_media_plan(fx::valid_snapshot(), fx::display_request(), PolicyVersion{9, 0, 0})});
    return reports;
}

} // namespace

TEST_CASE("canonical JSON has a fixed shape and declaration field order") {
    const auto json = to_canonical_json(report_for(fx::valid_snapshot()));
    REQUIRE(json.starts_with("{\n  \"schema_id\": \"catro.capabilities\",\n  \"schema_version\": {\n"));
    REQUIRE(position(json, "\"schema_version\"") < position(json, "\"redaction\": \"none\""));
    REQUIRE(position(json, "\"redaction\"") < position(json, "\"snapshot\""));
    REQUIRE(position(json, "\"snapshot\"") < position(json, "\"plan\""));
    // Fields follow the domain declaration order, not alphabetical order.
    REQUIRE(position(json, "\"header\"") < position(json, "\"platform\""));
    REQUIRE(position(json, "\"platform\"") < position(json, "\"hardware\""));
    REQUIRE(position(json, "\"hardware\"") < position(json, "\"devices\""));
    REQUIRE(position(json, "\"devices\"") < position(json, "\"runtime\""));
    REQUIRE(position(json, "\"runtime\"") < position(json, "\"probes\""));
    REQUIRE(position(json, "\"probes\"") < position(json, "\"issues\""));
    REQUIRE(json.ends_with("}\n"));
    REQUIRE_FALSE(has(json, "\r"));
    REQUIRE(to_canonical_json(report_for(fx::valid_snapshot())) == json);
}

TEST_CASE("facts carry explicit knowledge, provenance, and stable enum names") {
    const auto json = to_canonical_json(report_for(fx::apple_silicon_macbook()));
    REQUIRE(has(json, "\"knowledge\": \"known\""));
    REQUIRE(has(json, "\"knowledge\": \"unknown\""));
    REQUIRE(has(json, "\"knowledge\": \"unavailable\""));
    REQUIRE(has(json, "\"issue\": \"not_reported\""));
    REQUIRE(has(json, "\"issue\": \"relationship_unprovable\""));
    REQUIRE(has(json, "\"issue\": null"));
    REQUIRE(has(json, "\"probe_id\": \"macos.gpu_display.v1\""));
    REQUIRE(has(json, "\"method\": \"advertised\""));
    REQUIRE(has(json, "\"confidence\": \"degraded\""));
    REQUIRE(has(json, "\"scope\": \"os_session\""));
    REQUIRE(has(json, "\"api\": \"screen_capture_kit\""));
    REQUIRE(has(json, "\"transfer\": \"same_resource\""));
    REQUIRE(has(json, "\"rule\": \"select_conservative_start\""));
    REQUIRE(has(json, "\"reasons\": [\n"));
}

TEST_CASE("rationals and quantities are exact integers with declared units") {
    auto request = fx::display_request();
    request.display = fx::extra_display(2);
    const auto json = to_canonical_json(report_for(fx::mixed_refresh_desktop(), request));
    REQUIRE(has(json, "\"numerator\": 60000,"));
    REQUIRE(has(json, "\"denominator\": 1001\n"));
    REQUIRE(has(json, "\"unix_microseconds\": 1790000000000000\n"));
    REQUIRE(has(json, "\"microseconds\": 12000\n"));
    REQUIRE(has(json, "\"bytes\": 34359738368\n"));
    REQUIRE_FALSE(has(json, "e+"));
}

TEST_CASE("inventories are sorted while plan ranking keeps its order") {
    REQUIRE(to_canonical_json(report_for(fx::shuffled(fx::valid_snapshot(), 7))) ==
            to_canonical_json(report_for(fx::valid_snapshot())));

    const auto json = to_canonical_json(report_for(fx::hybrid_laptop()));
    // Device inventory: sorted by identifier, so AV1 precedes H.264.
    const auto devices = position(json, "\"devices\"");
    REQUIRE(position(json, "\"mft:av1:hardware:dgpu\"", devices) < position(json, "\"mft:h264:hardware:dgpu\"", devices));
    // Fallbacks: ranked, so the conservative H.264 precedes AV1.
    const auto fallbacks = position(json, "\"fallbacks\"");
    REQUIRE(position(json, "\"mft:h264:hardware:dgpu\"", fallbacks) < position(json, "\"mft:av1:hardware:dgpu\"", fallbacks));
}

TEST_CASE("serialization writes raw UTF-8 and never rewrites recorded timestamps") {
    auto snapshot = fx::valid_snapshot();
    snapshot.devices.gpus.front().name = fx::known(std::string("Grafik \xC4\xB0\xC5\x9Flemci"), fx::advertised(fx::kGpuDisplayProbe));
    snapshot.header.captured_at = UtcTimestamp{std::chrono::microseconds{1'234'567}};
    const auto json = to_canonical_json(report_for(snapshot));
    REQUIRE(has(json, "\"Grafik \xC4\xB0\xC5\x9Flemci\""));
    REQUIRE(has(json, "\"unix_microseconds\": 1234567\n"));

    const auto parsed = parse_report(json);
    REQUIRE(parsed.ok());
    REQUIRE(parsed.report->snapshot.header.captured_at == snapshot.header.captured_at);
    REQUIRE(to_canonical_json(*parsed.report) == json);
}

TEST_CASE("every fixture report round-trips byte for byte") {
    for (const auto& report : every_report()) {
        const auto json = to_canonical_json(report);
        const auto parsed = parse_report(json);
        INFO((parsed.error ? parsed.error->path : std::string("no error")));
        REQUIRE(parsed.ok());
        REQUIRE(to_canonical_json(*parsed.report) == json);
        REQUIRE(parsed.report->plan == report.plan);
        if (report.plan.status != PlanStatus::invalid_input) {
            REQUIRE(derive_media_plan(parsed.report->snapshot, report.plan.request, kPolicyVersion) == report.plan);
        }
    }
}

TEST_CASE("parsing rejects malformed, oversized, and incompatible input") {
    const auto json = to_canonical_json(report_for(fx::valid_snapshot()));

    REQUIRE(parse_error("{").code == ReportErrorCode::malformed_json);
    REQUIRE(parse_error("[]").code == ReportErrorCode::invalid_type);
    REQUIRE(parse_error(std::string(kMaxReportBytes + 1, ' ')).code == ReportErrorCode::input_too_large);

    REQUIRE(parse_error(edited(json, "\"catro.capabilities\"", "\"catro.other\"")).code == ReportErrorCode::unsupported_schema);
    REQUIRE(parse_error(edited(json, "\"schema_version\": {\n    \"major\": 1", "\"schema_version\": {\n    \"major\": 2")).code ==
            ReportErrorCode::unsupported_schema);

    const auto teleport = parse_error(edited(json, "\"transfer\": \"same_resource\"", "\"transfer\": \"teleport\""));
    REQUIRE(teleport == ReportParseError{ReportErrorCode::invalid_enum, "snapshot.devices.transfer_paths[0].transfer"});

    REQUIRE(parse_error(edited(json, "\"redaction\": \"none\",", "\"redaction\": \"none\",\n  \"redaction\": \"none\",")).code ==
            ReportErrorCode::duplicate_key);
    REQUIRE(parse_error(edited(json, "\"redaction\": \"none\",", "\"redaction\": \"none\",\n  \"extra\": 1,")) ==
            ReportParseError{ReportErrorCode::unexpected_field, "extra"});
    REQUIRE(parse_error(edited(json, "  \"redaction\": \"none\",\n", "")) ==
            ReportParseError{ReportErrorCode::missing_field, "redaction"});
    REQUIRE(parse_error(edited(json, "\"redaction\": \"none\"", "\"redaction\": \"device_names\"")).code ==
            ReportErrorCode::invalid_value);

    REQUIRE(parse_error(edited(json, "\"generation\": 1,", "\"generation\": \"1\",")) ==
            ReportParseError{ReportErrorCode::invalid_type, "snapshot.header.generation"});
    REQUIRE(parse_error(edited(json, "\"generation\": 1,", "\"generation\": -1,")).code == ReportErrorCode::invalid_value);
    REQUIRE(parse_error(edited(json, "\"generation\": 1,", "\"generation\": 1.5,")).code == ReportErrorCode::invalid_type);

    // Rationals must already be reduced; 2/2 is not canonical.
    const auto unreduced = edited(edited(json, "\"numerator\": 1,", "\"numerator\": 2,"), "\"denominator\": 1\n", "\"denominator\": 2\n");
    REQUIRE(parse_error(unreduced).code == ReportErrorCode::invalid_value);
}

TEST_CASE("parsing rejects reports whose snapshot fails validation") {
    auto duplicate = fx::valid_snapshot();
    duplicate.devices.encoders.push_back(duplicate.devices.encoders.front());
    const auto duplicated = parse_report(to_canonical_json(report_for(duplicate)));
    REQUIRE_FALSE(duplicated.ok());
    REQUIRE(duplicated.error->code == ReportErrorCode::invalid_snapshot);
    REQUIRE(duplicated.validation.contains(ValidationCode::duplicate_id));

    auto contradictory = fx::valid_snapshot();
    auto provenance = fx::measured(fx::kSystemProbe);
    provenance.issue = IssueCode::timeout;
    contradictory.hardware.installed_memory = Observed<Bytes>::known(Bytes{1}, provenance);
    const auto contradicted = parse_report(to_canonical_json(report_for(contradictory)));
    REQUIRE_FALSE(contradicted.ok());
    REQUIRE(contradicted.error->code == ReportErrorCode::invalid_snapshot);
    REQUIRE(contradicted.validation.contains(ValidationCode::contradictory_evidence));
}

TEST_CASE("human reports redact device names by default and say so") {
    const auto report = report_for(fx::valid_snapshot());
    const auto text = to_human_report(report);
    REQUIRE(text.starts_with("Catro capability report\n"));
    REQUIRE(has(text, "Redaction: device names replaced; identifiers remain, so this report is not anonymous\n"));
    REQUIRE(has(text, "[redacted]"));
    REQUIRE_FALSE(has(text, "Discrete GPU"));
    REQUIRE_FALSE(has(text, "Hardware H.264 Encoder"));
    REQUIRE_FALSE(has(text, "Microphone"));
    REQUIRE(has(text, "Status: planned\n"));
    REQUIRE(has(text, "Selected: mft:h264:hardware:0"));
    REQUIRE(has(text, "Captured at: 2026-09-21T14:13:20.000000Z\n"));
    REQUIRE(has(text, "0x1234"));
    REQUIRE_FALSE(has(text, "\r"));
    REQUIRE(to_human_report(report) == text);

    const auto full = to_human_report(report, RedactionMode::none);
    REQUIRE(has(full, "Redaction: none\n"));
    REQUIRE(has(full, "Discrete GPU"));
}

TEST_CASE("human reports keep exact rates and explicit unknowns") {
    auto request = fx::display_request();
    request.display = fx::extra_display(2);
    REQUIRE(has(to_human_report(report_for(fx::mixed_refresh_desktop(), request)), "60000/1001"));

    const auto mac = to_human_report(report_for(fx::apple_silicon_macbook()));
    REQUIRE(has(mac, "unavailable (not_reported)"));
    REQUIRE(has(mac, "unknown (relationship_unprovable)"));

    const auto headless = to_human_report(report_for(fx::headless_session()));
    REQUIRE(has(headless, "Status: no_viable_path (no_source_display)\n"));
}

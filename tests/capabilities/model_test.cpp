#include <catro/capabilities/evidence.hpp>
#include <catro/capabilities/ids.hpp>
#include <catro/capabilities/model.hpp>
#include <catro/capabilities/rational.hpp>
#include <catro/capabilities/version.hpp>

#include <catch2/catch_test_macros.hpp>

#include <compare>
#include <type_traits>

namespace capabilities = catro::capabilities;

TEST_CASE("rational preserves 59.94 exactly") {
    const capabilities::Rational rate{60000, 1001};

    REQUIRE(rate.numerator() == 60000);
    REQUIRE(rate.denominator() == 1001);
    REQUIRE(rate.valid());
}

TEST_CASE("rational reduces without rounding") {
    const capabilities::Rational rate{148500000, 2475000};

    REQUIRE(rate.numerator() == 60);
    REQUIRE(rate.denominator() == 1);
    REQUIRE(capabilities::Rational{120000, 2002} == capabilities::Rational{60000, 1001});
}

TEST_CASE("rational orders exactly by value") {
    REQUIRE(capabilities::Rational{60000, 1001} < capabilities::Rational{60, 1});
    REQUIRE(capabilities::Rational{144, 1} > capabilities::Rational{120, 1});
    REQUIRE(std::is_eq(capabilities::Rational{3, 2} <=> capabilities::Rational{6, 4}));
}

TEST_CASE("rational with a zero denominator is representable but invalid") {
    const capabilities::Rational rate{60, 0};

    REQUIRE_FALSE(rate.valid());
    REQUIRE(rate.denominator() == 0);
}

TEST_CASE("known evidence retains its explicit typed value and provenance") {
    const auto provenance = capabilities::Provenance{
        .probe_id = "windows.system.v1",
        .method = capabilities::EvidenceMethod::measured,
        .confidence = capabilities::Confidence::high,
    };
    const auto memory = capabilities::Observed<capabilities::Bytes>::known(
        capabilities::Bytes{16ULL * 1024ULL * 1024ULL * 1024ULL}, provenance);

    REQUIRE(memory.knowledge() == capabilities::Knowledge::known);
    REQUIRE(memory.value() == capabilities::Bytes{16ULL * 1024ULL * 1024ULL * 1024ULL});
    REQUIRE(memory.provenance().method == capabilities::EvidenceMethod::measured);
    REQUIRE(memory.provenance().probe_id == "windows.system.v1");
}

TEST_CASE("unknown evidence carries its reason instead of a value") {
    const auto encoders = capabilities::Observed<std::uint32_t>::unknown(capabilities::Provenance{
        .probe_id = "windows.encoders.v1",
        .method = capabilities::EvidenceMethod::advertised,
        .confidence = capabilities::Confidence::degraded,
        .issue = capabilities::IssueCode::timeout,
    });

    REQUIRE(encoders.knowledge() == capabilities::Knowledge::unknown);
    REQUIRE_FALSE(encoders.value().has_value());
    REQUIRE(encoders.provenance().issue == capabilities::IssueCode::timeout);
}

TEST_CASE("default observations have no provenance so validation can reject them") {
    const capabilities::Observed<capabilities::Bytes> unset;

    REQUIRE(unset.knowledge() == capabilities::Knowledge::unknown);
    REQUIRE(unset.provenance().probe_id.empty());
}

TEST_CASE("device identifiers are distinct types with explicit scope") {
    STATIC_REQUIRE_FALSE(std::is_convertible_v<capabilities::GpuId, capabilities::DisplayId>);
    STATIC_REQUIRE_FALSE(std::is_convertible_v<capabilities::EncoderId, capabilities::CapturePathId>);

    const capabilities::GpuId session_gpu{"luid:0000:1234", capabilities::IdentityScope::os_session};
    const capabilities::GpuId snapshot_gpu{"luid:0000:1234", capabilities::IdentityScope::snapshot};

    REQUIRE(session_gpu != snapshot_gpu);
    REQUIRE(session_gpu.scope == capabilities::IdentityScope::os_session);
}

TEST_CASE("schema and policy versions are independent and explicit") {
    STATIC_REQUIRE(capabilities::kSchemaId == "catro.capabilities");
    STATIC_REQUIRE(capabilities::kSchemaVersion == capabilities::SchemaVersion{1, 0});
    STATIC_REQUIRE(capabilities::kPolicyVersion == capabilities::PolicyVersion{1, 0, 0});
    STATIC_REQUIRE(capabilities::is_supported(capabilities::SchemaVersion{1, 0}));
    STATIC_REQUIRE_FALSE(capabilities::is_supported(capabilities::SchemaVersion{2, 0}));
    STATIC_REQUIRE_FALSE(capabilities::is_supported(capabilities::SchemaVersion{1, 1}));
    STATIC_REQUIRE_FALSE(capabilities::is_supported(capabilities::PolicyVersion{1, 1, 0}));
}

TEST_CASE("a snapshot models multiple GPUs without a primary-GPU assumption") {
    capabilities::CapabilitySnapshot snapshot;
    snapshot.devices.gpus.push_back({.id = {"gpu-a", capabilities::IdentityScope::os_session}});
    snapshot.devices.gpus.push_back({.id = {"gpu-b", capabilities::IdentityScope::os_session}});

    REQUIRE(snapshot.header.schema_id == capabilities::kSchemaId);
    REQUIRE(snapshot.header.schema_version == capabilities::kSchemaVersion);
    REQUIRE(snapshot.devices.gpus.size() == 2);
}

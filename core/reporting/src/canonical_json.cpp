#include <catro/reporting/canonical_json.hpp>

#include "schema.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace catro::reporting {
namespace detail {
namespace {

template <class T, class Less = std::ranges::less>
void sort_set(caps::Observed<std::vector<T>>& set, Less less = {}) {
    if (auto values = set.value()) {
        std::ranges::sort(*values, less);
        set = caps::Observed<std::vector<T>>(set.knowledge(), std::move(values), set.provenance());
    }
}

bool mode_before(const caps::DisplayMode& lhs, const caps::DisplayMode& rhs) {
    return std::tie(lhs.pixels.width, lhs.pixels.height, lhs.logical.width, lhs.logical.height, lhs.refresh_rate) <
           std::tie(rhs.pixels.width, rhs.pixels.height, rhs.logical.width, rhs.logical.height, rhs.refresh_rate);
}

} // namespace

caps::CapabilitySnapshot canonical_order(caps::CapabilitySnapshot snapshot) {
    sort_set(snapshot.hardware.cpu.simd);
    auto& devices = snapshot.devices;
    std::ranges::sort(devices.gpus, {}, &caps::GpuCapability::id);
    for (auto& gpu : devices.gpus) {
        sort_set(gpu.graphics_apis);
    }
    std::ranges::sort(devices.encoders, {}, &caps::EncoderCapability::id);
    for (auto& encoder : devices.encoders) {
        std::ranges::sort(encoder.modes, {}, caps::mode_key);
    }
    std::ranges::sort(devices.capture_paths, {}, &caps::CapturePathCapability::id);
    for (auto& capture : devices.capture_paths) {
        sort_set(capture.output_formats);
    }
    std::ranges::sort(devices.displays, {}, &caps::DisplayCapability::id);
    for (auto& display : devices.displays) {
        sort_set(display.modes, mode_before);
    }
    std::ranges::sort(devices.audio_endpoints, {}, &caps::AudioEndpointCapability::id);
    for (auto& endpoint : devices.audio_endpoints) {
        sort_set(endpoint.sample_formats);
    }
    std::ranges::sort(devices.transfer_paths, [](const caps::TransferPathCapability& lhs, const caps::TransferPathCapability& rhs) {
        return std::tie(lhs.source, lhs.destination, lhs.source_gpu, lhs.destination_gpu) <
               std::tie(rhs.source, rhs.destination, rhs.source_gpu, rhs.destination_gpu);
    });
    for (auto& transfer : devices.transfer_paths) {
        sort_set(transfer.conversions);
    }
    auto& runtime = snapshot.runtime;
    std::ranges::sort(runtime.displays, {}, &caps::DisplayState::display);
    std::ranges::sort(runtime.audio_endpoints, {}, &caps::AudioEndpointState::endpoint);
    for (auto& state : runtime.audio_endpoints) {
        sort_set(state.default_roles);
    }
    std::ranges::sort(runtime.capture_permissions, {}, &caps::CapturePermissionState::path);
    std::ranges::sort(snapshot.probes, [](const caps::ProbeRecord& lhs, const caps::ProbeRecord& rhs) {
        return std::tie(lhs.probe_id, lhs.family) < std::tie(rhs.probe_id, rhs.family);
    });
    std::ranges::sort(snapshot.issues, [](const caps::ProbeIssue& lhs, const caps::ProbeIssue& rhs) {
        return std::tie(lhs.probe_id, lhs.code) < std::tie(rhs.probe_id, rhs.code);
    });
    return snapshot;
}

} // namespace detail

namespace {

using namespace detail;
using OrderedJson = nlohmann::ordered_json;
using Json = nlohmann::json;

constexpr std::string_view kRedactionNone = "none";
constexpr std::array kReportFields{"schema_id"sv, "schema_version"sv, "redaction"sv, "snapshot"sv, "plan"sv};

// --- encoding ----------------------------------------------------------------------------------
// Declared together so templates see every overload.

OrderedJson encode(bool value);
OrderedJson encode(const std::string& value);
template <std::integral I>
    requires(!std::same_as<I, bool>)
OrderedJson encode(I value);
template <NamedEnum E>
OrderedJson encode(E value);
OrderedJson encode(const caps::Rational& value);
OrderedJson encode(caps::Bytes value);
OrderedJson encode(caps::UtcTimestamp value);
OrderedJson encode(std::chrono::microseconds value);
template <class T>
OrderedJson encode(const std::optional<T>& value);
template <class T>
OrderedJson encode(const std::vector<T>& values);
template <class T>
OrderedJson encode(const caps::Observed<T>& observed);
template <Record T>
OrderedJson encode(const T& record);

OrderedJson encode(bool value) {
    return value;
}

OrderedJson encode(const std::string& value) {
    return value;
}

template <std::integral I>
    requires(!std::same_as<I, bool>)
OrderedJson encode(I value) {
    return value;
}

template <NamedEnum E>
OrderedJson encode(E value) {
    return std::string(enum_name(value));
}

// Exact values with their unit named by the key.
template <class I>
OrderedJson quantity(std::string_view unit, I value) {
    auto object = OrderedJson::object();
    object[std::string(unit)] = value;
    return object;
}

OrderedJson encode(const caps::Rational& value) {
    auto object = OrderedJson::object();
    object["numerator"] = value.numerator();
    object["denominator"] = value.denominator();
    return object;
}

OrderedJson encode(caps::Bytes value) {
    return quantity("bytes", value.value);
}

OrderedJson encode(caps::UtcTimestamp value) {
    return quantity("unix_microseconds", std::int64_t{value.time_since_epoch().count()});
}

OrderedJson encode(std::chrono::microseconds value) {
    return quantity("microseconds", std::int64_t{value.count()});
}

template <class T>
OrderedJson encode(const std::optional<T>& value) {
    return value ? encode(*value) : OrderedJson(nullptr);
}

template <class T>
OrderedJson encode(const std::vector<T>& values) {
    auto array = OrderedJson::array();
    for (const auto& value : values) {
        array.push_back(encode(value));
    }
    return array;
}

// Knowledge and provenance are always explicit; a value appears only when one is held.
template <class T>
OrderedJson encode(const caps::Observed<T>& observed) {
    auto object = OrderedJson::object();
    object["knowledge"] = encode(observed.knowledge());
    if (observed.value()) {
        object["value"] = encode(*observed.value());
    }
    object["provenance"] = encode(observed.provenance());
    return object;
}

template <Record T>
OrderedJson encode(const T& record) {
    auto object = OrderedJson::object();
    for_each_field(record, [&object](std::string_view name, const auto& value) { object[std::string(name)] = encode(value); });
    return object;
}

// --- decoding ----------------------------------------------------------------------------------

struct Failure {
    ReportErrorCode code;
    std::string path;
};

[[noreturn]] void fail(ReportErrorCode code, std::string path) {
    throw Failure{code, std::move(path)};
}

std::string member_path(const std::string& path, std::string_view name) {
    return path.empty() ? std::string(name) : path + "." + std::string(name);
}

void expect_object(const Json& json, const std::string& path) {
    if (!json.is_object()) {
        fail(ReportErrorCode::invalid_type, path);
    }
}

const Json& member(const Json& object, std::string_view name, const std::string& path) {
    const auto found = object.find(std::string(name));
    if (found == object.end()) {
        fail(ReportErrorCode::missing_field, member_path(path, name));
    }
    return *found;
}

// Unknown members are rejected rather than ignored; the first in key order is reported.
void reject_unexpected(const Json& object, std::span<const std::string_view> names, const std::string& path) {
    for (const auto& item : object.items()) {
        if (std::ranges::find(names, item.key()) == names.end()) {
            fail(ReportErrorCode::unexpected_field, member_path(path, item.key()));
        }
    }
}

template <Record T>
constexpr auto field_names() {
    return std::apply(
        [](const auto&... fields) { return std::array<std::string_view, sizeof...(fields)>{fields.name...}; },
        Schema<T>::fields);
}

void decode(const Json& json, bool& out, const std::string& path);
void decode(const Json& json, std::string& out, const std::string& path);
template <std::integral I>
    requires(!std::same_as<I, bool>)
void decode(const Json& json, I& out, const std::string& path);
template <NamedEnum E>
void decode(const Json& json, E& out, const std::string& path);
void decode(const Json& json, caps::Rational& out, const std::string& path);
void decode(const Json& json, caps::Bytes& out, const std::string& path);
void decode(const Json& json, caps::UtcTimestamp& out, const std::string& path);
void decode(const Json& json, std::chrono::microseconds& out, const std::string& path);
template <class T>
void decode(const Json& json, std::optional<T>& out, const std::string& path);
template <class T>
void decode(const Json& json, std::vector<T>& out, const std::string& path);
template <class T>
void decode(const Json& json, caps::Observed<T>& out, const std::string& path);
template <Record T>
void decode(const Json& json, T& out, const std::string& path);

void decode(const Json& json, bool& out, const std::string& path) {
    if (!json.is_boolean()) {
        fail(ReportErrorCode::invalid_type, path);
    }
    out = json.get<bool>();
}

void decode(const Json& json, std::string& out, const std::string& path) {
    if (!json.is_string()) {
        fail(ReportErrorCode::invalid_type, path);
    }
    out = json.get<std::string>();
}

template <std::integral I>
    requires(!std::same_as<I, bool>)
void decode(const Json& json, I& out, const std::string& path) {
    if (!json.is_number_integer()) {
        fail(ReportErrorCode::invalid_type, path);
    }
    if (json.is_number_unsigned()) {
        const auto value = json.get<std::uint64_t>();
        if (value > static_cast<std::make_unsigned_t<I>>(std::numeric_limits<I>::max())) {
            fail(ReportErrorCode::invalid_value, path);
        }
        out = static_cast<I>(value);
    } else if constexpr (std::is_unsigned_v<I>) {
        fail(ReportErrorCode::invalid_value, path);
    } else {
        const auto value = json.get<std::int64_t>();
        if (value < std::numeric_limits<I>::min()) {
            fail(ReportErrorCode::invalid_value, path);
        }
        out = static_cast<I>(value);
    }
}

template <NamedEnum E>
void decode(const Json& json, E& out, const std::string& path) {
    if (!json.is_string()) {
        fail(ReportErrorCode::invalid_type, path);
    }
    const auto value = enum_from<E>(json.get_ref<const std::string&>());
    if (!value) {
        fail(ReportErrorCode::invalid_enum, path);
    }
    out = *value;
}

template <std::integral I>
I decode_quantity(const Json& json, std::string_view unit, const std::string& path) {
    expect_object(json, path);
    I value{};
    decode(member(json, unit, path), value, member_path(path, unit));
    reject_unexpected(json, std::array{unit}, path);
    return value;
}

// Only reduced rationals are canonical; anything else would not serialize back identically.
void decode(const Json& json, caps::Rational& out, const std::string& path) {
    expect_object(json, path);
    std::uint32_t numerator = 0;
    std::uint32_t denominator = 0;
    decode(member(json, "numerator", path), numerator, member_path(path, "numerator"));
    decode(member(json, "denominator", path), denominator, member_path(path, "denominator"));
    reject_unexpected(json, std::array{"numerator"sv, "denominator"sv}, path);
    if (denominator != 0 && std::gcd(numerator, denominator) != 1) {
        fail(ReportErrorCode::invalid_value, path);
    }
    out = caps::Rational{numerator, denominator};
}

void decode(const Json& json, caps::Bytes& out, const std::string& path) {
    out.value = decode_quantity<std::uint64_t>(json, "bytes", path);
}

void decode(const Json& json, caps::UtcTimestamp& out, const std::string& path) {
    out = caps::UtcTimestamp{std::chrono::microseconds{decode_quantity<std::int64_t>(json, "unix_microseconds", path)}};
}

void decode(const Json& json, std::chrono::microseconds& out, const std::string& path) {
    out = std::chrono::microseconds{decode_quantity<std::int64_t>(json, "microseconds", path)};
}

template <class T>
void decode(const Json& json, std::optional<T>& out, const std::string& path) {
    if (json.is_null()) {
        out.reset();
        return;
    }
    T value{};
    decode(json, value, path);
    out = std::move(value);
}

template <class T>
void decode(const Json& json, std::vector<T>& out, const std::string& path) {
    if (!json.is_array()) {
        fail(ReportErrorCode::invalid_type, path);
    }
    out.clear();
    out.reserve(json.size());
    for (std::size_t index = 0; index < json.size(); ++index) {
        T value{};
        decode(json.at(index), value, path + "[" + std::to_string(index) + "]");
        out.push_back(std::move(value));
    }
}

template <class T>
void decode(const Json& json, caps::Observed<T>& out, const std::string& path) {
    expect_object(json, path);
    auto knowledge = caps::Knowledge::unknown;
    decode(member(json, "knowledge", path), knowledge, member_path(path, "knowledge"));
    std::optional<T> value;
    if (const auto found = json.find("value"); found != json.end()) {
        T decoded{};
        decode(*found, decoded, member_path(path, "value"));
        value = std::move(decoded);
    }
    caps::Provenance provenance;
    decode(member(json, "provenance", path), provenance, member_path(path, "provenance"));
    reject_unexpected(json, std::array{"knowledge"sv, "value"sv, "provenance"sv}, path);
    out = caps::Observed<T>(knowledge, std::move(value), std::move(provenance));
}

template <Record T>
void decode(const Json& json, T& out, const std::string& path) {
    expect_object(json, path);
    for_each_field(out, [&json, &path](std::string_view name, auto& value) {
        decode(member(json, name, path), value, member_path(path, name));
    });
    static constexpr auto names = field_names<T>();
    reject_unexpected(json, names, path);
}

// Plans are data, but their documented bounds still hold for anything read back.
void check_plan_bounds(const caps::MediaPlan& plan) {
    const auto& records = plan.trace.records;
    if (records.size() > caps::kMaxTraceRecords) {
        fail(ReportErrorCode::invalid_value, "plan.trace.records");
    }
    for (std::size_t index = 0; index < records.size(); ++index) {
        if (records[index].reasons.size() > caps::kMaxTraceReasons) {
            fail(ReportErrorCode::invalid_value, "plan.trace.records[" + std::to_string(index) + "].reasons");
        }
    }
    if (plan.fallbacks.size() >= caps::kMaxTraceCandidates) {
        fail(ReportErrorCode::invalid_value, "plan.fallbacks");
    }
}

CapabilityReport decode_report(const Json& document) {
    expect_object(document, "");
    // Identify the schema before reading anything whose shape depends on it.
    std::string schema_id;
    decode(member(document, "schema_id", ""), schema_id, "schema_id");
    if (schema_id != caps::kSchemaId) {
        fail(ReportErrorCode::unsupported_schema, "schema_id");
    }
    caps::SchemaVersion version;
    decode(member(document, "schema_version", ""), version, "schema_version");
    if (!caps::is_supported(version)) {
        fail(ReportErrorCode::unsupported_schema, "schema_version");
    }
    std::string redaction;
    decode(member(document, "redaction", ""), redaction, "redaction");
    if (redaction != kRedactionNone) {
        fail(ReportErrorCode::invalid_value, "redaction");
    }

    CapabilityReport report;
    decode(member(document, "snapshot", ""), report.snapshot, "snapshot");
    decode(member(document, "plan", ""), report.plan, "plan");
    reject_unexpected(document, kReportFields, "");
    report.snapshot.header.schema_id = std::move(schema_id);
    report.snapshot.header.schema_version = version;
    check_plan_bounds(report.plan);
    return report;
}

} // namespace

std::string to_canonical_json(const CapabilityReport& report) {
    const auto snapshot = canonical_order(report.snapshot);
    auto document = OrderedJson::object();
    document["schema_id"] = snapshot.header.schema_id;
    document["schema_version"] = encode(snapshot.header.schema_version);
    document["redaction"] = std::string(kRedactionNone);
    document["snapshot"] = encode(snapshot);
    document["plan"] = encode(report.plan);
    // Validated snapshots hold well-formed UTF-8; replacement only guards unvalidated input.
    return document.dump(2, ' ', false, OrderedJson::error_handler_t::replace) + "\n";
}

ReportParseResult parse_report(std::string_view text) {
    ReportParseResult result;
    const auto failed = [&result](ReportErrorCode code, std::string path) {
        result.error = ReportParseError{code, std::move(path)};
        return std::move(result);
    };
    if (text.size() > kMaxReportBytes) {
        return failed(ReportErrorCode::input_too_large, "");
    }

    // The JSON library keeps the last of repeated keys; untrusted input must not hide values.
    std::vector<std::set<std::string>> open_objects;
    bool duplicate_key = false;
    const Json::parser_callback_t detect_duplicates = [&](int, Json::parse_event_t event, Json& parsed) {
        if (event == Json::parse_event_t::object_start) {
            open_objects.emplace_back();
        } else if (event == Json::parse_event_t::object_end) {
            open_objects.pop_back();
        } else if (event == Json::parse_event_t::key && !open_objects.back().insert(parsed.get<std::string>()).second) {
            duplicate_key = true;
        }
        return true;
    };
    Json document;
    try {
        document = Json::parse(text.begin(), text.end(), detect_duplicates);
    } catch (const Json::parse_error&) {
        return failed(ReportErrorCode::malformed_json, "");
    }
    if (duplicate_key) {
        return failed(ReportErrorCode::duplicate_key, "");
    }

    try {
        auto report = decode_report(document);
        result.validation = caps::validate(report.snapshot);
        if (!result.validation.ok()) {
            return failed(ReportErrorCode::invalid_snapshot, "snapshot");
        }
        result.report = std::move(report);
        return result;
    } catch (const Failure& failure) {
        return failed(failure.code, failure.path);
    }
}

} // namespace catro::reporting

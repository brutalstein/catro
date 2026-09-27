#include <catro/reporting/human_report.hpp>

#include "schema.hpp"

#include <charconv>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace catro::reporting {
namespace {

using namespace detail;

// Integers go through std::to_string/std::to_chars, never streams, so no locale can group or
// translate digits.
std::string padded(std::int64_t value, std::size_t width) {
    auto digits = std::to_string(value);
    if (digits.size() < width) {
        digits.insert(0, width - digits.size(), '0');
    }
    return digits;
}

std::string hex(std::uint32_t value) {
    char buffer[8] = {};
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, 16);
    std::string digits(buffer, result.ptr);
    if (digits.size() < 4) {
        digits.insert(0, 4 - digits.size(), '0');
    }
    return "0x" + digits;
}

// --- one-line values ---------------------------------------------------------------------------
// Declared together so templates see every overload.

std::string text(bool value);
std::string text(const std::string& value);
template <std::integral I>
    requires(!std::same_as<I, bool>)
std::string text(I value);
template <NamedEnum E>
std::string text(E value);
std::string text(const caps::Rational& value);
std::string text(const caps::Dimensions& value);
std::string text(const caps::DimensionRange& value);
std::string text(const caps::RationalRange& value);
std::string text(const caps::DisplayMode& value);
std::string text(const caps::OsVersion& value);
std::string text(const caps::SchemaVersion& value);
std::string text(const caps::PolicyVersion& value);
std::string text(caps::Bytes value);
std::string text(caps::UtcTimestamp value);
std::string text(std::chrono::microseconds value);
template <class Tag>
std::string text(const caps::ScopedId<Tag>& value);
std::string text(const caps::EncoderModeKey& value);
std::string text(const caps::CandidateRef& value);
std::string text(const caps::SupportFact& value);
template <class T>
std::string text(const std::optional<T>& value);
template <class T>
std::string text(const std::vector<T>& values);
template <class T>
std::string text(const caps::Observed<T>& observed);

std::string text(bool value) {
    return value ? "yes" : "no";
}

std::string text(const std::string& value) {
    return value;
}

template <std::integral I>
    requires(!std::same_as<I, bool>)
std::string text(I value) {
    return std::to_string(value);
}

template <NamedEnum E>
std::string text(E value) {
    return std::string(enum_name(value));
}

// Exact: whole rates print as integers, others as numerator/denominator.
std::string text(const caps::Rational& value) {
    const auto numerator = std::to_string(value.numerator());
    return value.denominator() == 1 ? numerator : numerator + "/" + std::to_string(value.denominator());
}

std::string text(const caps::Dimensions& value) {
    return std::to_string(value.width) + "x" + std::to_string(value.height);
}

std::string text(const caps::DimensionRange& value) {
    return text(value.minimum) + ".." + text(value.maximum);
}

std::string text(const caps::RationalRange& value) {
    return text(value.minimum) + ".." + text(value.maximum);
}

std::string text(const caps::DisplayMode& value) {
    return text(value.pixels) + " (logical " + text(value.logical) + ") @ " + text(value.refresh_rate) + " Hz";
}

std::string text(const caps::OsVersion& value) {
    return std::to_string(value.major) + "." + std::to_string(value.minor) + "." + std::to_string(value.build);
}

std::string text(const caps::SchemaVersion& value) {
    return std::to_string(value.major) + "." + std::to_string(value.minor);
}

std::string text(const caps::PolicyVersion& value) {
    return std::to_string(value.major) + "." + std::to_string(value.minor) + "." + std::to_string(value.patch);
}

std::string text(caps::Bytes value) {
    return std::to_string(value.value) + " bytes";
}

// ISO 8601 in UTC with microseconds, computed from the recorded value alone.
std::string text(caps::UtcTimestamp value) {
    const auto days = std::chrono::floor<std::chrono::days>(value);
    const std::chrono::year_month_day date{days};
    const std::chrono::hh_mm_ss time{value - days};
    return padded(static_cast<int>(date.year()), 4) + "-" + padded(static_cast<unsigned>(date.month()), 2) + "-" +
           padded(static_cast<unsigned>(date.day()), 2) + "T" + padded(time.hours().count(), 2) + ":" +
           padded(time.minutes().count(), 2) + ":" + padded(time.seconds().count(), 2) + "." +
           padded(time.subseconds().count(), 6) + "Z";
}

std::string text(std::chrono::microseconds value) {
    return std::to_string(value.count()) + " us";
}

template <class Tag>
std::string text(const caps::ScopedId<Tag>& value) {
    return value.value;
}

std::string text(const caps::EncoderModeKey& value) {
    const auto part = [](const auto& optional) { return optional ? text(*optional) : std::string("?"); };
    return part(value.profile) + " " + text(value.input_format) + " " + text(value.chroma) + " " + part(value.bit_depth) +
           "-bit " + part(value.hdr);
}

std::string text(const caps::CandidateRef& value) {
    std::string result;
    const auto append = [&result](const std::string& part) { result += (result.empty() ? "" : " / ") + part; };
    if (value.capture) {
        append(text(*value.capture));
    }
    if (value.encoder) {
        append(text(*value.encoder));
    }
    if (value.mode) {
        append(text(*value.mode));
    }
    return result.empty() ? "request" : result;
}

std::string issue_suffix(const caps::Provenance& provenance) {
    return provenance.issue ? " (" + text(*provenance.issue) + ")" : "";
}

std::string text(const caps::SupportFact& value) {
    return text(value.status) + issue_suffix(value.provenance);
}

template <class T>
std::string text(const std::optional<T>& value) {
    return value ? text(*value) : "none";
}

template <class T>
std::string text(const std::vector<T>& values) {
    std::string result;
    for (const auto& value : values) {
        result += (result.empty() ? "" : ", ") + text(value);
    }
    return result.empty() ? "none" : result;
}

// Every fact shows its knowledge state; absent facts show why.
template <class T>
std::string text(const caps::Observed<T>& observed) {
    const auto& provenance = observed.provenance();
    if (observed.knowledge() != caps::Knowledge::known || !observed.value()) {
        return text(observed.knowledge()) + issue_suffix(provenance);
    }
    return text(*observed.value()) + (provenance.confidence == caps::Confidence::degraded ? " (degraded)" : "");
}

template <class T>
FactState state_of(const T&) {
    return FactState::plain;
}

FactState state_of(const caps::Provenance& provenance, bool known) {
    if (!known) {
        return FactState::unknown;
    }
    return provenance.confidence == caps::Confidence::degraded ? FactState::degraded : FactState::known;
}

template <class T>
FactState state_of(const caps::Observed<T>& observed) {
    if (observed.knowledge() == caps::Knowledge::unavailable) {
        return FactState::unavailable;
    }
    return state_of(observed.provenance(), observed.knowledge() == caps::Knowledge::known);
}

FactState state_of(const caps::SupportFact& fact) {
    return state_of(fact.provenance, fact.status != caps::Support::unknown);
}

// --- layout ------------------------------------------------------------------------------------

// Records print as indented blocks unless they read naturally on one line.
template <class T>
struct OneLine : std::bool_constant<!Record<T>> {};
template <class Tag>
struct OneLine<caps::ScopedId<Tag>> : std::true_type {};
template <class T>
struct OneLine<std::optional<T>> : OneLine<T> {};
template <class T>
struct OneLine<std::vector<T>> : OneLine<T> {};
template <class T>
struct OneLine<caps::Observed<T>> : std::true_type {};
template <>
struct OneLine<caps::SupportFact> : std::true_type {};
template <>
struct OneLine<caps::Dimensions> : std::true_type {};
template <>
struct OneLine<caps::DimensionRange> : std::true_type {};
template <>
struct OneLine<caps::RationalRange> : std::true_type {};
template <>
struct OneLine<caps::DisplayMode> : std::true_type {};
template <>
struct OneLine<caps::OsVersion> : std::true_type {};
template <>
struct OneLine<caps::SchemaVersion> : std::true_type {};
template <>
struct OneLine<caps::PolicyVersion> : std::true_type {};
template <>
struct OneLine<caps::EncoderModeKey> : std::true_type {};
template <>
struct OneLine<caps::CandidateRef> : std::true_type {};

template <class T>
struct IsOptional : std::false_type {};
template <class T>
struct IsOptional<std::optional<T>> : std::true_type {};
template <class T>
struct IsVector : std::false_type {};
template <class T>
struct IsVector<std::vector<T>> : std::true_type {};

class Writer {
public:
    explicit Writer(RedactionMode redaction) : redact_(redaction == RedactionMode::device_names) {}

    void emit(std::uint32_t depth, std::string_view label, std::optional<std::string> value,
              FactState state = FactState::plain) {
        rows_.push_back({depth, std::string(label), std::move(value), state, std::exchange(list_item_, false)});
    }

    template <class T>
    void field(std::uint32_t depth, std::string_view name, const T& value) {
        if constexpr (OneLine<T>::value) {
            emit(depth, name, field_text(name, value), state_of(value));
        } else if constexpr (IsOptional<T>::value) {
            if (value) {
                field(depth, name, *value);
            } else {
                emit(depth, name, "none");
            }
        } else if constexpr (IsVector<T>::value) {
            if (value.empty()) {
                emit(depth, name, "none");
                return;
            }
            emit(depth, name, std::nullopt);
            for (const auto& item : value) {
                list_item_ = true;
                record(depth + 2, item);
            }
        } else {
            emit(depth, name, std::nullopt);
            record(depth + 1, value);
        }
    }

    template <Record T>
    void record(std::uint32_t depth, const T& value) {
        for_each_field(value, [this, depth](std::string_view name, const auto& member) { this->field(depth, name, member); });
    }

    std::vector<PresentedRow> take() { return std::move(rows_); }

private:
    template <class T>
    std::string field_text(std::string_view name, const T& value) const {
        if constexpr (std::is_same_v<T, caps::Observed<std::string>>) {
            // Device display names are the only free text; identifiers remain.
            if (redact_ && name == "name" && value.knowledge() == caps::Knowledge::known) {
                return "[redacted]";
            }
        }
        if constexpr (std::is_same_v<T, caps::Observed<std::uint32_t>>) {
            if ((name == "vendor_id" || name == "device_id") && value.knowledge() == caps::Knowledge::known && value.value()) {
                return hex(*value.value());
            }
        }
        return text(value);
    }

    bool redact_;
    bool list_item_ = false;
    std::vector<PresentedRow> rows_;
};

void render(std::string& out, const std::vector<PresentedRow>& rows) {
    for (const auto& row : rows) {
        std::string indent(static_cast<std::size_t>(row.depth) * 2, ' ');
        if (row.list_item && indent.size() >= 2) {
            indent.replace(indent.size() - 2, 2, "- ");
        }
        out += indent;
        out += row.label;
        out += row.value ? ": " + *row.value : ":";
        out += '\n';
    }
}

std::string describe(const caps::MediaCandidate& candidate) {
    return text(candidate.encoder) + " via " + text(candidate.capture) + " (" + text(candidate.transfer) + "), " +
           text(candidate.mode) + ", " + text(candidate.resolution) + " @ " + text(candidate.frame_rate) + " Hz";
}

void summary(Writer& writer, const CapabilityReport& report) {
    const auto& snapshot = report.snapshot;
    const auto& plan = report.plan;
    std::chrono::microseconds probe_time{0};
    for (const auto& probe : snapshot.probes) {
        probe_time += probe.duration;
    }

    writer.emit(0, "Schema", snapshot.header.schema_id + " " + text(snapshot.header.schema_version));
    writer.emit(0, "Policy", text(plan.policy_version));
    writer.emit(0, "Captured at", text(snapshot.header.captured_at));
    writer.emit(0, "Generation", text(snapshot.header.generation));
    writer.emit(0, "Probes", std::to_string(snapshot.probes.size()) + " in " + text(probe_time) + ", " +
                                 std::to_string(snapshot.issues.size()) + " issues");
    writer.emit(0, "Status", text(plan.status) + (plan.reasons.empty() ? "" : " (" + text(plan.reasons) + ")"));
    if (plan.status == caps::PlanStatus::invalid_input) {
        return;
    }
    writer.emit(0, "Profile", text(plan.profile.profile) + " (" + text(plan.profile.rule) + ")");
    if (!plan.selected) {
        return;
    }
    writer.emit(0, "Selected", describe(*plan.selected));
    writer.emit(0, "Start", text(plan.start.resolution) + " @ " + text(plan.start.frame_rate) + " Hz, " +
                                text(plan.start.bit_depth) + "-bit " + (plan.start.hdr ? "HDR" : "SDR"));
    for (std::size_t index = 0; index < plan.fallbacks.size(); ++index) {
        writer.emit(0, "Fallback " + std::to_string(index + 1), describe(plan.fallbacks[index]));
    }
}

std::vector<PresentedRow> summary(const CapabilityReport& report) {
    Writer writer(RedactionMode::none);
    summary(writer, report);
    return writer.take();
}

template <Record T>
std::vector<PresentedRow> rows(std::uint32_t depth, const T& value, RedactionMode redaction) {
    Writer writer(redaction);
    writer.record(depth, value);
    return writer.take();
}

std::vector<PresentedRow> indented(std::vector<PresentedRow> rows) {
    for (auto& row : rows) {
        ++row.depth;
    }
    return rows;
}

// One section per top-level field; a heading row names its section, so nested rows move up.
template <Record T>
void split(std::vector<PresentedSection>& sections, std::string_view prefix, const T& value,
           RedactionMode redaction) {
    for_each_field(value, [&](std::string_view name, const auto& member) {
        Writer writer(redaction);
        writer.field(0, name, member);
        auto section_rows = writer.take();
        if (section_rows.size() > 1 && !section_rows.front().value) {
            section_rows.erase(section_rows.begin());
            for (auto& row : section_rows) {
                --row.depth;
            }
        }
        sections.push_back({std::string(prefix) + "." + std::string(name), std::move(section_rows)});
    });
}

} // namespace

std::string to_human_report(const CapabilityReport& report, RedactionMode redaction) {
    const CapabilityReport ordered{canonical_order(report.snapshot), report.plan};
    std::string out = "Catro capability report\n";
    out += redaction == RedactionMode::none
               ? "Redaction: none\n"
               : "Redaction: device names replaced; identifiers remain, so this report is not anonymous\n";
    out += "\nSummary\n";
    render(out, indented(summary(ordered)));
    out += "\nSnapshot\n";
    render(out, rows(1, ordered.snapshot, redaction));
    out += "\nPlan\n";
    render(out, rows(1, ordered.plan, redaction));
    return out;
}

std::vector<PresentedSection> present_report(const CapabilityReport& report, RedactionMode redaction) {
    const CapabilityReport ordered{canonical_order(report.snapshot), report.plan};
    std::vector<PresentedSection> sections;
    sections.push_back({"summary", summary(ordered)});
    split(sections, "snapshot", ordered.snapshot, redaction);
    split(sections, "plan", ordered.plan, redaction);
    return sections;
}

} // namespace catro::reporting

#pragma once

#include <optional>
#include <string>
#include <utility>

namespace catro::capabilities {

enum class Knowledge {
    known,
    unknown,
    unavailable,
};

enum class Support {
    supported,
    unsupported,
    unknown,
};

enum class EvidenceMethod {
    measured,
    advertised,
    probe_validated,
    inferred,
    cached,
};

enum class Confidence {
    high,
    degraded,
};

// Stable reasons for absent or degraded evidence and for probe issues.
enum class IssueCode {
    api_unavailable,
    permission_unavailable,
    timeout,
    os_failure,
    malformed_output,
    helper_terminated,
    not_reported,
    not_applicable,
    relationship_unprovable,
};

// The observation generation of a fact is the generation recorded by the probe run named here,
// so refreshing a family re-stamps one record instead of every fact it produced.
struct Provenance {
    std::string probe_id;
    EvidenceMethod method = EvidenceMethod::measured;
    Confidence confidence = Confidence::high;
    std::optional<IssueCode> issue;

    friend bool operator==(const Provenance&, const Provenance&) = default;
};

// A typed fact with explicit knowledge state. The raw constructor can express contradictory
// states (a known fact without a value, an unknown fact with one) because probe and report input
// is untrusted; validation rejects them. The named factories always build consistent facts.
// A default-constructed observation has no provenance and therefore never passes validation.
template <class T>
class Observed {
public:
    Observed() = default;

    Observed(Knowledge knowledge, std::optional<T> value, Provenance provenance)
        : knowledge_(knowledge), value_(std::move(value)), provenance_(std::move(provenance)) {}

    [[nodiscard]] static Observed known(T value, Provenance provenance) {
        return {Knowledge::known, std::move(value), std::move(provenance)};
    }

    [[nodiscard]] static Observed unknown(Provenance provenance) {
        return {Knowledge::unknown, std::nullopt, std::move(provenance)};
    }

    [[nodiscard]] static Observed unavailable(Provenance provenance) {
        return {Knowledge::unavailable, std::nullopt, std::move(provenance)};
    }

    [[nodiscard]] Knowledge knowledge() const noexcept { return knowledge_; }
    [[nodiscard]] const std::optional<T>& value() const noexcept { return value_; }
    [[nodiscard]] const Provenance& provenance() const noexcept { return provenance_; }

    friend bool operator==(const Observed&, const Observed&) = default;

private:
    Knowledge knowledge_ = Knowledge::unknown;
    std::optional<T> value_;
    Provenance provenance_;
};

struct SupportFact {
    Support status = Support::unknown;
    Provenance provenance;

    friend bool operator==(const SupportFact&, const SupportFact&) = default;
};

} // namespace catro::capabilities

#include <catro/reporting/canonical_json.hpp>
#include <catro/reporting/human_report.hpp>
#include <catro/reporting/report.hpp>

#include "fixtures/capability_fixtures.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <locale>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace catro::capabilities;
using namespace catro::reporting;
namespace fx = catro::fixtures;

namespace {

struct Golden {
    std::string_view name;
    CapabilitySnapshot snapshot;
};

// One canonical report per pathological fixture in the specification.
std::vector<Golden> goldens() {
    return {
        {"high_end_desktop", fx::high_end_desktop()},
        {"intel_laptop_on_battery", fx::intel_laptop_on_battery()},
        {"hybrid_laptop", fx::hybrid_laptop()},
        {"apple_silicon_macbook", fx::apple_silicon_macbook()},
        {"hot_apple_silicon", fx::hot_apple_silicon()},
        {"older_intel_mac", fx::older_intel_mac()},
        {"software_only", fx::software_only()},
        {"unknown_codec_limits", fx::unknown_codec_limits()},
        {"missing_gpu_driver", fx::missing_gpu_driver()},
        {"headless_session", fx::headless_session()},
        {"remote_session", fx::remote_session()},
        {"no_microphone", fx::no_microphone()},
        {"mixed_refresh_desktop", fx::mixed_refresh_desktop()},
        {"partial_probe_failure", fx::partial_probe_failure()},
    };
}

std::filesystem::path golden_path(std::string_view name) {
    return std::filesystem::path(CATRO_FIXTURE_DIR) / (std::string(name) + ".json");
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Turkish breaks naive case mapping (dotless i) and groups digits with '.', so it exposes any
// locale-sensitive formatting or name lookup.
std::optional<std::locale> turkish_locale() {
    for (const char* name : {"tr_TR.UTF-8", "tr_TR.utf8", "tr_TR", "tr-TR", "Turkish_Turkey.1254"}) {
        try {
            return std::locale(name);
        } catch (const std::runtime_error&) {
        }
    }
    return std::nullopt;
}

// Sets the C++ and C global locales for a scope.
class GlobalLocale {
public:
    explicit GlobalLocale(const std::locale& locale) : previous_(std::locale::global(locale)) {}
    GlobalLocale(const GlobalLocale&) = delete;
    GlobalLocale& operator=(const GlobalLocale&) = delete;
    ~GlobalLocale() { std::locale::global(previous_); }

private:
    std::locale previous_;
};

struct Baseline {
    CapabilitySnapshot snapshot;
    std::string json;
    std::string human;
};

std::vector<Baseline> baselines() {
    std::vector<Baseline> result;
    for (auto snapshot : {fx::valid_snapshot(), fx::hybrid_laptop(), fx::apple_silicon_macbook(), fx::older_intel_mac(),
                          fx::mixed_refresh_desktop(), fx::partial_probe_failure()}) {
        const auto report = make_report(snapshot, fx::display_request());
        result.push_back({std::move(snapshot), to_canonical_json(report), to_human_report(report)});
    }
    return result;
}

void require_identical_output(const std::vector<Baseline>& expected) {
    for (const auto& baseline : expected) {
        for (std::uint32_t seed = 1; seed <= 4; ++seed) {
            const auto report = make_report(fx::shuffled(baseline.snapshot, seed), fx::display_request());
            REQUIRE(to_canonical_json(report) == baseline.json);
            REQUIRE(to_human_report(report) == baseline.human);
        }
        const auto parsed = parse_report(baseline.json);
        REQUIRE(parsed.ok());
        REQUIRE(to_canonical_json(*parsed.report) == baseline.json);
    }
}

} // namespace

TEST_CASE("output is byte-identical across inventory order in the C locale") {
    const GlobalLocale classic(std::locale::classic());
    require_identical_output(baselines());
}

TEST_CASE("output is byte-identical under the Turkish locale") {
    const auto expected = baselines();
    const auto turkish = turkish_locale();
    if (!turkish) {
        WARN("No Turkish locale is installed; the Turkish locale leg did not run.");
        return;
    }
    const GlobalLocale scope(*turkish);
    require_identical_output(expected);
}

TEST_CASE("golden canonical reports are byte-stable") {
    for (const auto& golden : goldens()) {
        INFO(golden.name);
        const auto json = to_canonical_json(make_report(golden.snapshot, fx::display_request()));
        const auto stored = read_file(golden_path(golden.name));
        REQUIRE(stored == json);
        const auto parsed = parse_report(stored);
        REQUIRE(parsed.ok());
        REQUIRE(to_canonical_json(*parsed.report) == stored);
    }
}

// Hidden: run `catro_canonical_determinism_test "[.update-goldens]"` after an intentional
// policy or schema change, then review the fixture diff.
TEST_CASE("regenerate golden canonical reports", "[.update-goldens]") {
    for (const auto& golden : goldens()) {
        std::ofstream(golden_path(golden.name), std::ios::binary) << to_canonical_json(make_report(golden.snapshot, fx::display_request()));
    }
}

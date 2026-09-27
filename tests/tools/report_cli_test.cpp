#include "fixtures/capability_fixtures.hpp"

#include <catro/reporting/canonical_json.hpp>
#include <report_cli.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace catro::tools;
namespace fx = catro::fixtures;
namespace fs = std::filesystem;
namespace reporting = catro::reporting;

namespace {

std::optional<ReportOptions> parse(std::vector<std::string_view> arguments) {
    return parse_report_arguments(arguments);
}

reporting::CapabilityReport sample_report() {
    return reporting::make_report(fx::valid_snapshot(), fx::display_request());
}

std::string read_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// A fresh directory per test case, removed afterwards.
struct TemporaryDirectory {
    fs::path path;

    explicit TemporaryDirectory(std::string_view name)
        : path(fs::temp_directory_path() / ("catro-report-cli-" + std::string(name))) {
        fs::remove_all(path);
        fs::create_directories(path);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
};

std::size_t entries(const fs::path& directory) {
    return static_cast<std::size_t>(std::distance(fs::directory_iterator(directory), fs::directory_iterator()));
}

} // namespace

TEST_CASE("report arguments accept each option once") {
    CHECK(parse({}) == ReportOptions{});
    CHECK(parse({"--format", "json"}) == ReportOptions{.format = ReportFormat::json});
    CHECK(parse({"--format", "human", "--output", "r.txt"}) ==
          ReportOptions{.format = ReportFormat::human, .output = fs::path("r.txt")});

    CHECK_FALSE(parse({"--format"}));
    CHECK_FALSE(parse({"--format", "xml"}));
    CHECK_FALSE(parse({"--format", "json", "--format", "json"}));
    CHECK_FALSE(parse({"--output"}));
    CHECK_FALSE(parse({"--output", ""}));
    CHECK_FALSE(parse({"--output", "a", "--output", "b"}));
    CHECK_FALSE(parse({"--verbose"}));
    CHECK_FALSE(parse({"json"}));
}

TEST_CASE("the JSON rendering is the canonical report") {
    const auto report = sample_report();
    CHECK(render_report(report, ReportFormat::json) == reporting::to_canonical_json(report));
    CHECK(render_report(report, ReportFormat::human) != render_report(report, ReportFormat::json));
}

TEST_CASE("a report file is written once and never replaced") {
    const TemporaryDirectory directory("write");
    const auto target = directory.path / "report.json";

    CHECK_FALSE(write_new_file(target, "first"));
    CHECK(read_file(target) == "first");

    CHECK(write_new_file(target, "second"));
    CHECK(read_file(target) == "first");
    // No temporary sibling is left behind by either write.
    CHECK(entries(directory.path) == 1);

    CHECK(write_new_file(directory.path / "missing" / "report.json", "x"));
}

TEST_CASE("the report command maps each failure to its exit code") {
    const auto collect = [] { return std::optional{sample_report()}; };

    SECTION("invalid arguments print usage") {
        std::ostringstream out;
        std::ostringstream error;
        const std::vector<std::string_view> arguments{"--nope"};
        CHECK(run_report_cli(arguments, collect, out, error) == report_invalid_arguments);
        CHECK(out.str().empty());
        CHECK(error.str().find("usage:") != std::string::npos);
    }

    SECTION("no published snapshot") {
        std::ostringstream out;
        std::ostringstream error;
        const auto nothing = [] { return std::optional<reporting::CapabilityReport>{}; };
        CHECK(run_report_cli({}, nothing, out, error) == report_collection_failed);
        CHECK(out.str().empty());
        CHECK_FALSE(error.str().empty());
    }

    SECTION("standard output") {
        std::ostringstream out;
        std::ostringstream error;
        const std::vector<std::string_view> arguments{"--format", "json"};
        CHECK(run_report_cli(arguments, collect, out, error) == report_ok);
        CHECK(out.str().starts_with(reporting::to_canonical_json(sample_report())));
    }

    SECTION("an existing output file is kept") {
        const TemporaryDirectory directory("exit");
        const auto target = (directory.path / "report.txt").string();
        {
            std::ofstream existing(target);
            existing << "keep";
        }
        std::ostringstream out;
        std::ostringstream error;
        const std::vector<std::string_view> arguments{"--output", target};
        CHECK(run_report_cli(arguments, collect, out, error) == report_write_failed);
        CHECK(read_file(target) == "keep");
    }
}

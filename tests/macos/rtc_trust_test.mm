#include "macos_trust.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <iterator>
#include <string>

TEST_CASE("macOS RTC trust bundle exports system anchors as PEM") {
    const auto trust =
        catro::rtc::macos_system_trust_bundle();
    INFO(trust.error);
    REQUIRE(trust.error.empty());
    REQUIRE_FALSE(trust.path.empty());

    std::ifstream input(
        trust.path, std::ios::binary);
    REQUIRE(input.good());
    const std::string pem(
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{});
    CHECK(
        pem.find("-----BEGIN CERTIFICATE-----") !=
        std::string::npos);
    CHECK(
        pem.find("-----END CERTIFICATE-----") !=
        std::string::npos);
}

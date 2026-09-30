#include <catro/platform/macos/directory_client.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace catro;

TEST_CASE("macOS directory cancellation source shares stop state") {
    platform::macos::DirectoryCancellationSource source;
    const auto token = source.get_token();

    CHECK_FALSE(token.stop_requested());
    CHECK(source.request_stop());
    CHECK(token.stop_requested());
    CHECK_FALSE(source.request_stop());
}

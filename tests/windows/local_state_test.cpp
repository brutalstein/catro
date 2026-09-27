#include <catro/platform/windows/local_state.hpp>

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <thread>
#include <variant>

using namespace catro;

namespace {

class SequenceEntropy final : public community::EntropySource {
public:
    explicit SequenceEntropy(std::uint8_t seed) : next_(seed) {}

    bool fill(std::span<std::byte> destination) noexcept override {
        for (auto& value : destination) {
            value = static_cast<std::byte>(next_++);
            if (next_ == 0) {
                next_ = 1;
            }
        }
        return true;
    }

private:
    std::uint8_t next_;
};

class FailingEntropy final : public community::EntropySource {
public:
    bool fill(std::span<std::byte>) noexcept override {
        return false;
    }
};

struct TempStatePath {
    std::filesystem::path directory;
    std::filesystem::path state;

    TempStatePath() {
        directory = std::filesystem::temp_directory_path() /
                    ("catro-state-test-" + std::to_string(GetCurrentProcessId()) + "-" +
                     std::to_string(GetCurrentThreadId()));
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
        std::filesystem::create_directories(directory);
        state = directory / "state-v1.json";
    }

    ~TempStatePath() {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
};

} // namespace

TEST_CASE("Windows local state is created once and survives entropy becoming unavailable") {
    TempStatePath path;
    SequenceEntropy entropy(1);

    const auto created = platform::windows::load_or_create_local_state(path.state, entropy);
    REQUIRE(std::holds_alternative<community::LocalState>(created));
    const auto first = std::get<community::LocalState>(created);
    CHECK(std::filesystem::exists(path.state));

    FailingEntropy unavailable;
    const auto reopened = platform::windows::load_or_create_local_state(path.state, unavailable);
    REQUIRE(std::holds_alternative<community::LocalState>(reopened));
    CHECK(std::get<community::LocalState>(reopened) == first);
}

TEST_CASE("Windows local state never silently replaces corrupt identity data") {
    TempStatePath path;
    {
        std::ofstream output(path.state, std::ios::binary);
        output << "{";
    }

    SequenceEntropy entropy(9);
    const auto result = platform::windows::load_or_create_local_state(path.state, entropy);
    REQUIRE(std::holds_alternative<platform::windows::LocalStateError>(result));
    CHECK(std::get<platform::windows::LocalStateError>(result).code ==
          platform::windows::LocalStateErrorCode::invalid_state);

    std::ifstream input(path.state, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    CHECK(bytes == "{");
}

TEST_CASE("Windows atomic state save replaces a valid snapshot without changing identifiers") {
    TempStatePath path;
    SequenceEntropy entropy(17);
    auto created = platform::windows::load_or_create_local_state(path.state, entropy);
    REQUIRE(std::holds_alternative<community::LocalState>(created));

    auto changed = std::get<community::LocalState>(created);
    const auto original_user = changed.identity.id;
    const auto original_server = changed.personal_server.id;
    changed.identity.display_name = "Local User";
    changed.personal_server.name = "Local User's Server";

    CHECK_FALSE(platform::windows::save_local_state_atomic(path.state, changed));
    const auto loaded = platform::windows::load_local_state(path.state);
    REQUIRE(std::holds_alternative<community::LocalState>(loaded));
    const auto& round_trip = std::get<community::LocalState>(loaded);
    CHECK(round_trip.identity.display_name == "Local User");
    CHECK(round_trip.personal_server.name == "Local User's Server");
    CHECK(round_trip.identity.id == original_user);
    CHECK(round_trip.personal_server.id == original_server);
}

TEST_CASE("concurrent first-run opens converge on one persisted identity") {
    TempStatePath path;
    SequenceEntropy first_entropy(33);
    SequenceEntropy second_entropy(97);
    std::variant<community::LocalState, platform::windows::LocalStateError> first;
    std::variant<community::LocalState, platform::windows::LocalStateError> second;

    std::thread first_thread([&] {
        first = platform::windows::load_or_create_local_state(path.state, first_entropy);
    });
    std::thread second_thread([&] {
        second = platform::windows::load_or_create_local_state(path.state, second_entropy);
    });
    first_thread.join();
    second_thread.join();

    REQUIRE(std::holds_alternative<community::LocalState>(first));
    REQUIRE(std::holds_alternative<community::LocalState>(second));
    CHECK(std::get<community::LocalState>(first) == std::get<community::LocalState>(second));
}

TEST_CASE("Windows system entropy supplies bounded cryptographic random bytes") {
    platform::windows::SystemEntropy entropy;
    std::array<std::byte, 32> bytes{};
    CHECK(entropy.fill(bytes));
}

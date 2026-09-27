#include <catro/platform/macos/local_state.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <thread>
#include <variant>

#include <unistd.h>

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
        static std::atomic_uint64_t sequence{0};
        directory = std::filesystem::temp_directory_path() /
                    ("catro-state-test-" + std::to_string(getpid()) + "-" +
                     std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
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

TEST_CASE("macOS local state is created once and remains stable") {
    TempStatePath path;
    SequenceEntropy entropy(1);
    const auto created = platform::macos::load_or_create_local_state(path.state, entropy);
    REQUIRE(std::holds_alternative<community::LocalState>(created));
    const auto first = std::get<community::LocalState>(created);

    FailingEntropy unavailable;
    const auto reopened = platform::macos::load_or_create_local_state(path.state, unavailable);
    REQUIRE(std::holds_alternative<community::LocalState>(reopened));
    CHECK(std::get<community::LocalState>(reopened) == first);
}

TEST_CASE("macOS local state does not overwrite corrupt identity data") {
    TempStatePath path;
    {
        std::ofstream output(path.state, std::ios::binary);
        output << "{";
    }

    SequenceEntropy entropy(19);
    const auto result = platform::macos::load_or_create_local_state(path.state, entropy);
    REQUIRE(std::holds_alternative<platform::macos::LocalStateError>(result));
    CHECK(std::get<platform::macos::LocalStateError>(result).code ==
          platform::macos::LocalStateErrorCode::invalid_state);

    std::ifstream input(path.state, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    CHECK(bytes == "{");
}

TEST_CASE("concurrent macOS first-run opens converge on one identity") {
    TempStatePath path;
    SequenceEntropy first_entropy(41);
    SequenceEntropy second_entropy(101);
    std::variant<community::LocalState, platform::macos::LocalStateError> first;
    std::variant<community::LocalState, platform::macos::LocalStateError> second;

    std::thread first_thread([&] {
        first = platform::macos::load_or_create_local_state(path.state, first_entropy);
    });
    std::thread second_thread([&] {
        second = platform::macos::load_or_create_local_state(path.state, second_entropy);
    });
    first_thread.join();
    second_thread.join();

    REQUIRE(std::holds_alternative<community::LocalState>(first));
    REQUIRE(std::holds_alternative<community::LocalState>(second));
    CHECK(std::get<community::LocalState>(first) == std::get<community::LocalState>(second));
}

TEST_CASE("macOS system entropy supplies cryptographic random bytes") {
    platform::macos::SystemEntropy entropy;
    std::array<std::byte, 32> bytes{};
    CHECK(entropy.fill(bytes));
}

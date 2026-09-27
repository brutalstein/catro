#include <audio_check.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace catro::tools;
using namespace catro::audio;
using catro::capabilities::AudioEndpointId;
using catro::capabilities::IdentityScope;

namespace {

std::optional<AudioCheckOptions> parse(std::vector<std::string_view> arguments) {
    return parse_audio_check_arguments(arguments);
}

class Stream final : public AudioStream {
public:
    explicit Stream(StreamInfo info) : info_(std::move(info)) {}
    const StreamInfo& info() const noexcept override { return info_; }
    std::optional<AudioError> start() override { return std::nullopt; }
    std::uint64_t glitches() const noexcept override { return 0; }

private:
    StreamInfo info_;
};

// Opens streams that never call back, or fails every open with `error`.
class Platform final : public AudioPlatform {
public:
    OpenResult open_capture(const std::optional<AudioEndpointId>&, CaptureSink&, StreamFailure failure) override {
        last_failure = std::move(failure);
        if (error) {
            return *error;
        }
        return std::make_unique<Stream>(StreamInfo{{"in", IdentityScope::persistent}, 48000, 1, 480, 0});
    }
    OpenResult open_render(const std::optional<AudioEndpointId>&, RenderSource&, StreamFailure) override {
        if (error) {
            return *error;
        }
        return std::make_unique<Stream>(StreamInfo{{"out", IdentityScope::persistent}, 44100, 2, 441, 48});
    }

    std::optional<AudioError> error;
    StreamFailure last_failure;
};

} // namespace

TEST_CASE("audio-check arguments accept each option once") {
    const auto defaults = parse({});
    REQUIRE(defaults);
    CHECK(defaults->session.mode == SessionMode::meter);
    CHECK(defaults->duration == std::chrono::seconds(5));

    const auto full = parse({"--mode", "monitor", "--seconds", "0", "--input", "mmdevice:{a}", "--output", "b"});
    REQUIRE(full);
    CHECK(full->session.mode == SessionMode::monitor);
    CHECK(full->duration == std::chrono::seconds(0));
    CHECK(full->session.input->value == "mmdevice:{a}");
    CHECK(full->session.output->value == "b");

    CHECK_FALSE(parse({"--mode"}));
    CHECK_FALSE(parse({"--mode", "loud"}));
    CHECK_FALSE(parse({"--mode", "tone", "--mode", "tone"}));
    CHECK_FALSE(parse({"--seconds", "61"}));
    CHECK_FALSE(parse({"--seconds", "-1"}));
    CHECK_FALSE(parse({"--seconds", "2s"}));
    CHECK_FALSE(parse({"--input", ""}));
    CHECK_FALSE(parse({"--volume", "3"}));
}

TEST_CASE("audio-check reports streams, progress, and latency") {
    Platform platform;
    std::ostringstream out;
    std::ostringstream error;
    int waits = 0;
    const std::vector<std::string_view> arguments{"--mode", "monitor", "--seconds", "2"};
    const auto code = run_audio_check(arguments, platform, [&](auto) { ++waits; }, out, error);
    CHECK(code == audio_check_ok);
    CHECK(waits == 2);
    const auto text = out.str();
    CHECK(text.find("mode: monitor") != std::string::npos);
    CHECK(text.find("input: in (48000 Hz, 1 ch, period 480 frames") != std::string::npos);
    CHECK(text.find("2s: input -120.0 dBFS output -120.0 dBFS") != std::string::npos);
    // 0 + 480 + 921 target + 441 + 48 = 1890 frames at 48 kHz.
    CHECK(text.find("estimated latency: 39.4 ms") != std::string::npos);
    CHECK(error.str().find("headphones") != std::string::npos);
}

TEST_CASE("audio-check maps failures to exit codes") {
    Platform platform;
    std::ostringstream out;
    std::ostringstream error;
    const auto wait = [](auto) {};

    const std::vector<std::string_view> invalid{"--nope"};
    CHECK(run_audio_check(invalid, platform, wait, out, error) == audio_check_invalid_arguments);
    CHECK(error.str().find("usage:") != std::string::npos);

    platform.error = AudioError{AudioErrorCode::permission_denied, static_cast<std::int64_t>(0x80070005)};
    error.str("");
    CHECK(run_audio_check({}, platform, wait, out, error) == audio_check_audio_failed);
    CHECK(error.str() == "catro-audio-check: microphone access denied (0x80070005)\n");

    // A device lost during the run fails the check after the summary.
    platform.error.reset();
    error.str("");
    const std::vector<std::string_view> one{"--seconds", "1"};
    const auto lose = [&](auto) { platform.last_failure(AudioError{AudioErrorCode::device_lost}); };
    CHECK(run_audio_check(one, platform, lose, out, error) == audio_check_audio_failed);
    CHECK(error.str() == "catro-audio-check: device lost\n");
}

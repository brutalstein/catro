#include <catro/platform/macos/video_decoder.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

using namespace catro::platform::macos;

namespace {

class FakeDecoderAdapter final : public VideoDecoderNativeAdapter {
public:
    VideoDecoderStartResult start(
        const VideoDecoderConfig& config,
        OutputHandler on_output,
        FailureHandler on_failure) noexcept override {
        ++start_calls;
        started_config = config;
        output_handler = std::move(on_output);
        failure_handler = std::move(on_failure);
        return start_result;
    }

    std::optional<VideoDecoderError> decode(
        std::span<const std::byte> access_unit,
        std::int64_t pts_100ns) noexcept override {
        ++decode_calls;
        decoded_bytes.assign(access_unit.begin(), access_unit.end());
        decoded_pts = pts_100ns;
        return decode_error;
    }

    void stop() noexcept override {
        ++stop_calls;
    }

    VideoDecoderStartResult start_result{
        .hardware_accelerated = true,
        .implementation_name = "Fake hardware H.264",
    };
    std::optional<VideoDecoderError> decode_error;
    OutputHandler output_handler;
    FailureHandler failure_handler;
    VideoDecoderConfig started_config;
    std::vector<std::byte> decoded_bytes;
    std::int64_t decoded_pts = 0;
    int start_calls = 0;
    int decode_calls = 0;
    int stop_calls = 0;
};

constexpr std::array<std::byte, 5> keyframe{
    std::byte{0}, std::byte{0}, std::byte{0}, std::byte{1}, std::byte{0x65}};

NativeVideoFrame frame() {
    return NativeVideoFrame{
        .lease = std::make_shared<int>(1),
        .pixel_buffer = reinterpret_cast<void*>(1),
        .sequence = 4,
        .width = 1280,
        .height = 720,
        .pts_100ns = 900,
    };
}

} // namespace

TEST_CASE("macOS H264 decoder rejects invalid bounds before VideoToolbox") {
    auto adapter = std::make_unique<FakeDecoderAdapter>();
    auto* fake = adapter.get();
    MacH264HardwareDecoder decoder(std::move(adapter));
    VideoDecoderConfig config;
    config.max_access_unit_bytes = 3;

    const auto failure = decoder.start(config);

    REQUIRE(failure);
    CHECK(failure->code == VideoDecoderErrorCode::invalid_config);
    CHECK(fake->start_calls == 0);
}

TEST_CASE("macOS H264 decoder refuses a hidden software fallback") {
    auto adapter = std::make_unique<FakeDecoderAdapter>();
    auto* fake = adapter.get();
    fake->start_result.hardware_accelerated = false;
    MacH264HardwareDecoder decoder(std::move(adapter));

    const auto failure = decoder.start(VideoDecoderConfig{});

    REQUIRE(failure);
    CHECK(failure->code ==
          VideoDecoderErrorCode::hardware_decoder_unavailable);
    CHECK_FALSE(decoder.running());
    CHECK(fake->stop_calls == 1);
}

TEST_CASE("macOS H264 decoder rejects malformed access units before native decode") {
    auto adapter = std::make_unique<FakeDecoderAdapter>();
    auto* fake = adapter.get();
    MacH264HardwareDecoder decoder(std::move(adapter));
    REQUIRE_FALSE(decoder.start(VideoDecoderConfig{}));
    constexpr std::array<std::byte, 3> malformed{
        std::byte{1}, std::byte{2}, std::byte{3}};

    const auto failure = decoder.decode(malformed, 100);

    REQUIRE(failure);
    CHECK(failure->code == VideoDecoderErrorCode::malformed_access_unit);
    CHECK(fake->decode_calls == 0);
}

TEST_CASE("macOS H264 decoder publishes native frames and ignores stale callbacks") {
    auto adapter = std::make_unique<FakeDecoderAdapter>();
    auto* fake = adapter.get();
    MacH264HardwareDecoder decoder(std::move(adapter));
    NativeVideoFrame observed;
    int outputs = 0;
    REQUIRE_FALSE(decoder.start(
        VideoDecoderConfig{},
        [&](const NativeVideoFrame& decoded) {
            observed = decoded;
            ++outputs;
        }));

    REQUIRE_FALSE(decoder.decode(keyframe, 900));
    fake->output_handler(frame());
    CHECK(outputs == 1);
    CHECK(observed.width == 1280);
    CHECK(decoder.statistics().frames_decoded == 1);

    decoder.stop();
    fake->output_handler(frame());
    CHECK(outputs == 1);
}

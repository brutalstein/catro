#include <catro/platform/macos/video_encoder.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <utility>

using namespace catro::platform::macos;

namespace {

class FakeEncoderAdapter final : public VideoEncoderNativeAdapter {
public:
    VideoEncoderStartResult start(
        const VideoEncoderConfig& config,
        OutputHandler on_output,
        FailureHandler on_failure) noexcept override {
        ++start_calls;
        started_config = config;
        output_handler = std::move(on_output);
        failure_handler = std::move(on_failure);
        return start_result;
    }

    std::optional<VideoEncoderError> encode(
        const NativeVideoFrame& frame,
        bool force_keyframe) noexcept override {
        ++encode_calls;
        encoded_frame = frame;
        forced_keyframe = force_keyframe;
        return encode_error;
    }

    void stop() noexcept override {
        ++stop_calls;
    }

    VideoEncoderStartResult start_result{
        .hardware_accelerated = true,
        .implementation_name = "Fake hardware H.264",
    };
    std::optional<VideoEncoderError> encode_error;
    OutputHandler output_handler;
    FailureHandler failure_handler;
    VideoEncoderConfig started_config;
    NativeVideoFrame encoded_frame;
    int start_calls = 0;
    int encode_calls = 0;
    int stop_calls = 0;
    bool forced_keyframe = false;
};

NativeVideoFrame frame() {
    return NativeVideoFrame{
        .lease = std::make_shared<int>(1),
        .pixel_buffer = reinterpret_cast<void*>(1),
        .sequence = 7,
        .width = 1920,
        .height = 1080,
        .pts_100ns = 500,
    };
}

} // namespace

TEST_CASE("macOS H264 encoder validates even geometry bitrate and frame rate") {
    auto adapter = std::make_unique<FakeEncoderAdapter>();
    auto* fake = adapter.get();
    MacH264HardwareEncoder encoder(std::move(adapter));

    VideoEncoderConfig config;
    config.width = 1279;
    CHECK(encoder.start(config).value().code ==
          VideoEncoderErrorCode::invalid_config);
    config.width = 1280;
    config.bitrate = 0;
    CHECK(encoder.start(config).value().code ==
          VideoEncoderErrorCode::invalid_config);
    config.bitrate = 4'000'000;
    config.frame_rate = 0;
    CHECK(encoder.start(config).value().code ==
          VideoEncoderErrorCode::invalid_config);
    CHECK(fake->start_calls == 0);
}

TEST_CASE("macOS H264 encoder refuses a hidden software fallback") {
    auto adapter = std::make_unique<FakeEncoderAdapter>();
    auto* fake = adapter.get();
    fake->start_result.hardware_accelerated = false;
    fake->start_result.implementation_name = "Software";
    MacH264HardwareEncoder encoder(std::move(adapter));

    const auto failure = encoder.start(VideoEncoderConfig{});

    REQUIRE(failure);
    CHECK(failure->code ==
          VideoEncoderErrorCode::hardware_encoder_unavailable);
    CHECK_FALSE(encoder.running());
    CHECK(fake->stop_calls == 1);
}

TEST_CASE("macOS H264 encoder exposes keyframe access units and ignores stale output") {
    auto adapter = std::make_unique<FakeEncoderAdapter>();
    auto* fake = adapter.get();
    MacH264HardwareEncoder encoder(std::move(adapter));
    EncodedAccessUnit observed;
    int outputs = 0;
    REQUIRE_FALSE(encoder.start(
        VideoEncoderConfig{},
        [&](const EncodedAccessUnit& unit) {
            observed = unit;
            ++outputs;
        }));

    REQUIRE_FALSE(encoder.encode(frame(), true));
    CHECK(fake->forced_keyframe);
    fake->output_handler(EncodedAccessUnit{
        .bytes = {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{1},
                  std::byte{0x65}},
        .sequence = 7,
        .pts_100ns = 500,
        .keyframe = true,
    });

    CHECK(outputs == 1);
    CHECK(observed.keyframe);
    CHECK(observed.sequence == 7);
    CHECK(encoder.statistics().keyframes == 1);

    encoder.stop();
    fake->output_handler(EncodedAccessUnit{
        .bytes = {std::byte{1}},
        .sequence = 8,
    });
    CHECK(outputs == 1);
}

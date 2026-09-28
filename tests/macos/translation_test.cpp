#include "macos_translation.hpp"

#include <catro/platform/macos/audio_platform_contract.hpp>
#include <catro/capabilities/validation.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <vector>

using namespace catro::capabilities;
using namespace catro::platform::macos;

TEST_CASE("AUHAL capture uses the global callback element and input render bus") {
    CHECK(detail::kInputCallbackElement == 0);
    CHECK(detail::kInputRenderElement == 1);
}

TEST_CASE("microphone authorization requests undecided access and rejects denial") {
    using detail::MicrophoneAuthorization;
    using detail::MicrophoneAuthorizationAction;

    CHECK(detail::authorization_action(MicrophoneAuthorization::authorized) ==
          MicrophoneAuthorizationAction::open);
    CHECK(detail::authorization_action(MicrophoneAuthorization::not_determined) ==
          MicrophoneAuthorizationAction::request);
    CHECK(detail::authorization_action(MicrophoneAuthorization::denied) ==
          MicrophoneAuthorizationAction::deny);
    CHECK(detail::authorization_action(MicrophoneAuthorization::restricted) ==
          MicrophoneAuthorizationAction::deny);
}

namespace {

constexpr std::string_view kProbe = "macos.test.v1";

template <class T>
const T& require_known(const Observed<T>& fact) {
    REQUIRE(fact.knowledge() == Knowledge::known);
    REQUIRE(fact.value());
    return *fact.value();
}

template <class T>
void require_unknown(const Observed<T>& fact, IssueCode issue) {
    REQUIRE(fact.knowledge() == Knowledge::unknown);
    REQUIRE_FALSE(fact.value());
    REQUIRE(fact.provenance().issue == issue);
}

bool has_issue(const std::vector<ProbeIssue>& issues, IssueCode code) {
    return std::ranges::any_of(issues, [&](const ProbeIssue& issue) { return issue.code == code; });
}

NativeGpu apple_gpu() {
    return {.registry_id = 0x1000, .name = "Integrated GPU", .unified_memory = true, .working_set = 1ULL << 34};
}

NativeGpu discrete_gpu() {
    return {.registry_id = 0x2000, .name = "Discrete GPU", .vendor_id = 0x1002, .device_id = 0x7340};
}

NativeDisplayMode mode(std::uint32_t width, std::uint32_t height, double hertz) {
    return {.pixel_width = width * 2, .pixel_height = height * 2, .point_width = width, .point_height = height,
            .refresh_hz = hertz};
}

} // namespace

TEST_CASE("CoreGraphics refresh rates become exact rationals") {
    CHECK(exact_refresh(60.0) == Rational{60, 1});
    CHECK(exact_refresh(120.0004) == Rational{120, 1});
    CHECK(exact_refresh(59.94) == Rational{60000, 1001});
    CHECK(exact_refresh(29.97) == Rational{30000, 1001});
    CHECK(exact_refresh(23.976) == Rational{24000, 1001});
    CHECK(exact_refresh(47.5) == Rational{475, 10});
    CHECK_FALSE(exact_refresh(0.0));
    CHECK_FALSE(exact_refresh(-60.0));
    CHECK_FALSE(exact_refresh(std::numeric_limits<double>::quiet_NaN()));
    CHECK_FALSE(exact_refresh(std::numeric_limits<double>::infinity()));
}

TEST_CASE("identifiers are stable, typed, and scoped") {
    CHECK(gpu_id(0xABCULL) == GpuId{"metal:0000000000000abc", IdentityScope::os_session});
    CHECK(display_id(69733378) == DisplayId{"cgdisplay:69733378", IdentityScope::os_session});
    const NativeEncoder encoder{.codec = Codec::hevc, .encoder_id = "com.apple.VideoToolbox.HEVC", .hardware = true};
    CHECK(encoder_id(encoder) == EncoderId{"vt:hevc:hardware:com.apple.videotoolbox.hevc", IdentityScope::persistent});
}

TEST_CASE("Metal device traits map to a GPU kind as inferred evidence") {
    std::vector<ProbeIssue> issues;
    NativeGpu removable = discrete_gpu();
    removable.registry_id = 0x3000;
    removable.removable = true;
    NativeGpu low_power = discrete_gpu();
    low_power.registry_id = 0x4000;
    low_power.low_power = true;
    const auto facts = translate_gpu_display(
        {.gpus = std::vector{apple_gpu(), discrete_gpu(), removable, low_power, apple_gpu()}}, kProbe, issues);

    // The duplicate registry entry is dropped.
    REQUIRE(facts.gpus.size() == 4);
    CHECK(require_known(facts.gpus[0].kind) == GpuKind::integrated);
    CHECK(facts.gpus[0].kind.provenance().method == EvidenceMethod::inferred);
    CHECK(require_known(facts.gpus[1].kind) == GpuKind::discrete);
    CHECK(require_known(facts.gpus[2].kind) == GpuKind::external);
    CHECK(require_known(facts.gpus[3].kind) == GpuKind::integrated);

    // Metal reports neither dedicated nor shared memory; the working set is not passed off as either.
    require_unknown(facts.gpus[0].dedicated_memory, IssueCode::not_reported);
    require_unknown(facts.gpus[0].shared_memory, IssueCode::not_reported);
    require_unknown(facts.gpus[0].vendor_id, IssueCode::not_reported);
    CHECK(require_known(facts.gpus[1].vendor_id) == 0x1002U);
    CHECK(require_known(facts.gpus[0].graphics_apis) == std::vector{GraphicsApi::metal});
    CHECK(has_issue(issues, IssueCode::not_reported));
}

TEST_CASE("a display's GPU is claimed only when an enumerated Metal device drives it") {
    std::vector<ProbeIssue> issues;
    NativeGpuDisplay native{
        .gpus = std::vector{apple_gpu()},
        .displays = std::vector{
            NativeDisplay{.id = 1, .gpu = 0x1000, .active = mode(1512, 982, 120.0),
                          .modes = std::vector{mode(1512, 982, 120.0), mode(1512, 982, 120.0), mode(1512, 982, 0.0),
                                               mode(1728, 1117, 59.94)},
                          .main = true, .hdr_supported = true, .gamut = ColorGamut::display_p3,
                          .bits_per_channel = std::uint8_t{10}},
            NativeDisplay{.id = 2, .gpu = 0x9999},
            NativeDisplay{.id = 1},
        },
    };
    const auto facts = translate_gpu_display(native, kProbe, issues);

    REQUIRE(facts.displays.size() == 2);
    REQUIRE(facts.display_states.size() == 2);
    const auto& builtin = facts.displays[0];
    CHECK(require_known(builtin.gpu) == gpu_id(0x1000));
    // Duplicates and modes without a fixed rate are dropped.
    const auto& modes = require_known(builtin.modes);
    REQUIRE(modes.size() == 2);
    CHECK(modes[1].refresh_rate == Rational{60000, 1001});
    CHECK(builtin.hdr.status == Support::supported);
    CHECK(require_known(builtin.bits_per_channel) == 10);

    const auto& state = facts.display_states[0];
    CHECK(require_known(state.primary));
    CHECK(require_known(state.scale) == Rational{2, 1});
    CHECK(require_known(state.active_mode).refresh_rate == Rational{120, 1});
    require_unknown(state.hdr_enabled, IssueCode::not_reported);

    // Display 2 names a device the probe never enumerated.
    require_unknown(facts.displays[1].gpu, IssueCode::relationship_unprovable);
    require_unknown(facts.displays[1].modes, IssueCode::not_reported);
    CHECK(facts.displays[1].hdr.status == Support::unknown);
    CHECK(has_issue(issues, IssueCode::relationship_unprovable));
}

TEST_CASE("without a GPU enumeration no display relationship is claimed") {
    std::vector<ProbeIssue> issues;
    const auto facts =
        translate_gpu_display({.displays = std::vector{NativeDisplay{.id = 1, .gpu = 0x1000}}}, kProbe, issues);
    REQUIRE(facts.displays.size() == 1);
    require_unknown(facts.displays[0].gpu, IssueCode::relationship_unprovable);
}

TEST_CASE("ScreenCaptureKit paths and their permission never assume a grant") {
    std::vector<ProbeIssue> issues;
    const auto unasked = translate_gpu_display({.capture = {.screen_capture_kit = true}}, kProbe, issues);
    REQUIRE(unasked.capture_paths.size() == 3);
    for (const auto& path : unasked.capture_paths) {
        CHECK(path.api == CaptureApi::screen_capture_kit);
        CHECK(path.support.status == Support::supported);
        CHECK(require_known(path.output_formats) == std::vector{PixelFormat::bgra8, PixelFormat::nv12});
        require_unknown(path.gpu, IssueCode::relationship_unprovable);
    }
    REQUIRE(unasked.capture_permissions.size() == 3);
    // A false preflight cannot tell a denial from a question never asked.
    for (const auto& permission : unasked.capture_permissions) {
        require_unknown(permission.permission, IssueCode::not_reported);
    }

    const auto granted =
        translate_gpu_display({.capture = {.screen_capture_kit = true, .access_granted = true}}, kProbe, issues);
    for (const auto& permission : granted.capture_permissions) {
        CHECK(require_known(permission.permission) == CapturePermission::granted);
    }

    std::vector<ProbeIssue> absent_issues;
    const auto absent = translate_gpu_display({}, kProbe, absent_issues);
    REQUIRE(absent.capture_paths.size() == 3);
    CHECK(absent.capture_paths[0].support.status == Support::unsupported);
    CHECK(absent.capture_paths[0].output_formats.knowledge() == Knowledge::unavailable);
    CHECK(absent.capture_permissions.empty());
    CHECK(has_issue(absent_issues, IssueCode::not_applicable));
    CHECK_FALSE(has_issue(absent_issues, IssueCode::api_unavailable));
}

TEST_CASE("encoder GPUs and transfers are claimed only on proof") {
    const std::vector encoders{
        NativeEncoder{.codec = Codec::h264, .encoder_id = "com.apple.videotoolbox.videoencoder.ave.avc",
                      .name = "Apple H.264", .hardware = true, .gpu = 0x1000},
        NativeEncoder{.codec = Codec::h264, .encoder_id = "com.apple.videotoolbox.videoencoder.h264",
                      .name = "H.264 (software)"},
        NativeEncoder{.codec = Codec::hevc, .encoder_id = "com.apple.videotoolbox.videoencoder.ave.hevc",
                      .hardware = true, .gpu = 0x7777},
    };

    SECTION("every GPU shares memory: a hardware encoder reads captured surfaces in place") {
        std::vector<ProbeIssue> issues;
        const auto facts = translate_encoders(encoders, std::vector{apple_gpu()}, kProbe, issues);
        REQUIRE(facts.encoders.size() == 3);
        REQUIRE(facts.transfer_paths.size() == 9);

        CHECK(require_known(facts.encoders[0].gpu) == gpu_id(0x1000));
        CHECK(facts.encoders[0].backend == EncoderBackend::video_toolbox);
        CHECK(facts.encoders[1].gpu.knowledge() == Knowledge::unavailable);
        // The HEVC encoder names a registry entry this probe did not enumerate.
        require_unknown(facts.encoders[2].gpu, IssueCode::relationship_unprovable);
        require_unknown(facts.encoders[2].name, IssueCode::not_reported);

        const auto& hardware = facts.transfer_paths[0];
        CHECK(hardware.source.value == kScreenCaptureDisplay);
        CHECK(hardware.transfer == TransferKind::same_resource);
        CHECK(hardware.destination_gpu == gpu_id(0x1000));
        CHECK(hardware.evidence.status == Support::supported);
        CHECK(hardware.evidence.provenance.method == EvidenceMethod::inferred);

        const auto& software = facts.transfer_paths[3];
        CHECK(software.transfer == TransferKind::cpu_staging);
        CHECK_FALSE(software.destination_gpu);

        // Every transfer targets an encoder's own GPU.
        for (std::size_t index = 0; index < facts.transfer_paths.size(); ++index) {
            const auto& encoder = facts.encoders[index / 3];
            CHECK(facts.transfer_paths[index].destination == encoder.id);
            CHECK(facts.transfer_paths[index].destination_gpu == encoder.gpu.value());
        }
    }

    SECTION("a GPU with its own memory leaves the hardware transfer cost unknown") {
        std::vector<ProbeIssue> issues;
        const auto facts = translate_encoders(encoders, std::vector{apple_gpu(), discrete_gpu()}, kProbe, issues);
        CHECK(facts.transfer_paths[0].transfer == TransferKind::unknown);
        CHECK(facts.transfer_paths[0].evidence.status == Support::unknown);
        CHECK(facts.transfer_paths[3].transfer == TransferKind::cpu_staging);
        CHECK(has_issue(issues, IssueCode::relationship_unprovable));
    }

    SECTION("without a GPU enumeration nothing about hardware is claimed") {
        std::vector<ProbeIssue> issues;
        const auto facts = translate_encoders(encoders, std::nullopt, kProbe, issues);
        require_unknown(facts.encoders[0].gpu, IssueCode::relationship_unprovable);
        CHECK(facts.transfer_paths[0].transfer == TransferKind::unknown);
        CHECK_FALSE(facts.transfer_paths[0].destination_gpu);
    }
}

TEST_CASE("encoder modes are inferred, never measured") {
    std::vector<ProbeIssue> issues;
    const auto facts = translate_encoders(
        {NativeEncoder{.codec = Codec::av1, .encoder_id = "com.apple.av1", .hardware = true, .gpu = 0x1000}},
        std::vector{apple_gpu()}, kProbe, issues);
    REQUIRE(facts.encoders.size() == 1);
    REQUIRE(facts.encoders[0].modes.size() == 1);
    const auto& mode = facts.encoders[0].modes[0];
    CHECK(mode.codec == Codec::av1);
    CHECK(mode.input_format == PixelFormat::nv12);
    CHECK(require_known(mode.bit_depth) == 8);
    CHECK(mode.bit_depth.provenance().method == EvidenceMethod::inferred);
    require_unknown(mode.profile, IssueCode::not_reported);
    require_unknown(mode.dimensions, IssueCode::not_reported);
    CHECK(mode.low_latency.status == Support::unknown);
}

TEST_CASE("encoders without a usable identifier are dropped with an issue") {
    std::vector<ProbeIssue> issues;
    const auto facts = translate_encoders(
        {NativeEncoder{.encoder_id = ""}, NativeEncoder{.encoder_id = "bad id"},
         NativeEncoder{.encoder_id = std::string(200, 'x')}, NativeEncoder{.encoder_id = "ok"},
         NativeEncoder{.encoder_id = "OK"}},
        std::nullopt, kProbe, issues);
    // "ok" and "OK" lowercase to the same persistent identifier.
    REQUIRE(facts.encoders.size() == 1);
    CHECK(facts.encoders[0].id.value == "vt:h264:software:ok");
    CHECK(has_issue(issues, IssueCode::not_reported));
}

TEST_CASE("audio endpoints keep one default per direction as inferred roles") {
    std::vector<ProbeIssue> issues;
    const std::vector devices{
        NativeAudioDevice{.uid = "BuiltInSpeakerDevice", .direction = AudioDirection::output, .name = "Speakers",
                          .channels = 2, .sample_rate_hz = 48000, .sample_format = SampleFormat::pcm_f32,
                          .is_default = true},
        NativeAudioDevice{.uid = "BuiltInMicrophoneDevice", .direction = AudioDirection::input, .alive = false,
                          .channels = 0, .is_default = false},
        NativeAudioDevice{.uid = "Headset", .direction = AudioDirection::input, .channels = 1},
        NativeAudioDevice{.uid = "BuiltInSpeakerDevice", .direction = AudioDirection::output},
        NativeAudioDevice{.uid = "has space", .direction = AudioDirection::output},
    };
    const auto facts = translate_audio(devices, kProbe, issues);

    REQUIRE(facts.endpoints.size() == 3);
    REQUIRE(facts.states.size() == 3);
    const auto& speaker = facts.endpoints[0];
    CHECK(speaker.id == AudioEndpointId{"coreaudio:BuiltInSpeakerDevice:output", IdentityScope::persistent});
    CHECK(require_known(speaker.channels) == 2U);
    CHECK(require_known(speaker.sample_rate_hz) == 48000U);
    CHECK(require_known(speaker.sample_formats) == std::vector{SampleFormat::pcm_f32});
    CHECK(require_known(facts.states[0].default_roles).size() == 3);
    CHECK(facts.states[0].default_roles.provenance().method == EvidenceMethod::inferred);

    // Zero channels means not reported, never a device with no channels.
    require_unknown(facts.endpoints[1].channels, IssueCode::not_reported);
    require_unknown(facts.endpoints[1].name, IssueCode::not_reported);
    CHECK_FALSE(require_known(facts.states[1].active));
    CHECK(require_known(facts.states[1].default_roles).empty());

    // The default query failed for the headset.
    require_unknown(facts.states[2].default_roles, IssueCode::os_failure);
    CHECK(has_issue(issues, IssueCode::not_reported));
}

TEST_CASE("device names are bounded on a UTF-8 boundary") {
    std::vector<ProbeIssue> issues;
    // 255 ASCII bytes followed by a two-byte code point straddling the 256-byte bound.
    const auto name = std::string(kMaxTextBytes - 1, 'a') + "\xC3\xA9";
    const auto facts =
        translate_gpu_display({.gpus = std::vector{NativeGpu{.registry_id = 1, .name = name}}}, kProbe, issues);
    REQUIRE(facts.gpus.size() == 1);
    CHECK(require_known(facts.gpus[0].name) == std::string(kMaxTextBytes - 1, 'a'));
}

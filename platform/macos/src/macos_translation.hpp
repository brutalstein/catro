#pragma once

#include <catro/capabilities/probe.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Plain records of what the native macOS APIs reported, and their translation into domain
// facts. No Apple headers: this builds on every platform, tests build synthetic records, and
// the Objective-C++ probes fill real ones.
namespace catro::platform::macos {

namespace caps = catro::capabilities;

struct NativeGpu {
    // MTLDevice registryID: the IORegistry entry, stable until the next restart.
    std::uint64_t registry_id = 0;
    // UTF-8 device name.
    std::string name;
    // MTLDevice isLowPower / isRemovable / hasUnifiedMemory.
    bool low_power = false;
    bool removable = false;
    bool unified_memory = false;
    // recommendedMaxWorkingSetSize; absent when zero.
    std::optional<std::uint64_t> working_set;
    // PCI identity from the IORegistry; absent for GPUs that are not PCI devices.
    std::optional<std::uint32_t> vendor_id;
    std::optional<std::uint32_t> device_id;
};

struct NativeDisplayMode {
    std::uint32_t pixel_width = 0;
    std::uint32_t pixel_height = 0;
    std::uint32_t point_width = 0;
    std::uint32_t point_height = 0;
    // CGDisplayModeGetRefreshRate; zero when the display does not report a fixed rate.
    double refresh_hz = 0;
};

struct NativeDisplay {
    // CGDirectDisplayID, stable for the login session.
    std::uint32_t id = 0;
    // registryID of the Metal device driving the display; absent when CoreGraphics cannot say.
    std::optional<std::uint64_t> gpu;
    std::optional<NativeDisplayMode> active;
    // CGDisplayCopyAllDisplayModes; absent when the list could not be read.
    std::optional<std::vector<NativeDisplayMode>> modes;
    bool main = false;
    // EDR headroom above 1.0 on the matching NSScreen; absent when no screen matched.
    std::optional<bool> hdr_supported;
    std::optional<caps::ColorGamut> gamut;
    std::optional<std::uint8_t> bits_per_channel;
};

struct NativeCaptureApis {
    // ScreenCaptureKit is present (macOS 12.3 and later).
    bool screen_capture_kit = false;
    // CGPreflightScreenCaptureAccess: true only when access is granted. It cannot tell a denial
    // from a question never asked, so false stays unknown.
    bool access_granted = false;
};

struct NativeGpuDisplay {
    // Absent when the enumeration API itself failed.
    std::optional<std::vector<NativeGpu>> gpus;
    std::optional<std::vector<NativeDisplay>> displays;
    NativeCaptureApis capture;
};

struct NativeEncoder {
    caps::Codec codec = caps::Codec::h264;
    // kVTVideoEncoderList_EncoderID, e.g. "com.apple.videotoolbox.videoencoder.ave.avc".
    std::string encoder_id;
    // UTF-8 display name.
    std::string name;
    bool hardware = false;
    // kVTVideoEncoderList_GPURegistryID; absent when the list does not name a GPU.
    std::optional<std::uint64_t> gpu;
};

struct NativeAudioDevice {
    // kAudioDevicePropertyDeviceUID, persistent across restarts.
    std::string uid;
    caps::AudioDirection direction = caps::AudioDirection::output;
    std::string name;
    // kAudioDevicePropertyDeviceIsAlive.
    bool alive = true;
    std::optional<std::uint32_t> channels;
    std::optional<std::uint32_t> sample_rate_hz;
    std::optional<caps::SampleFormat> sample_format;
    // Whether this is the default device for its direction; absent when the query failed.
    std::optional<bool> is_default;
};

// Capture paths every gpu_display fragment publishes; encoder transfers start from these.
inline constexpr std::string_view kScreenCaptureDisplay = "sck:display";
inline constexpr std::string_view kScreenCaptureWindow = "sck:window";
inline constexpr std::string_view kScreenCaptureApplication = "sck:application";

[[nodiscard]] caps::GpuId gpu_id(std::uint64_t registry_id);
[[nodiscard]] caps::DisplayId display_id(std::uint32_t display);

// An exact rate for a CoreGraphics refresh rate: whole rates and the 1000/1001 broadcast rates
// are recognized; anything else is kept to the millihertz. Nothing for zero or invalid input.
[[nodiscard]] std::optional<caps::Rational> exact_refresh(double hertz);

// Relationships are claimed only when native data proves them: a display's GPU is known only
// when CoreGraphics names an enumerated Metal device. Every reason for absent or degraded
// evidence is also appended to `issues` once.
[[nodiscard]] caps::GpuDisplayProbeFacts translate_gpu_display(const NativeGpuDisplay& native,
                                                               std::string_view probe_id,
                                                               std::vector<caps::ProbeIssue>& issues);

[[nodiscard]] caps::EncoderId encoder_id(const NativeEncoder& encoder);

// Encoders are advertised by the encoder list, never instantiated. VideoToolbox converts any
// source buffer it is given, so each encoder gets an 8-bit NV12 mode as inferred evidence.
// An encoder's GPU is claimed only when it names one of `gpus`, the Metal devices enumerated in
// the same probe. ScreenCaptureKit delivers IOSurface-backed buffers: a software encoder reads
// them on the CPU, and a hardware encoder reads them in place only when every enumerated GPU
// shares system memory; otherwise, or without an enumeration, the transfer cost stays unknown.
[[nodiscard]] caps::EncoderProbeFacts translate_encoders(const std::vector<NativeEncoder>& encoders,
                                                         const std::optional<std::vector<NativeGpu>>& gpus,
                                                         std::string_view probe_id,
                                                         std::vector<caps::ProbeIssue>& issues);

[[nodiscard]] caps::AudioProbeFacts translate_audio(const std::vector<NativeAudioDevice>& devices,
                                                    std::string_view probe_id,
                                                    std::vector<caps::ProbeIssue>& issues);

} // namespace catro::platform::macos

#include <catro/platform/windows/capability_service.hpp>

#include <Windows.h>
#include <winternl.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace catro::platform::windows {
namespace {

namespace caps = catro::capabilities;

caps::Provenance measured(std::string_view probe_id) {
    return {.probe_id = std::string(probe_id), .method = caps::EvidenceMethod::measured};
}

caps::Provenance inferred(std::string_view probe_id, caps::IssueCode issue) {
    return {std::string(probe_id), caps::EvidenceMethod::inferred, caps::Confidence::degraded, issue};
}

caps::Provenance absent(std::string_view probe_id, caps::IssueCode issue) {
    return {std::string(probe_id), caps::EvidenceMethod::measured, caps::Confidence::degraded, issue};
}

template <class T>
caps::Observed<T> known(T value, std::string_view probe_id) {
    return caps::Observed<T>::known(std::move(value), measured(probe_id));
}

template <class T>
caps::Observed<T> unknown(std::string_view probe_id, caps::IssueCode issue) {
    return caps::Observed<T>::unknown(absent(probe_id, issue));
}

void add_issue(std::vector<caps::ProbeIssue>& issues, std::string_view probe_id, caps::IssueCode code) {
    const caps::ProbeIssue issue{std::string(probe_id), code};
    if (std::ranges::find(issues, issue) == issues.end()) {
        issues.push_back(issue);
    }
}

std::optional<caps::CpuArchitecture> architecture(USHORT machine) {
    if (machine == IMAGE_FILE_MACHINE_AMD64 || machine == PROCESSOR_ARCHITECTURE_AMD64) {
        return caps::CpuArchitecture::x86_64;
    }
    if (machine == IMAGE_FILE_MACHINE_ARM64 || machine == PROCESSOR_ARCHITECTURE_ARM64) {
        return caps::CpuArchitecture::arm64;
    }
    return std::nullopt;
}

caps::CpuArchitecture build_architecture() {
#if defined(_M_ARM64)
    return caps::CpuArchitecture::arm64;
#else
    return caps::CpuArchitecture::x86_64;
#endif
}

std::vector<caps::SimdFeature> simd_features() {
    std::vector<caps::SimdFeature> features;
#ifdef PF_SSE4_2_INSTRUCTIONS_AVAILABLE
    if (IsProcessorFeaturePresent(PF_SSE4_2_INSTRUCTIONS_AVAILABLE)) {
        features.push_back(caps::SimdFeature::sse4_2);
    }
#endif
#ifdef PF_AVX_INSTRUCTIONS_AVAILABLE
    if (IsProcessorFeaturePresent(PF_AVX_INSTRUCTIONS_AVAILABLE)) {
        features.push_back(caps::SimdFeature::avx);
    }
#endif
#ifdef PF_AVX2_INSTRUCTIONS_AVAILABLE
    if (IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE)) {
        features.push_back(caps::SimdFeature::avx2);
    }
#endif
#ifdef PF_AVX512F_INSTRUCTIONS_AVAILABLE
    if (IsProcessorFeaturePresent(PF_AVX512F_INSTRUCTIONS_AVAILABLE)) {
        features.push_back(caps::SimdFeature::avx512f);
    }
#endif
#ifdef PF_ARM_NEON_INSTRUCTIONS_AVAILABLE
    if (IsProcessorFeaturePresent(PF_ARM_NEON_INSTRUCTIONS_AVAILABLE)) {
        features.push_back(caps::SimdFeature::neon);
    }
#endif
    return features;
}

} // namespace

caps::ProbeFragment run_system_probe(const caps::ProbeSpec& spec) {
    const auto started = std::chrono::steady_clock::now();
    caps::ProbeFragment fragment{
        .probe_id = spec.probe_id,
        .family = spec.family,
        .revision = spec.revision,
        .outcome = caps::ProbeOutcome::partial,
    };
    std::optional<std::int64_t> first_error;
    const auto remember_error = [&](DWORD error) {
        if (!first_error) {
            first_error = error;
        }
        add_issue(fragment.issues, spec.probe_id, caps::IssueCode::os_failure);
    };

    caps::SystemProbeFacts facts;
    facts.platform.os = caps::OperatingSystem::windows;

    using RtlGetVersionFunction = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    const auto ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto rtl_get_version = ntdll == nullptr
                                     ? nullptr
                                     : reinterpret_cast<RtlGetVersionFunction>(GetProcAddress(ntdll, "RtlGetVersion"));
    RTL_OSVERSIONINFOW version{.dwOSVersionInfoSize = sizeof(RTL_OSVERSIONINFOW)};
    if (rtl_get_version != nullptr && rtl_get_version(&version) >= 0) {
        facts.platform.version = known(caps::OsVersion{version.dwMajorVersion, version.dwMinorVersion,
                                                       version.dwBuildNumber},
                                       spec.probe_id);
    } else {
        facts.platform.version = unknown<caps::OsVersion>(spec.probe_id, caps::IssueCode::api_unavailable);
        add_issue(fragment.issues, spec.probe_id, caps::IssueCode::api_unavailable);
    }

    auto native_architecture = std::optional<caps::CpuArchitecture>{};
    auto process_architecture = std::optional<caps::CpuArchitecture>{};
    using IsWow64Process2Function = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
    const auto kernel = GetModuleHandleW(L"kernel32.dll");
    const auto is_wow64_process2 = kernel == nullptr
                                        ? nullptr
                                        : reinterpret_cast<IsWow64Process2Function>(
                                              GetProcAddress(kernel, "IsWow64Process2"));
    if (is_wow64_process2 != nullptr) {
        USHORT process_machine = IMAGE_FILE_MACHINE_UNKNOWN;
        USHORT native_machine = IMAGE_FILE_MACHINE_UNKNOWN;
        if (is_wow64_process2(GetCurrentProcess(), &process_machine, &native_machine)) {
            native_architecture = architecture(native_machine);
            process_architecture = process_machine == IMAGE_FILE_MACHINE_UNKNOWN ? native_architecture
                                                                                 : architecture(process_machine);
        } else {
            remember_error(GetLastError());
        }
    }
    if (!native_architecture) {
        SYSTEM_INFO info{};
        GetNativeSystemInfo(&info);
        native_architecture = architecture(info.wProcessorArchitecture);
    }
    if (!process_architecture) {
        process_architecture = build_architecture();
    }

    auto& cpu = facts.hardware.cpu;
    cpu.native_architecture = native_architecture ? known(*native_architecture, spec.probe_id)
                                                  : unknown<caps::CpuArchitecture>(spec.probe_id,
                                                                                  caps::IssueCode::not_reported);
    cpu.process_architecture = process_architecture ? known(*process_architecture, spec.probe_id)
                                                    : unknown<caps::CpuArchitecture>(spec.probe_id,
                                                                                    caps::IssueCode::not_reported);
    if (native_architecture && process_architecture) {
        cpu.translation = caps::Observed<caps::TranslationState>::known(
            *native_architecture == *process_architecture ? caps::TranslationState::native
                                                          : caps::TranslationState::translated,
            {.probe_id = spec.probe_id, .method = caps::EvidenceMethod::inferred});
    } else {
        cpu.translation = unknown<caps::TranslationState>(spec.probe_id, caps::IssueCode::not_reported);
    }

    DWORD topology_bytes = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &topology_bytes);
    std::uint32_t physical_cores = 0;
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && topology_bytes != 0) {
        std::vector<std::byte> topology(topology_bytes);
        if (GetLogicalProcessorInformationEx(RelationProcessorCore,
                                             reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(topology.data()),
                                             &topology_bytes)) {
            for (DWORD offset = 0; offset < topology_bytes;) {
                const auto* entry = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(
                    topology.data() + offset);
                if (entry->Size == 0 || offset + entry->Size > topology_bytes) {
                    physical_cores = 0;
                    break;
                }
                if (entry->Relationship == RelationProcessorCore) {
                    ++physical_cores;
                }
                offset += entry->Size;
            }
        } else {
            remember_error(GetLastError());
        }
    } else {
        remember_error(GetLastError());
    }
    cpu.physical_cores = physical_cores != 0 ? known(physical_cores, spec.probe_id)
                                             : unknown<std::uint32_t>(spec.probe_id, caps::IssueCode::os_failure);

    const auto logical_cores = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (logical_cores != 0) {
        cpu.logical_cores = known(static_cast<std::uint32_t>(logical_cores), spec.probe_id);
    } else {
        remember_error(GetLastError());
        cpu.logical_cores = unknown<std::uint32_t>(spec.probe_id, caps::IssueCode::os_failure);
    }
    cpu.performance_cores = unknown<std::uint32_t>(spec.probe_id, caps::IssueCode::not_reported);
    cpu.efficiency_cores = unknown<std::uint32_t>(spec.probe_id, caps::IssueCode::not_reported);
    cpu.simd = known(simd_features(), spec.probe_id);
    add_issue(fragment.issues, spec.probe_id, caps::IssueCode::not_reported);

    MEMORYSTATUSEX memory{.dwLength = sizeof(MEMORYSTATUSEX)};
    if (GlobalMemoryStatusEx(&memory)) {
        facts.hardware.installed_memory = known(caps::Bytes{memory.ullTotalPhys}, spec.probe_id);
    } else {
        remember_error(GetLastError());
        facts.hardware.installed_memory = unknown<caps::Bytes>(spec.probe_id, caps::IssueCode::os_failure);
    }

    SYSTEM_POWER_STATUS power{};
    if (GetSystemPowerStatus(&power) && power.BatteryFlag != 255) {
        const auto role = (power.BatteryFlag & 128U) == 0 ? caps::PlatformRole::mobile : caps::PlatformRole::desktop;
        facts.hardware.platform_role = caps::Observed<caps::PlatformRole>::known(
            role, inferred(spec.probe_id, caps::IssueCode::relationship_unprovable));
        add_issue(fragment.issues, spec.probe_id, caps::IssueCode::relationship_unprovable);
    } else {
        facts.hardware.platform_role = unknown<caps::PlatformRole>(spec.probe_id, caps::IssueCode::not_reported);
    }

    fragment.system = std::move(facts);
    fragment.native_error = first_error;
    fragment.duration = std::max(std::chrono::microseconds{1},
                                 std::chrono::duration_cast<std::chrono::microseconds>(
                                     std::chrono::steady_clock::now() - started));
    return fragment;
}

} // namespace catro::platform::windows

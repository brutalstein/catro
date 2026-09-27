#include "../probe_support.hpp"

#import <Foundation/Foundation.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

namespace catro::platform::macos {
namespace {

caps::CpuArchitecture build_architecture() {
#if defined(__arm64__) || defined(__aarch64__)
    return caps::CpuArchitecture::arm64;
#else
    return caps::CpuArchitecture::x86_64;
#endif
}

// Features visible to this process; under Rosetta these are the translated x86 features.
std::vector<caps::SimdFeature> simd_features() {
    struct Key {
        const char* name;
        caps::SimdFeature feature;
    };
#if defined(__arm64__) || defined(__aarch64__)
    constexpr Key keys[] = {{"hw.optional.neon", caps::SimdFeature::neon}};
#else
    constexpr Key keys[] = {
        {"hw.optional.sse4_2", caps::SimdFeature::sse4_2},
        {"hw.optional.avx1_0", caps::SimdFeature::avx},
        {"hw.optional.avx2_0", caps::SimdFeature::avx2},
        {"hw.optional.avx512f", caps::SimdFeature::avx512f},
    };
#endif
    std::vector<caps::SimdFeature> features;
    for (const auto& key : keys) {
        // An undefined key means the feature is absent.
        int error = 0;
        if (sysctl_integer(key.name, error).value_or(0) == 1) {
            features.push_back(key.feature);
        }
    }
    return features;
}

} // namespace

caps::ProbeFragment run_system_probe(const caps::ProbeSpec& spec) {
    const auto started = std::chrono::steady_clock::now();
    auto fragment = begin_fragment(spec);
    std::optional<std::int64_t> first_error;
    const auto remember_error = [&](int error) {
        if (!first_error) {
            first_error = error;
        }
        add_issue(fragment.issues, spec.probe_id, caps::IssueCode::os_failure);
    };
    const auto count = [&](const char* name) -> caps::Observed<std::uint32_t> {
        int error = 0;
        const auto value = sysctl_integer(name, error);
        if (value && *value > 0) {
            return known(static_cast<std::uint32_t>(*value), spec.probe_id);
        }
        if (!value && error != ENOENT) {
            remember_error(error);
            return unknown<std::uint32_t>(spec.probe_id, caps::IssueCode::os_failure);
        }
        add_issue(fragment.issues, spec.probe_id, caps::IssueCode::not_reported);
        return unknown<std::uint32_t>(spec.probe_id, caps::IssueCode::not_reported);
    };

    caps::SystemProbeFacts facts;
    facts.platform.os = caps::OperatingSystem::macos;
    @autoreleasepool {
        const auto version = NSProcessInfo.processInfo.operatingSystemVersion;
        // macOS reports major.minor.patch; the patch level fills the build field.
        facts.platform.version = known(caps::OsVersion{static_cast<std::uint32_t>(version.majorVersion),
                                                       static_cast<std::uint32_t>(version.minorVersion),
                                                       static_cast<std::uint32_t>(version.patchVersion)},
                                       spec.probe_id);
    }

    // An arm64 process always runs natively. An x86_64 process runs under Rosetta exactly when
    // sysctl.proc_translated is 1; systems without Rosetta do not define the key.
    auto& cpu = facts.hardware.cpu;
    const auto process_architecture = build_architecture();
    cpu.process_architecture = known(process_architecture, spec.probe_id);
    std::optional<caps::TranslationState> translation;
    if (process_architecture == caps::CpuArchitecture::arm64) {
        translation = caps::TranslationState::native;
        cpu.translation = caps::Observed<caps::TranslationState>::known(
            *translation, {.probe_id = spec.probe_id, .method = caps::EvidenceMethod::inferred});
    } else {
        int error = 0;
        const auto translated = sysctl_integer("sysctl.proc_translated", error);
        if (translated) {
            translation = *translated == 1 ? caps::TranslationState::translated : caps::TranslationState::native;
            cpu.translation = known(*translation, spec.probe_id);
        } else if (error == ENOENT) {
            translation = caps::TranslationState::native;
            cpu.translation = caps::Observed<caps::TranslationState>::known(
                *translation, {.probe_id = spec.probe_id, .method = caps::EvidenceMethod::inferred});
        } else {
            remember_error(error);
            cpu.translation = unknown<caps::TranslationState>(spec.probe_id, caps::IssueCode::os_failure);
        }
    }
    if (translation) {
        const auto native = *translation == caps::TranslationState::translated ? caps::CpuArchitecture::arm64
                                                                                 : process_architecture;
        cpu.native_architecture = caps::Observed<caps::CpuArchitecture>::known(
            native, {.probe_id = spec.probe_id, .method = caps::EvidenceMethod::inferred});
    } else {
        cpu.native_architecture = unknown<caps::CpuArchitecture>(spec.probe_id, caps::IssueCode::os_failure);
    }

    cpu.physical_cores = count("hw.physicalcpu");
    cpu.logical_cores = count("hw.logicalcpu");
    // Performance level 0 is the fastest cluster. A single level means no efficiency cores.
    int levels_error = 0;
    const auto levels = sysctl_integer("hw.nperflevels", levels_error);
    if (levels && *levels >= 2) {
        cpu.performance_cores = count("hw.perflevel0.physicalcpu");
        cpu.efficiency_cores = count("hw.perflevel1.physicalcpu");
    } else if (levels && *levels == 1) {
        cpu.performance_cores = cpu.physical_cores;
        cpu.efficiency_cores = known(std::uint32_t{0}, spec.probe_id);
    } else {
        if (!levels && levels_error != ENOENT) {
            remember_error(levels_error);
        }
        cpu.performance_cores = unknown<std::uint32_t>(spec.probe_id, caps::IssueCode::not_reported);
        cpu.efficiency_cores = unknown<std::uint32_t>(spec.probe_id, caps::IssueCode::not_reported);
        add_issue(fragment.issues, spec.probe_id, caps::IssueCode::not_reported);
    }
    cpu.simd = known(simd_features(), spec.probe_id);

    int memory_error = 0;
    const auto memory = sysctl_integer("hw.memsize", memory_error);
    if (memory && *memory > 0) {
        facts.hardware.installed_memory = known(caps::Bytes{static_cast<std::uint64_t>(*memory)}, spec.probe_id);
    } else {
        remember_error(memory ? EINVAL : memory_error);
        facts.hardware.installed_memory = unknown<caps::Bytes>(spec.probe_id, caps::IssueCode::os_failure);
    }

    // A battery suggests a portable machine but does not prove one.
    if (const auto battery = internal_battery_present()) {
        facts.hardware.platform_role = caps::Observed<caps::PlatformRole>::known(
            *battery ? caps::PlatformRole::mobile : caps::PlatformRole::desktop,
            inferred(spec.probe_id, caps::IssueCode::relationship_unprovable));
        add_issue(fragment.issues, spec.probe_id, caps::IssueCode::relationship_unprovable);
    } else {
        facts.hardware.platform_role = unknown<caps::PlatformRole>(spec.probe_id, caps::IssueCode::not_reported);
        add_issue(fragment.issues, spec.probe_id, caps::IssueCode::not_reported);
    }

    if (fragment.issues.empty()) {
        fragment.outcome = caps::ProbeOutcome::success;
    }
    fragment.system = std::move(facts);
    fragment.native_error = first_error;
    finish_fragment(fragment, started);
    return fragment;
}

} // namespace catro::platform::macos

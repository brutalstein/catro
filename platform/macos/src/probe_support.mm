#include "probe_support.hpp"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/ps/IOPSKeys.h>
#include <IOKit/ps/IOPowerSources.h>
#include <sys/sysctl.h>

#include <cerrno>
#include <cstddef>
#include <cstring>

namespace catro::platform::macos {
namespace {

// A 32-bit little-endian IORegistry property of the entry or, for a GPU, its PCI parent.
std::optional<std::uint32_t> pci_property(io_registry_entry_t entry, CFStringRef key) {
    const auto value = static_cast<CFTypeRef>(IORegistryEntrySearchCFProperty(
        entry, kIOServicePlane, key, kCFAllocatorDefault, kIORegistryIterateRecursively | kIORegistryIterateParents));
    if (value == nullptr) {
        return std::nullopt;
    }
    std::optional<std::uint32_t> result;
    if (CFGetTypeID(value) == CFDataGetTypeID() &&
        CFDataGetLength(static_cast<CFDataRef>(value)) >= static_cast<CFIndex>(sizeof(std::uint32_t))) {
        const auto* bytes = CFDataGetBytePtr(static_cast<CFDataRef>(value));
        result = std::uint32_t{bytes[0]} | std::uint32_t{bytes[1]} << 8U | std::uint32_t{bytes[2]} << 16U |
                 std::uint32_t{bytes[3]} << 24U;
    }
    CFRelease(value);
    return result;
}

void add_pci_identity(NativeGpu& gpu) {
    // IOServiceGetMatchingService consumes the matching dictionary.
    const io_service_t service =
        IOServiceGetMatchingService(kIOMainPortDefault, IORegistryEntryIDMatching(gpu.registry_id));
    if (service == IO_OBJECT_NULL) {
        return;
    }
    gpu.vendor_id = pci_property(service, CFSTR("vendor-id"));
    gpu.device_id = pci_property(service, CFSTR("device-id"));
    IOObjectRelease(service);
}

} // namespace


std::optional<std::int64_t> sysctl_integer(const char* name, int& error) {
    std::int64_t wide = 0;
    std::size_t size = sizeof(wide);
    if (sysctlbyname(name, &wide, &size, nullptr, 0) != 0) {
        error = errno;
        return std::nullopt;
    }
    if (size == sizeof(std::int32_t)) {
        std::int32_t narrow = 0;
        std::memcpy(&narrow, &wide, sizeof(narrow));
        return narrow;
    }
    if (size == sizeof(std::int64_t)) {
        return wide;
    }
    error = EINVAL;
    return std::nullopt;
}

std::optional<bool> internal_battery_present() {
    const CFTypeRef info = IOPSCopyPowerSourcesInfo();
    if (info == nullptr) {
        return std::nullopt;
    }
    const CFArrayRef sources = IOPSCopyPowerSourcesList(info);
    if (sources == nullptr) {
        CFRelease(info);
        return std::nullopt;
    }
    bool present = false;
    for (CFIndex index = 0; index < CFArrayGetCount(sources) && !present; ++index) {
        // Get rule: the description is owned by `info`.
        const CFDictionaryRef description = IOPSGetPowerSourceDescription(info, CFArrayGetValueAtIndex(sources, index));
        if (description == nullptr) {
            continue;
        }
        const auto type = static_cast<CFStringRef>(CFDictionaryGetValue(description, CFSTR(kIOPSTypeKey)));
        present = type != nullptr && CFGetTypeID(type) == CFStringGetTypeID() &&
                  CFStringCompare(type, CFSTR(kIOPSInternalBatteryType), 0) == kCFCompareEqualTo;
    }
    CFRelease(sources);
    CFRelease(info);
    return present;
}

std::optional<std::vector<NativeGpu>> enumerate_gpus() {
    @autoreleasepool {
        NSArray<id<MTLDevice>>* devices = MTLCopyAllDevices();
        if (devices == nil) {
            return std::nullopt;
        }
        std::vector<NativeGpu> gpus;
        for (id<MTLDevice> device in devices) {
            NativeGpu gpu{
                .registry_id = device.registryID,
                .name = device.name.UTF8String != nullptr ? device.name.UTF8String : "",
                .low_power = static_cast<bool>(device.lowPower),
                .removable = static_cast<bool>(device.removable),
                .unified_memory = static_cast<bool>(device.hasUnifiedMemory),
            };
            if (device.recommendedMaxWorkingSetSize > 0) {
                gpu.working_set = device.recommendedMaxWorkingSetSize;
            }
            add_pci_identity(gpu);
            gpus.push_back(std::move(gpu));
        }
        return gpus;
    }
}

} // namespace catro::platform::macos

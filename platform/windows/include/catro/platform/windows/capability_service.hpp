#pragma once

#include <catro/capabilities/probe_coordinator.hpp>
#include <catro/capabilities/snapshot_diff.hpp>

#include <filesystem>
#include <functional>
#include <memory>

namespace catro::platform::windows {

class ProcessProbeExecutor final : public capabilities::ProbeExecutor {
public:
    explicit ProcessProbeExecutor(std::filesystem::path helper_path);

    [[nodiscard]] std::unique_ptr<capabilities::RunningProbe>
    start(const capabilities::ProbeSpec& spec) override;

private:
    std::filesystem::path helper_path_;
};

// Dispatches one passive family in the helper process. Unsupported families produce an
// explicit API-unavailable fragment; discovery never starts capture or requests permission.
[[nodiscard]] capabilities::ProbeFragment run_passive_probe(const capabilities::ProbeSpec& spec);

class CapabilityService {
public:
    using UpdateCallback = std::function<void(capabilities::SnapshotUpdate)>;

    explicit CapabilityService(std::filesystem::path helper_path);
    ~CapabilityService();

    CapabilityService(const CapabilityService&) = delete;
    CapabilityService& operator=(const CapabilityService&) = delete;

    void start(UpdateCallback callback);
    void refresh(capabilities::RefreshReason reason);
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace catro::platform::windows

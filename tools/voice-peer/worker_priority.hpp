#pragma once

#include <memory>

namespace catro::tools {

// Best-effort priority boost for the codec/network worker. Audio callbacks already have their own
// native real-time scheduling. Failure to elevate never prevents a voice session from running.
class VoiceWorkerPriority {
public:
    VoiceWorkerPriority() noexcept;
    ~VoiceWorkerPriority();

    VoiceWorkerPriority(const VoiceWorkerPriority&) = delete;
    VoiceWorkerPriority& operator=(const VoiceWorkerPriority&) = delete;

    [[nodiscard]] bool elevated() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace catro::tools

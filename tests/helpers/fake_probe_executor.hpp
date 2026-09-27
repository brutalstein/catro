#pragma once

#include <catro/capabilities/probe_coordinator.hpp>

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace catro::tests {

class FakeProbeExecutor final : public capabilities::ProbeExecutor {
public:
    struct Behavior {
        std::optional<capabilities::ProbeFragment> fragment;
        std::chrono::milliseconds ready_after{0};
        bool never_completes = false;
    };

    void set(std::string probe_id, Behavior behavior) {
        behaviors_.insert_or_assign(std::move(probe_id), std::move(behavior));
    }

    [[nodiscard]] std::unique_ptr<capabilities::RunningProbe> start(const capabilities::ProbeSpec& spec) override {
        launched_.push_back(spec.probe_id);
        auto found = behaviors_.find(spec.probe_id);
        Behavior behavior;
        if (found != behaviors_.end()) {
            behavior = found->second;
        } else {
            behavior.fragment = capabilities::ProbeFragment{
                .probe_id = spec.probe_id,
                .family = spec.family,
                .revision = spec.revision,
                .outcome = capabilities::ProbeOutcome::helper_terminated,
            };
        }
        return std::make_unique<Task>(*this, spec.probe_id, std::move(behavior));
    }

    [[nodiscard]] const std::vector<std::string>& launched() const noexcept { return launched_; }
    [[nodiscard]] const std::vector<std::string>& terminated() const noexcept { return terminated_; }
    [[nodiscard]] std::size_t launch_count_at_first_wait() const noexcept { return launch_count_at_first_wait_; }

private:
    class Task final : public capabilities::RunningProbe {
    public:
        Task(FakeProbeExecutor& owner, std::string probe_id, Behavior behavior)
            : owner_(owner), probe_id_(std::move(probe_id)), behavior_(std::move(behavior)),
              ready_at_(std::chrono::steady_clock::now() + behavior_.ready_after) {}

        [[nodiscard]] std::optional<capabilities::ProbeFragment>
        wait_until(std::chrono::steady_clock::time_point deadline) override {
            if (owner_.launch_count_at_first_wait_ == 0) {
                owner_.launch_count_at_first_wait_ = owner_.launched_.size();
            }
            if (behavior_.never_completes || ready_at_ > deadline) {
                std::this_thread::sleep_until(deadline);
                return std::nullopt;
            }
            std::this_thread::sleep_until(ready_at_);
            return behavior_.fragment;
        }

        void terminate() noexcept override { owner_.terminated_.push_back(probe_id_); }

    private:
        FakeProbeExecutor& owner_;
        std::string probe_id_;
        Behavior behavior_;
        std::chrono::steady_clock::time_point ready_at_;
    };

    std::map<std::string, Behavior> behaviors_;
    std::vector<std::string> launched_;
    std::vector<std::string> terminated_;
    std::size_t launch_count_at_first_wait_ = 0;
};

} // namespace catro::tests

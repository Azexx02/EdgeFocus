#pragma once

#include "session/SessionRuntime.h"
#include "session/SessionSnapshotPublisher.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace edgefocus::session {

class SessionOrchestrator {
public:
    using RuntimeFactory = std::function<std::unique_ptr<SessionRuntime>(const SessionContext&)>;
    using Clock = std::function<std::int64_t()>;
    using IdFactory = std::function<std::string(std::int64_t wall_ms)>;
    using BeforeStartHook = std::function<bool(std::string* error)>;
    using AfterStopHook = std::function<void()>;
    using RuntimeChangedHook = std::function<void()>;

    struct StartRequest {
        std::int64_t target_duration_ms{};
        SourceMode source_mode{SourceMode::LiveCamera};
    };

    struct Result {
        bool success{};
        std::string error;
        std::optional<SessionContext> context;
    };

    SessionOrchestrator(
        SessionSnapshotPublisher& snapshots,
        RuntimeFactory runtime_factory,
        Clock monotonic_clock,
        Clock wall_clock,
        IdFactory id_factory = {});
    ~SessionOrchestrator();

    Result start(const StartRequest& request);
    Result confirmReady();
    Result pause();
    Result resume();
    Result end(StopReason reason = StopReason::Completed);
    void setBeforeStartHook(BeforeStartHook hook);
    void setAfterStopHook(AfterStopHook hook);
    void setRuntimeChangedHook(RuntimeChangedHook hook);

    Phase phase() const;
    bool active() const;

private:
    Result transitionLocked(Phase expected, Phase next,
                            std::optional<std::int64_t> transition_ms = std::nullopt);
    Result currentResultLocked() const;

    SessionSnapshotPublisher& snapshots_;
    RuntimeFactory runtime_factory_;
    Clock monotonic_clock_;
    Clock wall_clock_;
    IdFactory id_factory_;
    BeforeStartHook before_start_hook_;
    AfterStopHook after_stop_hook_;
    RuntimeChangedHook runtime_changed_hook_;

    mutable std::mutex mutex_;
    std::unique_ptr<SessionRuntime> runtime_;
    Phase phase_{Phase::Idle};
};

}  // namespace edgefocus::session

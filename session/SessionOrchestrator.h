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

    Phase phase() const;
    bool active() const;

private:
    Result transitionLocked(Phase expected, Phase next);
    Result currentResultLocked() const;

    SessionSnapshotPublisher& snapshots_;
    RuntimeFactory runtime_factory_;
    Clock monotonic_clock_;
    Clock wall_clock_;
    IdFactory id_factory_;

    mutable std::mutex mutex_;
    std::unique_ptr<SessionRuntime> runtime_;
    Phase phase_{Phase::Idle};
};

}  // namespace edgefocus::session

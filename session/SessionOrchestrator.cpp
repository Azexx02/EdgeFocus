#include "session/SessionOrchestrator.h"

#include <stdexcept>
#include <utility>

namespace edgefocus::session {

SessionOrchestrator::SessionOrchestrator(
    SessionSnapshotPublisher& snapshots,
    RuntimeFactory runtime_factory,
    Clock monotonic_clock,
    Clock wall_clock,
    IdFactory id_factory)
    : snapshots_(snapshots),
      runtime_factory_(std::move(runtime_factory)),
      monotonic_clock_(std::move(monotonic_clock)),
      wall_clock_(std::move(wall_clock)),
      id_factory_(std::move(id_factory)) {
    if (!runtime_factory_ || !monotonic_clock_ || !wall_clock_) {
        throw std::invalid_argument("session orchestrator dependencies are required");
    }
    if (!id_factory_) {
        id_factory_ = [](std::int64_t wall_ms) {
            return std::string("session_") + std::to_string(wall_ms);
        };
    }
}

SessionOrchestrator::~SessionOrchestrator() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (runtime_) {
        runtime_->stop(StopReason::ProcessShutdown);
        if (after_stop_hook_) after_stop_hook_();
    }
}

SessionOrchestrator::Result SessionOrchestrator::start(const StartRequest& request) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (phase_ != Phase::Idle || runtime_) {
        Result result = currentResultLocked();
        result.success = true;
        return result;
    }
    if (request.target_duration_ms <= 0) {
        return {false, "target_duration_ms must be positive", std::nullopt};
    }
    if (request.source_mode != SourceMode::LiveCamera) {
        return {false, "formal sessions only support live_camera", std::nullopt};
    }
    if (before_start_hook_) {
        std::string error;
        if (!before_start_hook_(&error)) {
            return {false, error.empty() ? "failed to acquire formal runtime" : error, std::nullopt};
        }
    }

    const auto monotonic_ms = monotonic_clock_();
    const auto wall_ms = wall_clock_();
    SessionContext context;
    context.session_id = id_factory_(wall_ms);
    context.source_mode = request.source_mode;
    context.started_monotonic_ms = monotonic_ms;
    context.started_wall_ms = wall_ms;
    context.target_duration_ms = request.target_duration_ms;

    auto runtime = runtime_factory_(context);
    if (!runtime) {
        if (after_stop_hook_) after_stop_hook_();
        return {false, "runtime factory returned null", std::nullopt};
    }

    std::string error;
    if (!runtime->prepare(&error)) {
        if (after_stop_hook_) after_stop_hook_();
        return {false, std::move(error), std::nullopt};
    }

    // Publish the session identity before features enter PRECHECK so their
    // synchronous evidence/status updates cannot be wiped by a later begin().
    snapshots_.begin(context, monotonic_ms);
    if (runtime_changed_hook_) runtime_changed_hook_();
    try {
        runtime->enterPhase(Phase::Precheck);
    } catch (const std::exception& exception) {
        runtime->stop(StopReason::PrepareFailed);
        snapshots_.end(monotonic_clock_());
        if (runtime_changed_hook_) runtime_changed_hook_();
        if (after_stop_hook_) after_stop_hook_();
        return {false, exception.what(), std::nullopt};
    } catch (...) {
        runtime->stop(StopReason::PrepareFailed);
        snapshots_.end(monotonic_clock_());
        if (runtime_changed_hook_) runtime_changed_hook_();
        if (after_stop_hook_) after_stop_hook_();
        return {false, "unknown precheck transition exception", std::nullopt};
    }

    runtime_ = std::move(runtime);
    phase_ = Phase::Precheck;
    snapshots_.setDegradedFeatures(runtime_->degradedFeatures(), monotonic_ms);
    return {true, {}, context};
}

SessionOrchestrator::Result SessionOrchestrator::transitionLocked(
    Phase expected, Phase next, std::optional<std::int64_t> transition_ms) {
    if (!runtime_ || phase_ != expected) {
        return {false,
                std::string("invalid transition from ") + phase_name(phase_) + " to " +
                    phase_name(next),
                std::nullopt};
    }
    try {
        runtime_->enterPhase(next);
    } catch (const std::exception& exception) {
        const SessionContext context = runtime_->context();
        phase_ = Phase::Ending;
        snapshots_.setPhase(Phase::Ending, monotonic_clock_());
        runtime_->stop(StopReason::Interrupted);
        runtime_.reset();
        phase_ = Phase::Idle;
        snapshots_.end(monotonic_clock_());
        if (after_stop_hook_) after_stop_hook_();
        return {false, exception.what(), context};
    } catch (...) {
        const SessionContext context = runtime_->context();
        phase_ = Phase::Ending;
        snapshots_.setPhase(Phase::Ending, monotonic_clock_());
        runtime_->stop(StopReason::Interrupted);
        runtime_.reset();
        phase_ = Phase::Idle;
        snapshots_.end(monotonic_clock_());
        if (after_stop_hook_) after_stop_hook_();
        return {false, "unknown phase transition exception", context};
    }
    phase_ = next;
    snapshots_.setPhase(next, transition_ms.value_or(monotonic_clock_()),
                        expected == Phase::Precheck && next == Phase::Focusing &&
                            runtime_->formalStarted());
    if (runtime_changed_hook_) runtime_changed_hook_();
    snapshots_.setDegradedFeatures(runtime_->degradedFeatures(), monotonic_clock_());
    return currentResultLocked();
}

SessionOrchestrator::Result SessionOrchestrator::confirmReady() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!runtime_ || phase_ != Phase::Precheck) {
        return {false,
                std::string("invalid transition from ") + phase_name(phase_) +
                    " to FOCUSING",
                std::nullopt};
    }
    const FormalStartContext formal_start{monotonic_clock_(), wall_clock_()};
    std::string error;
    if (!runtime_->beginFormalSession(formal_start, &error)) {
        const SessionContext context = runtime_->context();
        runtime_->stop(StopReason::PrepareFailed);
        runtime_.reset();
        phase_ = Phase::Idle;
        snapshots_.end(monotonic_clock_());
        if (runtime_changed_hook_) runtime_changed_hook_();
        if (after_stop_hook_) after_stop_hook_();
        return {false, std::move(error), context};
    }
    return transitionLocked(
        Phase::Precheck, Phase::Focusing, formal_start.started_monotonic_ms);
}

SessionOrchestrator::Result SessionOrchestrator::pause() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (phase_ == Phase::Paused) return currentResultLocked();
    return transitionLocked(Phase::Focusing, Phase::Paused);
}

SessionOrchestrator::Result SessionOrchestrator::resume() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (phase_ == Phase::Focusing) return currentResultLocked();
    return transitionLocked(Phase::Paused, Phase::Focusing);
}

SessionOrchestrator::Result SessionOrchestrator::end(StopReason reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!runtime_ || phase_ == Phase::Idle) {
        return {false, "no active session", std::nullopt};
    }

    const SessionContext context = runtime_->context();
    phase_ = Phase::Ending;
    snapshots_.setPhase(Phase::Ending, monotonic_clock_());
    try {
        runtime_->enterPhase(Phase::Ending);
    } catch (...) {
        // stop() still runs and is noexcept.
    }
    runtime_->stop(reason);
    runtime_.reset();
    phase_ = Phase::Idle;
    snapshots_.end(monotonic_clock_());
    if (runtime_changed_hook_) runtime_changed_hook_();
    if (after_stop_hook_) after_stop_hook_();
    return {true, {}, context};
}

SessionOrchestrator::Result SessionOrchestrator::currentResultLocked() const {
    if (!runtime_) return {phase_ == Phase::Idle, {}, std::nullopt};
    return {true, {}, runtime_->context()};
}

Phase SessionOrchestrator::phase() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return phase_;
}

bool SessionOrchestrator::active() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return runtime_ != nullptr;
}

void SessionOrchestrator::setBeforeStartHook(BeforeStartHook hook) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (runtime_) throw std::logic_error("cannot replace before-start hook during a session");
    before_start_hook_ = std::move(hook);
}

void SessionOrchestrator::setAfterStopHook(AfterStopHook hook) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (runtime_) throw std::logic_error("cannot replace after-stop hook during a session");
    after_stop_hook_ = std::move(hook);
}

void SessionOrchestrator::setRuntimeChangedHook(RuntimeChangedHook hook) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (runtime_) throw std::logic_error("cannot replace runtime-change hook during a session");
    runtime_changed_hook_ = std::move(hook);
}

}  // namespace edgefocus::session

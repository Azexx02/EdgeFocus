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
    end(StopReason::ProcessShutdown);
    std::thread completed_worker;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (start_worker_.joinable()) completed_worker = std::move(start_worker_);
    }
    if (completed_worker.joinable()) completed_worker.join();
}

SessionOrchestrator::Result SessionOrchestrator::start(const StartRequest& request) {
    std::thread completed_worker;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (lifecycle_ == Lifecycle::Idle && start_worker_.joinable()) {
            completed_worker = std::move(start_worker_);
        }
    }
    if (completed_worker.joinable()) completed_worker.join();

    if (request.target_duration_ms <= 0) {
        return {false, "target_duration_ms must be positive", std::nullopt};
    }
    if (request.source_mode != SourceMode::LiveCamera) {
        return {false, "formal sessions only support live_camera", std::nullopt};
    }
    SessionContext context;
    RuntimeChangedHook runtime_changed;
    std::uint64_t generation{};
    std::string worker_error;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutdown_requested_) {
            return {false, "session orchestrator is shutting down", std::nullopt};
        }
        if (lifecycle_ != Lifecycle::Idle || phase_ != Phase::Idle || runtime_) {
            Result result = currentResultLocked();
            result.success = lifecycle_ != Lifecycle::Cleaning;
            result.already_active = result.success;
            if (!result.success) result.error = "session cleanup in progress";
            return result;
        }

        const auto monotonic_ms = monotonic_clock_();
        const auto wall_ms = wall_clock_();
        context.session_id = id_factory_(wall_ms);
        context.source_mode = request.source_mode;
        context.started_monotonic_ms = monotonic_ms;
        context.started_wall_ms = wall_ms;
        context.target_duration_ms = request.target_duration_ms;

        generation = ++start_generation_;
        pending_context_ = context;
        cancel_start_ = false;
        lifecycle_ = Lifecycle::Starting;
        phase_ = Phase::Precheck;
        snapshots_.begin(context, monotonic_ms, generation, request.operation_id);
        runtime_changed = runtime_changed_hook_;
        try {
            start_worker_ = std::thread(
                [this, context, generation] { runStartWorker(context, generation); });
        } catch (const std::exception& exception) {
            worker_error = std::string("failed to create session preparation worker: ") +
                           exception.what();
            lifecycle_ = Lifecycle::Cleaning;
            cancel_start_ = true;
        } catch (...) {
            worker_error = "failed to create session preparation worker";
            lifecycle_ = Lifecycle::Cleaning;
            cancel_start_ = true;
        }
    }
    if (!worker_error.empty()) {
        snapshots_.failPreparation(worker_error, monotonic_clock_(), wall_clock_());
        if (runtime_changed) {
            try { runtime_changed(); } catch (...) {}
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (generation == start_generation_ && lifecycle_ == Lifecycle::Cleaning) {
                pending_context_.reset();
                cancel_start_ = false;
                phase_ = Phase::Idle;
                lifecycle_ = Lifecycle::Idle;
            }
        }
        lifecycle_changed_.notify_all();
        return {false, worker_error, context};
    }
    if (runtime_changed) {
        try { runtime_changed(); } catch (...) {}
    }
    return {true, {}, context};
}

bool SessionOrchestrator::startCancelled(std::uint64_t generation) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return generation != start_generation_ || cancel_start_ ||
           lifecycle_ == Lifecycle::Cleaning;
}

void SessionOrchestrator::runStartWorker(
    SessionContext context, std::uint64_t generation) {
    bool resources_acquired = false;
    std::unique_ptr<SessionRuntime> runtime;
    std::string error;
    try {
        if (before_start_hook_) {
            if (!before_start_hook_(&error)) {
                if (error.empty()) error = "failed to acquire formal runtime";
            } else {
                resources_acquired = true;
            }
        } else {
            resources_acquired = true;
        }

        if (error.empty() && !startCancelled(generation)) {
            runtime = runtime_factory_(context);
            if (!runtime) error = "runtime factory returned null";
        }
        if (error.empty() && !startCancelled(generation) && !runtime->prepare(&error)) {
            if (error.empty()) error = "runtime prepare failed";
        }
        if (error.empty() && !startCancelled(generation)) {
            runtime->enterPhase(Phase::Precheck);
        }
    } catch (const std::exception& exception) {
        error = exception.what();
    } catch (...) {
        error = "unknown session preparation exception";
    }

    const bool cancelled = startCancelled(generation);
    if (error.empty() && !cancelled) {
        const auto degraded = runtime->degradedFeatures();
        bool accepted = false;
        RuntimeChangedHook runtime_changed;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (generation == start_generation_ && lifecycle_ == Lifecycle::Starting &&
                !cancel_start_) {
                snapshots_.setDegradedFeatures(degraded, monotonic_clock_());
                snapshots_.markPreparationComplete(!degraded.empty(), monotonic_clock_());
                runtime_ = std::move(runtime);
                pending_context_.reset();
                lifecycle_ = Lifecycle::Active;
                phase_ = Phase::Precheck;
                runtime_changed = runtime_changed_hook_;
                accepted = true;
            }
        }
        if (accepted) {
            if (runtime_changed) {
                try { runtime_changed(); } catch (...) {}
            }
            return;
        }
    }

    if (runtime) {
        runtime->stop(cancelled ? StopReason::Cancelled : StopReason::PrepareFailed);
    }
    if (resources_acquired && after_stop_hook_) {
        try {
            after_stop_hook_();
        } catch (...) {
            // Cleanup hooks must not terminate the asynchronous preparation worker.
        }
    }

    bool publish_terminal = false;
    RuntimeChangedHook runtime_changed;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation == start_generation_ &&
            (lifecycle_ == Lifecycle::Starting || lifecycle_ == Lifecycle::Cleaning)) {
            lifecycle_ = Lifecycle::Cleaning;
            runtime_changed = runtime_changed_hook_;
            publish_terminal = true;
        }
    }
    if (publish_terminal) {
        if (cancelled) {
            snapshots_.end(monotonic_clock_());
        } else {
            snapshots_.failPreparation(
                error.empty() ? "session preparation failed" : std::move(error),
                monotonic_clock_(), wall_clock_());
        }
        if (runtime_changed) {
            try { runtime_changed(); } catch (...) {}
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation == start_generation_ && lifecycle_ == Lifecycle::Cleaning) {
            pending_context_.reset();
            phase_ = Phase::Idle;
            cancel_start_ = false;
            lifecycle_ = Lifecycle::Idle;
        }
    }
    lifecycle_changed_.notify_all();
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
        return {false, exception.what(), context};
    } catch (...) {
        const SessionContext context = runtime_->context();
        return {false, "unknown phase transition exception", context};
    }
    phase_ = next;
    snapshots_.setPhase(next, transition_ms.value_or(monotonic_clock_()),
                        expected == Phase::Precheck && next == Phase::Focusing &&
                            runtime_->formalStarted());
    snapshots_.setDegradedFeatures(runtime_->degradedFeatures(), monotonic_clock_());
    return currentResultLocked();
}

SessionOrchestrator::Result SessionOrchestrator::confirmReady() {
    Result result;
    RuntimeChangedHook runtime_changed;
    bool cleanup_required = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (lifecycle_ == Lifecycle::Starting && phase_ == Phase::Precheck) {
            return {false, "precheck_not_ready", pending_context_};
        }
        if (lifecycle_ != Lifecycle::Active || !runtime_ || phase_ != Phase::Precheck) {
            return {false,
                    std::string("invalid transition from ") + phase_name(phase_) +
                        " to FOCUSING",
                    std::nullopt};
        }
        const FormalStartContext formal_start{monotonic_clock_(), wall_clock_()};
        std::string error;
        if (!runtime_->beginFormalSession(formal_start, &error)) {
            result = {false, std::move(error), runtime_->context()};
            cleanup_required = true;
        } else {
            result = transitionLocked(
                Phase::Precheck, Phase::Focusing, formal_start.started_monotonic_ms);
            cleanup_required = !result.success && result.context.has_value();
            if (result.success) runtime_changed = runtime_changed_hook_;
        }
    }
    if (cleanup_required) {
        const auto error = result.error;
        const auto context = result.context;
        end(StopReason::PrepareFailed);
        return {false, error, context};
    }
    if (runtime_changed) {
        try { runtime_changed(); } catch (...) {}
    }
    return result;
}

SessionOrchestrator::Result SessionOrchestrator::pause() {
    Result result;
    RuntimeChangedHook runtime_changed;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (phase_ == Phase::Paused) return currentResultLocked();
        result = transitionLocked(Phase::Focusing, Phase::Paused);
        if (result.success) runtime_changed = runtime_changed_hook_;
    }
    if (!result.success && result.context) {
        const auto error = result.error;
        const auto context = result.context;
        end(StopReason::Interrupted);
        return {false, error, context};
    }
    if (runtime_changed) {
        try { runtime_changed(); } catch (...) {}
    }
    return result;
}

SessionOrchestrator::Result SessionOrchestrator::resume() {
    Result result;
    RuntimeChangedHook runtime_changed;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (phase_ == Phase::Focusing) return currentResultLocked();
        result = transitionLocked(Phase::Paused, Phase::Focusing);
        if (result.success) runtime_changed = runtime_changed_hook_;
    }
    if (!result.success && result.context) {
        const auto error = result.error;
        const auto context = result.context;
        end(StopReason::Interrupted);
        return {false, error, context};
    }
    if (runtime_changed) {
        try { runtime_changed(); } catch (...) {}
    }
    return result;
}

SessionOrchestrator::Result SessionOrchestrator::end(StopReason reason) {
    std::thread worker;
    std::unique_ptr<SessionRuntime> runtime;
    std::optional<SessionContext> context;
    AfterStopHook after_stop;
    RuntimeChangedHook runtime_changed;
    bool starting = false;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (reason == StopReason::ProcessShutdown) {
            shutdown_requested_ = true;
        }
        if (lifecycle_ == Lifecycle::Idle || phase_ == Phase::Idle) {
            return {false, "no active session", std::nullopt};
        }
        if (lifecycle_ == Lifecycle::Cleaning) {
            context = currentResultLocked().context;
            lifecycle_changed_.wait(lock, [this] { return lifecycle_ == Lifecycle::Idle; });
            return {true, {}, context};
        }

        context = runtime_ ? std::optional<SessionContext>(runtime_->context()) : pending_context_;
        starting = lifecycle_ == Lifecycle::Starting;
        lifecycle_ = Lifecycle::Cleaning;
        cancel_start_ = true;
        if (start_worker_.joinable()) worker = std::move(start_worker_);
        if (!starting) {
            runtime = std::move(runtime_);
            after_stop = after_stop_hook_;
            runtime_changed = runtime_changed_hook_;
        }
    }

    if (worker.joinable()) worker.join();
    if (starting) {
        return {true, {}, context};
    }

    if (runtime) {
        try {
            runtime->enterPhase(Phase::Ending);
        } catch (...) {
            // stop() still runs and is noexcept.
        }
        runtime->stop(reason);
    }
    if (after_stop) {
        try { after_stop(); } catch (...) {}
    }
    snapshots_.end(monotonic_clock_());
    if (runtime_changed) {
        try { runtime_changed(); } catch (...) {}
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_context_.reset();
        cancel_start_ = false;
        phase_ = Phase::Idle;
        lifecycle_ = Lifecycle::Idle;
    }
    lifecycle_changed_.notify_all();
    return {true, {}, context};
}

SessionOrchestrator::Result SessionOrchestrator::currentResultLocked() const {
    if (!runtime_) {
        if (pending_context_) return {true, {}, pending_context_};
        return {phase_ == Phase::Idle, {}, std::nullopt};
    }
    return {true, {}, runtime_->context()};
}

Phase SessionOrchestrator::phase() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return phase_;
}

bool SessionOrchestrator::active() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lifecycle_ != Lifecycle::Idle;
}

void SessionOrchestrator::setBeforeStartHook(BeforeStartHook hook) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (lifecycle_ != Lifecycle::Idle) throw std::logic_error("cannot replace before-start hook during a session");
    before_start_hook_ = std::move(hook);
}

void SessionOrchestrator::setAfterStopHook(AfterStopHook hook) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (lifecycle_ != Lifecycle::Idle) throw std::logic_error("cannot replace after-stop hook during a session");
    after_stop_hook_ = std::move(hook);
}

void SessionOrchestrator::setRuntimeChangedHook(RuntimeChangedHook hook) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (lifecycle_ != Lifecycle::Idle) throw std::logic_error("cannot replace runtime-change hook during a session");
    runtime_changed_hook_ = std::move(hook);
}

}  // namespace edgefocus::session

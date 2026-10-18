#include "session/SessionOrchestrator.h"

#include <cassert>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

void require_always(bool condition, const char* expression, int line) {
    if (condition) return;
    std::cerr << "FAIL line " << line << ": " << expression << '\n';
    std::exit(1);
}

#undef assert
#define assert(expression) require_always((expression), #expression, __LINE__)

using namespace edgefocus::session;

template <typename Predicate>
bool wait_until(Predicate predicate, int timeout_ms = 2000) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

struct TraceFeature final : ISessionFeature {
    TraceFeature(std::string value, std::vector<std::string>& trace, PrepareResult prepare_result,
                 PrepareResult formal_result = PrepareResult::success())
        : value(std::move(value)), trace(trace), prepare_result(std::move(prepare_result)),
          formal_result(std::move(formal_result)) {}

    const char* name() const noexcept override { return value.c_str(); }
    PrepareResult prepare(const SessionContext& context) override {
        session_seen = context.session_id;
        trace.push_back(value + ":prepare");
        return prepare_result;
    }
    PrepareResult beginFormalSession(const FormalStartContext& context) override {
        formal_start_seen = context.started_monotonic_ms;
        trace.push_back(value + ":formal");
        return formal_result;
    }
    void enterPhase(Phase phase) override {
        trace.push_back(value + ":" + phase_name(phase));
    }
    void stop(StopReason) noexcept override { trace.push_back(value + ":stop"); }
    FeatureHealth health() const override { return {true, false, "ready", {}, 0}; }

    std::string value;
    std::vector<std::string>& trace;
    PrepareResult prepare_result;
    PrepareResult formal_result;
    std::string session_seen;
    std::int64_t formal_start_seen{};
};

struct BlockingFeature final : ISessionFeature {
    BlockingFeature(std::atomic<bool>& entered, std::atomic<bool>& released)
        : entered(entered), released(released) {}

    const char* name() const noexcept override { return "blocking"; }
    PrepareResult prepare(const SessionContext&) override {
        entered.store(true);
        while (!released.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return PrepareResult::success();
    }
    void enterPhase(Phase) override {}
    void stop(StopReason) noexcept override {}
    FeatureHealth health() const override { return {true, false, "ready", {}, 0}; }

    std::atomic<bool>& entered;
    std::atomic<bool>& released;
};

void test_runtime_order_and_optional_degradation() {
    std::vector<std::string> trace;
    SessionContext context{"s1", SourceMode::LiveCamera, 1, 2, 1000};
    SessionRuntime runtime(context);
    runtime.addFeature(std::make_unique<TraceFeature>("camera", trace, PrepareResult::success()), true);
    runtime.addFeature(
        std::make_unique<TraceFeature>("gimbal", trace, PrepareResult::failure("offline")), false);
    assert(runtime.prepare());
    runtime.enterPhase(Phase::Precheck);
    assert(runtime.beginFormalSession({10, 20}));
    runtime.enterPhase(Phase::Focusing);
    runtime.stop(StopReason::Completed);
    const std::vector<std::string> expected{
        "camera:prepare", "gimbal:prepare", "camera:PRECHECK", "camera:formal",
        "camera:FOCUSING", "camera:stop"};
    assert(trace == expected);
    assert(runtime.degradedFeatures() == std::vector<std::string>{"gimbal"});
}

void test_required_formal_start_failure_rolls_back_without_focusing() {
    std::vector<std::string> trace;
    SessionRuntime runtime({"s3", SourceMode::LiveCamera, 1, 2, 1000});
    runtime.addFeature(std::make_unique<TraceFeature>(
                           "fusion", trace, PrepareResult::success(), PrepareResult::success()),
                       true);
    runtime.addFeature(std::make_unique<TraceFeature>(
                           "sqlite", trace, PrepareResult::success(),
                           PrepareResult::failure("write failed")),
                       true);
    assert(runtime.prepare());
    runtime.enterPhase(Phase::Precheck);
    std::string error;
    assert(!runtime.beginFormalSession({50, 500}, &error));
    assert(error == "sqlite: write failed");
    assert(!runtime.formalStarted());
    runtime.stop(StopReason::PrepareFailed);
    const std::vector<std::string> expected{
        "fusion:prepare", "sqlite:prepare", "fusion:PRECHECK", "sqlite:PRECHECK",
        "fusion:formal", "sqlite:formal", "sqlite:stop", "fusion:stop"};
    assert(trace == expected);
}

void test_required_prepare_failure_rolls_back() {
    std::vector<std::string> trace;
    SessionRuntime runtime({"s2", SourceMode::LiveCamera, 1, 2, 1000});
    runtime.addFeature(std::make_unique<TraceFeature>("camera", trace, PrepareResult::success()), true);
    runtime.addFeature(
        std::make_unique<TraceFeature>("npu", trace, PrepareResult::failure("missing model")), true);
    std::string error;
    assert(!runtime.prepare(&error));
    assert(error == "npu: missing model");
    const std::vector<std::string> expected{"camera:prepare", "npu:prepare", "camera:stop"};
    assert(trace == expected);
}

void test_orchestrator_lifecycle_and_snapshot_isolation() {
    std::int64_t monotonic = 100;
    std::int64_t wall = 1000;
    std::vector<std::string> trace;
    SessionSnapshotPublisher snapshots("boot-test");

    auto factory = [&](const SessionContext& context) {
        auto runtime = std::make_unique<SessionRuntime>(context);
        runtime->addFeature(
            std::make_unique<TraceFeature>("camera-" + context.session_id, trace, PrepareResult::success()),
            true);
        return runtime;
    };
    SessionOrchestrator orchestrator(
        snapshots,
        factory,
        [&] { return monotonic; },
        [&] { return wall; },
        [](std::int64_t value) { return "session-" + std::to_string(value); });

    auto first = orchestrator.start({60000, SourceMode::LiveCamera});
    assert(first.success && first.context->session_id == "session-1000");
    assert(orchestrator.phase() == Phase::Precheck);
    assert(snapshots.read().runtime_active);
    assert(!snapshots.read().formal_active);
    assert(snapshots.read().elapsed_ms == 0);
    assert(wait_until([&] {
        const auto state = snapshots.read().preparation.state;
        return state == StartPreparationState::Ready ||
               state == StartPreparationState::Degraded;
    }));
    monotonic = 150;
    assert(orchestrator.confirmReady().success);
    assert(snapshots.read().formal_active);
    snapshots.updateVisual({}, 200);
    assert(snapshots.read().elapsed_ms == 50);
    monotonic = 250;
    assert(orchestrator.pause().success);
    snapshots.updateVisual({}, 1000);
    assert(snapshots.read().elapsed_ms == 100);
    assert(orchestrator.pause().success);  // idempotent
    monotonic = 1100;
    assert(orchestrator.resume().success);
    snapshots.updateVisual({}, 1150);
    assert(snapshots.read().elapsed_ms == 150);
    assert(orchestrator.end().success);
    assert(!orchestrator.active());
    auto idle = snapshots.read();
    assert(idle.phase == Phase::Idle && !idle.runtime_active && !idle.formal_active);
    assert(idle.context.session_id.empty());
    assert(idle.elapsed_ms == 0 && idle.remaining_ms == 0);

    monotonic = 200;
    wall = 2000;
    auto recorded = orchestrator.start({30000, SourceMode::RecordedInput});
    assert(!recorded.success);
    bool before_start_called = false;
    int after_stop_calls = 0;
    orchestrator.setBeforeStartHook([&](std::string*) {
        before_start_called = true;
        return true;
    });
    orchestrator.setAfterStopHook([&] { ++after_stop_calls; });
    auto second = orchestrator.start({30000, SourceMode::LiveCamera});
    assert(second.success && second.context->session_id == "session-2000");
    assert(wait_until([&] {
        return snapshots.read().preparation.state == StartPreparationState::Ready;
    }));
    assert(before_start_called);
    auto active = snapshots.read();
    assert(active.context.session_id == "session-2000");
    assert(active.context.source_mode == SourceMode::LiveCamera);
    assert(active.visual.frame_id == 0);
    assert(active.assessment.level == std::nullopt);
    assert(orchestrator.end(StopReason::Cancelled).success);
    assert(after_stop_calls == 1);
}

void test_orchestrator_fast_ack_duplicate_and_initializing_gate() {
    std::atomic<bool> entered{false};
    std::atomic<bool> released{false};
    SessionSnapshotPublisher snapshots("boot-fast-ack");
    auto factory = [&](const SessionContext& context) {
        auto runtime = std::make_unique<SessionRuntime>(context);
        runtime->addFeature(std::make_unique<BlockingFeature>(entered, released), true);
        return runtime;
    };
    SessionOrchestrator orchestrator(
        snapshots, factory, [] { return 100; }, [] { return 200; },
        [](std::int64_t) { return "session-fast"; });

    const auto started = orchestrator.start(
        {60000, SourceMode::LiveCamera, "android-operation-1"});
    assert(started.success);
    assert(wait_until([&] { return entered.load(); }));
    const auto initializing = snapshots.read();
    assert(initializing.phase == Phase::Precheck);
    assert(initializing.preparation.state == StartPreparationState::Initializing);
    assert(initializing.preparation.operation_id == "android-operation-1");
    assert(initializing.preparation.generation == 1);

    const auto duplicate = orchestrator.start(
        {60000, SourceMode::LiveCamera, "android-operation-2"});
    assert(duplicate.success);
    assert(duplicate.context->session_id == started.context->session_id);
    const auto early_confirm = orchestrator.confirmReady();
    assert(!early_confirm.success && early_confirm.error == "precheck_not_ready");

    released.store(true);
    assert(wait_until([&] {
        return snapshots.read().preparation.state == StartPreparationState::Ready;
    }));
    assert(orchestrator.confirmReady().success);
    assert(orchestrator.end().success);
}

void test_precheck_waits_for_an_explicit_transition_without_session_timeout() {
    std::int64_t monotonic = 100;
    SessionSnapshotPublisher snapshots("boot-precheck-no-timeout");
    std::vector<std::string> trace;
    SessionOrchestrator orchestrator(
        snapshots,
        [&](const SessionContext& context) {
            auto runtime = std::make_unique<SessionRuntime>(context);
            runtime->addFeature(std::make_unique<TraceFeature>(
                "camera", trace, PrepareResult::success()), true);
            return runtime;
        },
        [&] { return monotonic; }, [] { return 200; },
        [](std::int64_t) { return "session-precheck-no-timeout"; });

    const auto started = orchestrator.start({60000, SourceMode::LiveCamera});
    assert(started.success);
    assert(wait_until([&] {
        return snapshots.read().preparation.state == StartPreparationState::Ready;
    }));

    monotonic += 24LL * 60LL * 60LL * 1000LL;
    assert(orchestrator.active());
    assert(orchestrator.phase() == Phase::Precheck);
    const auto still_prechecking = snapshots.read();
    assert(still_prechecking.runtime_active);
    assert(!still_prechecking.formal_active);
    assert(still_prechecking.phase == Phase::Precheck);

    const auto duplicate = orchestrator.start({60000, SourceMode::LiveCamera});
    assert(duplicate.success && duplicate.already_active);
    assert(duplicate.context->session_id == started.context->session_id);
    assert(orchestrator.end(StopReason::Cancelled).success);
    assert(orchestrator.phase() == Phase::Idle);
}

void test_prepare_failure_is_retained_and_correlated() {
    SessionSnapshotPublisher snapshots("boot-failed-start");
    auto factory = [&](const SessionContext& context) {
        auto runtime = std::make_unique<SessionRuntime>(context);
        static std::vector<std::string> trace;
        runtime->addFeature(std::make_unique<TraceFeature>(
            "camera", trace, PrepareResult::failure("open failed")), true);
        return runtime;
    };
    SessionOrchestrator orchestrator(
        snapshots, factory, [] { return 500; }, [] { return 600; },
        [](std::int64_t) { return "session-failed"; });

    assert(orchestrator.start(
        {60000, SourceMode::LiveCamera, "android-operation-failed"}).success);
    assert(wait_until([&] {
        return snapshots.read().preparation.state == StartPreparationState::Failed;
    }));
    const auto failed = snapshots.read();
    assert(failed.phase == Phase::Idle && !failed.runtime_active);
    assert(failed.context.session_id.empty());
    assert(failed.preparation.operation_id == "android-operation-failed");
    assert(failed.preparation.error == "camera: open failed");
    assert(!orchestrator.active());
}

void test_factory_exception_becomes_correlated_failure() {
    SessionSnapshotPublisher snapshots("boot-factory-throw");
    SessionOrchestrator orchestrator(
        snapshots,
        [](const SessionContext&) -> std::unique_ptr<SessionRuntime> {
            throw std::runtime_error("camera runtime unavailable");
        },
        [] { return 700; }, [] { return 800; },
        [](std::int64_t) { return "session-factory-throw"; });

    assert(orchestrator.start(
        {60000, SourceMode::LiveCamera, "android-operation-throw"}).success);
    assert(wait_until([&] {
        return snapshots.read().preparation.state == StartPreparationState::Failed;
    }));
    const auto failed = snapshots.read();
    assert(failed.phase == Phase::Idle);
    assert(failed.preparation.operation_id == "android-operation-throw");
    assert(failed.preparation.error == "camera runtime unavailable");
    assert(!orchestrator.active());
}

void test_cancel_during_preparation_cleans_once() {
    std::atomic<bool> entered{false};
    std::atomic<bool> released{false};
    std::atomic<int> after_stop_calls{0};
    SessionSnapshotPublisher snapshots("boot-cancel-start");
    SessionOrchestrator orchestrator(
        snapshots,
        [&](const SessionContext& context) {
            auto runtime = std::make_unique<SessionRuntime>(context);
            runtime->addFeature(std::make_unique<BlockingFeature>(entered, released), true);
            return runtime;
        },
        [] { return 900; }, [] { return 1000; },
        [](std::int64_t) { return "session-cancel-start"; });
    orchestrator.setAfterStopHook([&] { ++after_stop_calls; });

    assert(orchestrator.start(
        {60000, SourceMode::LiveCamera, "android-operation-cancel"}).success);
    assert(wait_until([&] { return entered.load(); }));

    SessionOrchestrator::Result end_result;
    std::thread ending([&] { end_result = orchestrator.end(StopReason::Cancelled); });
    assert(wait_until([&] {
        const auto probe = orchestrator.start(
            {60000, SourceMode::LiveCamera, "must-not-start-while-cleaning"});
        return !probe.success && probe.error == "session cleanup in progress";
    }));

    std::atomic<bool> shutdown_waiter_entered{false};
    std::atomic<bool> shutdown_waiter_returned{false};
    SessionOrchestrator::Result shutdown_result;
    std::thread shutdown_waiter([&] {
        shutdown_waiter_entered.store(true);
        shutdown_result = orchestrator.end(StopReason::Interrupted);
        shutdown_waiter_returned.store(true);
    });
    assert(wait_until([&] { return shutdown_waiter_entered.load(); }));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(!shutdown_waiter_returned.load());

    released.store(true);
    ending.join();
    shutdown_waiter.join();

    assert(end_result.success);
    assert(shutdown_result.success);
    assert(!orchestrator.active());
    assert(snapshots.read().phase == Phase::Idle);
    assert(after_stop_calls.load() == 1);

    const auto restarted = orchestrator.start(
        {60000, SourceMode::LiveCamera, "android-operation-after-cancel"});
    assert(restarted.success && !restarted.already_active);
    assert(wait_until([&] {
        return snapshots.read().preparation.state == StartPreparationState::Ready;
    }));
    assert(snapshots.read().preparation.operation_id == "android-operation-after-cancel");
    assert(orchestrator.end(StopReason::Cancelled).success);

    const auto shutdown_idle = orchestrator.end(StopReason::ProcessShutdown);
    assert(!shutdown_idle.success && shutdown_idle.error == "no active session");
    const auto rejected_after_shutdown = orchestrator.start(
        {60000, SourceMode::LiveCamera, "must-not-start-after-shutdown"});
    assert(!rejected_after_shutdown.success);
    assert(rejected_after_shutdown.error == "session orchestrator is shutting down");
}

}  // namespace

int main() {
    test_runtime_order_and_optional_degradation();
    test_required_prepare_failure_rolls_back();
    test_required_formal_start_failure_rolls_back_without_focusing();
    test_orchestrator_lifecycle_and_snapshot_isolation();
    test_orchestrator_fast_ack_duplicate_and_initializing_gate();
    test_precheck_waits_for_an_explicit_transition_without_session_timeout();
    test_prepare_failure_is_retained_and_correlated();
    test_factory_exception_becomes_correlated_failure();
    test_cancel_during_preparation_cleans_once();
    return 0;
}

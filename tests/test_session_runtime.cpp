#include "session/SessionOrchestrator.h"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
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

struct TraceFeature final : ISessionFeature {
    TraceFeature(std::string value, std::vector<std::string>& trace, PrepareResult prepare_result)
        : value(std::move(value)), trace(trace), prepare_result(std::move(prepare_result)) {}

    const char* name() const noexcept override { return value.c_str(); }
    PrepareResult prepare(const SessionContext& context) override {
        session_seen = context.session_id;
        trace.push_back(value + ":prepare");
        return prepare_result;
    }
    void enterPhase(Phase phase) override {
        trace.push_back(value + ":" + phase_name(phase));
    }
    void stop(StopReason) noexcept override { trace.push_back(value + ":stop"); }
    FeatureHealth health() const override { return {true, false, "ready", {}, 0}; }

    std::string value;
    std::vector<std::string>& trace;
    PrepareResult prepare_result;
    std::string session_seen;
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
    runtime.enterPhase(Phase::Focusing);
    runtime.stop(StopReason::Completed);
    const std::vector<std::string> expected{
        "camera:prepare", "gimbal:prepare", "camera:PRECHECK", "camera:FOCUSING", "camera:stop"};
    assert(trace == expected);
    assert(runtime.degradedFeatures() == std::vector<std::string>{"gimbal"});
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
    monotonic = 150;
    assert(orchestrator.confirmReady().success);
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
    assert(idle.phase == Phase::Idle && !idle.runtime_active);
    assert(idle.context.session_id.empty());
    assert(idle.elapsed_ms == 0 && idle.remaining_ms == 0);

    monotonic = 200;
    wall = 2000;
    auto second = orchestrator.start({30000, SourceMode::RecordedInput});
    assert(second.success && second.context->session_id == "session-2000");
    auto active = snapshots.read();
    assert(active.context.session_id == "session-2000");
    assert(active.context.source_mode == SourceMode::RecordedInput);
    assert(active.visual.frame_id == 0);
    assert(active.assessment.level == std::nullopt);
    assert(orchestrator.end(StopReason::Cancelled).success);
}

}  // namespace

int main() {
    test_runtime_order_and_optional_degradation();
    test_required_prepare_failure_rolls_back();
    test_orchestrator_lifecycle_and_snapshot_isolation();
    return 0;
}

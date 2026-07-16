#include "app/AppContext.h"
#include "app/SystemStateStore.h"
#include "session/LegacySessionProjectionFeature.h"
#include "session/SessionOrchestrator.h"
#include "storage/SQLiteStore.h"

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>

using namespace edgefocus;

int main() {
    SystemStateStore store;
    SQLiteStore sqlite(":memory:");
    AppContext context;
    assert(sqlite.init(context));

    SessionReport previous;
    previous.session_id = "previous-session";
    store.updateLastReport(previous);

    session::SessionSnapshotPublisher snapshots("precheck-boundary-test");
    std::int64_t monotonic = 100;
    std::int64_t wall = 1000;
    session::SessionOrchestrator orchestrator(
        snapshots,
        [&](const session::SessionContext& session_context) {
            auto runtime = std::make_unique<session::SessionRuntime>(session_context);
            runtime->addFeature(
                std::make_unique<session::LegacySessionProjectionFeature>(store, sqlite), true);
            return runtime;
        },
        [&] { return monotonic; },
        [&] { return wall; },
        [](std::int64_t wall_ms) { return "boundary-" + std::to_string(wall_ms); });

    auto precheck = orchestrator.start({60000, session::SourceMode::LiveCamera});
    assert(precheck.success);
    assert(snapshots.read().runtime_active);
    assert(!snapshots.read().formal_active);
    assert(!store.snapshot().session.active);
    assert(!sqlite.sessionExists(precheck.context->session_id));

    assert(orchestrator.end(session::StopReason::Cancelled).success);
    assert(!sqlite.sessionExists(precheck.context->session_id));
    assert(store.snapshot().last_report.session_id == "previous-session");

    monotonic = 500;
    wall = 5000;
    auto second = orchestrator.start({60000, session::SourceMode::LiveCamera});
    assert(second.success);
    assert(!sqlite.sessionExists(second.context->session_id));

    monotonic = 600;
    wall = 6000;
    assert(orchestrator.confirmReady().success);
    const auto formal = store.snapshot().session;
    assert(formal.active);
    assert(formal.start_ms == 600);
    assert(formal.elapsed_ms == 0);
    assert(sqlite.sessionExists(second.context->session_id));
    const auto record = sqlite.querySessionById(second.context->session_id);
    assert(record && record->start_time_ms == 6000);
    assert(snapshots.read().formal_active);
    assert(snapshots.read().elapsed_ms == 0);

    monotonic = 900;
    assert(orchestrator.end(session::StopReason::Completed).success);
    assert(!store.snapshot().session.active);
    sqlite.shutdown(context);
    return 0;
}

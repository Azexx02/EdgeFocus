#pragma once
#include "app/Module.h"

#include "app/BoundedQueue.h"
#include "app/SystemStateStore.h"
#include "storage/SQLiteStore.h"
#include "vision/VisionResult.h"
#include "behavior/FocusStateMachine.h"

namespace edgefocus {

class AppContext;

/// Behavior understanding module. Consumes VisionResult, outputs FocusState.
class BehaviorEngine : public Module {
public:
    BehaviorEngine(BoundedQueue<VisionResult>& vision_queue, SystemStateStore& store, SQLiteStore& sqlite_store);
    bool init(AppContext& ctx) override;
    void runOnce(AppContext& ctx) override;
    void shutdown(AppContext& ctx) override;

private:
    BoundedQueue<VisionResult>& vision_queue_;
    SystemStateStore& store_;
    SQLiteStore& sqlite_store_;
    FocusStateMachine fsm_;
    bool session_was_active_ = false;
};

} // namespace edgefocus

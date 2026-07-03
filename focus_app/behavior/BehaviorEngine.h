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
    int64_t focus_accumulated_ms_ = 0;
    int64_t last_focus_update_ms_ = 0;
    std::string stable_distance_hint_;
    std::string stable_pose_hint_;
    bool stable_precheck_ready_ = false;
    std::string pending_distance_hint_;
    std::string pending_pose_hint_;
    bool pending_precheck_ready_ = false;
    int64_t pending_guidance_since_ms_ = 0;
};

} // namespace edgefocus

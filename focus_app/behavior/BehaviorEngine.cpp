#include "BehaviorEngine.h"
#include "app/AppContext.h"
#include "storage/StorageEvent.h"

namespace edgefocus {

BehaviorEngine::BehaviorEngine(BoundedQueue<VisionResult>& vision_queue, SystemStateStore& store, SQLiteStore& sqlite_store) 
    : Module("BehaviorEngine"), vision_queue_(vision_queue), store_(store), sqlite_store_(sqlite_store) {}

bool BehaviorEngine::init(AppContext& /*ctx*/) {
    LOGI("BehaviorEngine", "Initialized");
    return true;
}

void BehaviorEngine::runOnce(AppContext& /*ctx*/) {
    static bool first_run = true;
    if (first_run) {
        store_.updateFocusState(FocusState::CALIBRATING, 0, "Waiting for vision data...");
        first_run = false;
    }

    auto result_opt = vision_queue_.tryPop();
    if (result_opt.has_value()) {
        const auto& vision_result = result_opt.value();
        FocusState old_state = fsm_.getCurrentState();
        fsm_.processFrame(vision_result);
        FocusState new_state = fsm_.getCurrentState();
        
        if (old_state != new_state) {
            std::string reason = "State transition";
            store_.updateFocusState(new_state, vision_result.timestamp_ms, reason);
            
            StorageEvent ev;
            ev.session_id = "current_session"; // Should be retrieved from store session info
            ev.type = StorageEventType::StateChange;
            ev.timestamp = vision_result.timestamp_ms;
            ev.data = "Changed to " + std::to_string(static_cast<int>(new_state));
            sqlite_store_.logEvent(ev);
        }
    } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void BehaviorEngine::shutdown(AppContext& /*ctx*/) {
    LOGI("BehaviorEngine", "Shutdown");
}

} // namespace edgefocus

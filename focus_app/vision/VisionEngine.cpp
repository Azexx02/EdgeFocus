#include "VisionEngine.h"
#include "app/AppContext.h"

#include <chrono>
#include <cstdlib>
#include <thread>

namespace edgefocus {

VisionEngine::VisionEngine(Queue& output_queue, FrameQueue& frame_queue, SystemStateStore& store)
    : Module("VisionEngine"), output_queue_(output_queue), frame_queue_(&frame_queue), store_(store) {}

VisionEngine::~VisionEngine() = default;

bool VisionEngine::init(AppContext& /*ctx*/) {
    frame_counter_ = 0;
    runtime_mode_ = parseVisionRuntimeMode(std::getenv("EDGEFOCUS_VISION_MODE"));

    if (runtime_mode_ == VisionRuntimeMode::MOCK) {
        LOGI("VisionEngine", "Init in MOCK mode (explicit via EDGEFOCUS_VISION_MODE)");
        return true;
    }

    if (frame_queue_ == nullptr) {
        LOGW("VisionEngine", "No frame queue provided, will idle until frames available");
        return true;
    }

    const char* registry_env = std::getenv("EDGEFOCUS_MODEL_REGISTRY");
    const std::string registry_path = registry_env ? registry_env : "config/model_registry.json";
    scheduler_.reset(new InferenceScheduler(*frame_queue_, output_queue_));
    scheduler_->init(runtime_mode_, registry_path);
    LOGI("VisionEngine", "Init scheduler mode=%d adapter=%s registry=%s",
         static_cast<int>(runtime_mode_), scheduler_->activeAdapterName(), registry_path.c_str());
    return true;
}

void VisionEngine::setMockScenario(MockScenario scenario) {
    mock_source_.setScenario(scenario);
    runtime_mode_ = VisionRuntimeMode::MOCK;
    LOGI("VisionEngine", "Mock scenario changed to %d", static_cast<int>(scenario));
}



void VisionEngine::runOnce(AppContext& /*ctx*/) {
    SystemSnapshot snap = store_.snapshot();

    // --- IDLE: session not active ---
    if (!snap.session.active) {
        // Drain any stale frames from the camera queue
        if (frame_queue_) {
            while (frame_queue_->tryPop().has_value()) {}
        }
        // Block until session becomes active (with timeout for shutdown check)
        std::unique_lock<std::mutex> lock(store_.mutexForCV());
        store_.waitForSessionChange(lock, 1000);
        return;
    }

    // --- ACTIVE: run inference ---
    if (scheduler_ && runtime_mode_ != VisionRuntimeMode::MOCK) {
        scheduler_->runOnce();
        frame_counter_ = scheduler_->processedFrames();
        return;
    }

    // Mock path - only when EDGEFOCUS_VISION_MODE=mock explicitly
    VisionResult result = mock_source_.buildMockResult();
    output_queue_.push(std::move(result));
    ++frame_counter_;
    if (frame_counter_ % 100 == 0) {
        LOGD("VisionEngine", "Produced %lld mock results", (long long)frame_counter_);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void VisionEngine::shutdown(AppContext& /*ctx*/) {
    if (scheduler_) {
        scheduler_->shutdown();
    }
    LOGI("VisionEngine", "Shutdown (produced %lld results)", (long long)frame_counter_);
}

} // namespace edgefocus

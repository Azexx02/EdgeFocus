#pragma once
#include "app/Module.h"
#include "app/SystemStateStore.h"
#include "FrameBuffer.h"
#include "Page.h"
#include "StatusPage.h"
#include "StatsPage.h"
#include "ReviewPage.h"
#include "SettingsPage.h"

#include <memory>
#include <vector>
#include <thread>
#include <atomic>

namespace edgefocus {

class AppContext;

// ---------------------------------------------------------------------------
// UIManager — owns the UI rendering loop, page stack, and touch input.
//
// Reads ONLY from SystemStateStore (via snapshot()).
// Never writes to SystemStateStore.
// ---------------------------------------------------------------------------

class UIManager : public Module {
public:
    UIManager(SystemStateStore& store);
    ~UIManager() override;

    bool init(AppContext& ctx) override;
    void runOnce(AppContext& ctx) override;
    void shutdown(AppContext& ctx) override;

private:
    enum PageId {
        PAGE_STATUS = 0,
        PAGE_STATS = 1,
        PAGE_SETTINGS = 2,
        PAGE_REVIEW = 3,  // Modal, not part of swipe cycle
    };

    void switchPage(int id);
    void renderFrame();
    void renderGlobalHeader(const SystemSnapshot& snap);
    void handleTouch();

    // Components
    FrameBuffer fb_;
    SystemStateStore& store_;

    // Pages
    std::vector<std::unique_ptr<Page>> pages_;
    int current_page_ = PAGE_STATUS;

    // Touch input & Swipe detection
    int touch_fd_ = -1;
    bool use_i2c_touch_ = false;
    int touch_x_ = 0;
    int touch_y_ = 0;
    int touch_start_x_ = 0;
    int touch_start_y_ = 0;
    bool touch_pending_ = false;

    // Screenshot counter (for headless mode)
    int screenshot_count_ = 0;

    // Frame timing
    static constexpr int FRAME_INTERVAL_MS = 100;  // 10 FPS
};

} // namespace edgefocus

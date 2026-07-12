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
#include <mutex>

namespace edgefocus {

class AppContext;
class ReportGenerator;
class SQLiteStore;
class ModeManager;

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

    void setReportGenerator(ReportGenerator* rg) { report_gen_ = rg; }
    void setSQLiteStore(SQLiteStore* store) { sqlite_store_ = store; }
    void setModeManager(ModeManager* mm) { mode_mgr_ = mm; }

    int currentPageForTest() const { return current_page_; }
    void setCurrentPageForTest(int id) { switchPage(id); }
    bool screenHardwareBypassedForTest() const { return screen_hardware_bypassed_; }
    bool framebufferIsRealForTest() const { return fb_.isReal(); }

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
    void touchWorkerLoop();
    void queueTouchSwipe(int direction);
    void queueTouchTap(int x, int y);
    bool splashActive() const;

    // Components
    FrameBuffer fb_;
    SystemStateStore& store_;
    ReportGenerator* report_gen_ = nullptr;
    SQLiteStore* sqlite_store_ = nullptr;
    ModeManager* mode_mgr_ = nullptr;

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
    bool touch_swipe_sent_ = false;
    std::thread touch_thread_;
    std::atomic<bool> touch_thread_running_{false};
    std::mutex touch_event_mutex_;
    int pending_swipe_direction_ = 0;
    bool pending_tap_ = false;
    int pending_tap_x_ = 0;
    int pending_tap_y_ = 0;

    // Screenshot counter (for headless mode)
    int screenshot_count_ = 0;

    // Frame timing
    static constexpr int FRAME_INTERVAL_MS = 100;  // 10 FPS

    bool last_session_active_ = false;
    bool screen_hardware_bypassed_ = false;
    int64_t splash_until_ms_ = 0;
};

} // namespace edgefocus

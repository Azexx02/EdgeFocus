#pragma once
#include "Page.h"
#include "storage/ReportGenerator.h"
#include "storage/SQLiteStore.h"

namespace edgefocus {

/// Main status screen — shows focus state, session timer, quick stats, active reminder.
class StatusPage : public Page {
public:
    void render(FrameBuffer& fb, const SystemSnapshot& snap) override;
    bool onTouch(int x, int y) override;
    const char* name() const override { return "StatusPage"; }

    void setReportGenerator(ReportGenerator* rg) { report_gen_ = rg; }
    void setSQLiteStore(SQLiteStore* store) { sqlite_store_ = store; }
    void syncWithSnapshotForTest(const SystemSnapshot& snap);

    int currentInternalStateForTest() const { return static_cast<int>(internal_state_); }
    int targetDurationMinForTest() const { return target_duration_min_; }
    int64_t countdownSecondsForTest(const SystemSnapshot& snap) const;

    static constexpr int READY_STATE = 0;
    static constexpr int CALIBRATING_STATE = 1;
    static constexpr int FOCUSED_STATE = 2;
    static constexpr int PAUSED_STATE = 3;

private:
    enum class InternalState {
        READY,
        CALIBRATING,
        FOCUSED,
        PAUSED
    };

    InternalState internal_state_ = InternalState::READY;
    ReportGenerator* report_gen_ = nullptr;
    SQLiteStore* sqlite_store_ = nullptr;

    // Focus duration selector (minutes)
    int target_duration_min_ = 25;  // default 25min pomodoro
    bool local_duration_override_ = false;
    bool was_session_active_ = false;
    static constexpr int DURATION_MIN = 5;
    static constexpr int DURATION_MAX = 120;
    static constexpr int DURATION_STEP = 5;

    // Calibration auto-skip timer
    int64_t calibration_start_ms_ = 0;

    Color focusStateColor(FocusState state) const;
    const char* focusStateText(FocusState state) const;
    const char* focusStateEmoji(FocusState state) const;
    std::string formatDuration(int64_t seconds) const;
    int64_t countdownSeconds(const SystemSnapshot& snap) const;
    int64_t elapsedSeconds(const SystemSnapshot& snap) const;
    void syncFromSnapshot(const SystemSnapshot& snap);
};

} // namespace edgefocus

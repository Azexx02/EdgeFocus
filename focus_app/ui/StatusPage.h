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

private:
    enum class InternalState {
        READY,
        CALIBRATING,
        FOCUSED
    };

    InternalState internal_state_ = InternalState::READY;
    ReportGenerator* report_gen_ = nullptr;
    SQLiteStore* sqlite_store_ = nullptr;

    // Focus duration selector (minutes)
    int target_duration_min_ = 25;  // default 25min pomodoro
    static constexpr int DURATION_MIN = 5;
    static constexpr int DURATION_MAX = 120;
    static constexpr int DURATION_STEP = 5;

    Color focusStateColor(FocusState state) const;
    const char* focusStateText(FocusState state) const;
    const char* focusStateEmoji(FocusState state) const;
    std::string formatDuration(int64_t seconds) const;
};

} // namespace edgefocus

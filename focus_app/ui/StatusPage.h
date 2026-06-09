#pragma once
#include "Page.h"

namespace edgefocus {

/// Main status screen — shows focus state, session timer, quick stats, active reminder.
class StatusPage : public Page {
public:
    void render(FrameBuffer& fb, const SystemSnapshot& snap) override;
    bool onTouch(int x, int y) override;
    const char* name() const override { return "StatusPage"; }

private:
    enum class InternalState {
        READY,
        CALIBRATING,
        FOCUSED
    };

    InternalState internal_state_ = InternalState::READY;

    Color focusStateColor(FocusState state) const;
    const char* focusStateText(FocusState state) const;
    const char* focusStateEmoji(FocusState state) const;
    std::string formatDuration(int64_t seconds) const;
};

} // namespace edgefocus

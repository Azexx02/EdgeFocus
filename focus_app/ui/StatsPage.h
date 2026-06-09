#pragma once
#include "Page.h"

namespace edgefocus {

/// Today's statistics page — focus time, sessions, events.
class StatsPage : public Page {
public:
    void render(FrameBuffer& fb, const SystemSnapshot& snap) override;
    bool onTouch(int x, int y) override;
    const char* name() const override { return "StatsPage"; }

private:
    std::string formatMinutes(int64_t seconds) const;
};

} // namespace edgefocus

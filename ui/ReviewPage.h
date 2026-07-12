#pragma once
#include "Page.h"

namespace edgefocus {

/// Review page — shows last session report summary.
class ReviewPage : public Page {
public:
    void render(FrameBuffer& fb, const SystemSnapshot& snap) override;
    bool onTouch(int x, int y) override;
    const char* name() const override { return "ReviewPage"; }

private:
    std::string formatDuration(int64_t seconds) const;
};

} // namespace edgefocus

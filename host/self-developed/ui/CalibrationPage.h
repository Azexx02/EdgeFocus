#pragma once
#include "Page.h"

namespace edgefocus {

/// Calibration/placement guidance page — shows frame quality and adjustment tips.
class CalibrationPage : public Page {
public:
    void render(FrameBuffer& fb, const SystemSnapshot& snap) override;
    bool onTouch(int x, int y) override;
    const char* name() const override { return "CalibrationPage"; }
};

} // namespace edgefocus

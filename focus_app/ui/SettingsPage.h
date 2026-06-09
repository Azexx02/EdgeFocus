#pragma once
#include "Page.h"

namespace edgefocus {

/// Settings/more page — mode selection, device status, timeout config.
class SettingsPage : public Page {
public:
    void render(FrameBuffer& fb, const SystemSnapshot& snap) override;
    bool onTouch(int x, int y) override;
    const char* name() const override { return "SettingsPage"; }

    using ModeChangeCallback = std::function<void(SystemMode)>;
    void setModeChangeCallback(ModeChangeCallback cb) { mode_cb_ = std::move(cb); }

    using PageChangeCallback = std::function<void(int)>;
    void setPageChangeCallback(PageChangeCallback cb) { page_cb_ = std::move(cb); }

private:
    ModeChangeCallback mode_cb_;
    PageChangeCallback page_cb_;

    // Away timeout setting (in minutes, for display)
    static constexpr int TIMEOUT_MIN_VAL = 5;
    static constexpr int TIMEOUT_MAX_VAL = 120;
    static constexpr int TIMEOUT_STEP = 5;
};

} // namespace edgefocus

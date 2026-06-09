#pragma once
#include "Page.h"

namespace edgefocus {

/// Settings/more page — mode selection, device status, review entry.
class SettingsPage : public Page {
public:
    void render(FrameBuffer& fb, const SystemSnapshot& snap) override;
    bool onTouch(int x, int y) override;
    const char* name() const override { return "SettingsPage"; }

    /// Set callbacks for mode changes (UI doesn't write to SystemStateStore directly,
    /// but can signal the UIManager which then delegates to ModeManager).
    using ModeChangeCallback = std::function<void(SystemMode)>;
    void setModeChangeCallback(ModeChangeCallback cb) { mode_cb_ = std::move(cb); }

    using PageChangeCallback = std::function<void(int)>;
    void setPageChangeCallback(PageChangeCallback cb) { page_cb_ = std::move(cb); }

private:
    ModeChangeCallback mode_cb_;
    PageChangeCallback page_cb_;
};

} // namespace edgefocus

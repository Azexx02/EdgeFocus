#pragma once
#include "FrameBuffer.h"
#include "app/SystemStateStore.h"

namespace edgefocus {

// ---------------------------------------------------------------------------
// Page — base class for all UI screens.
// Each page renders to a FrameBuffer using data from SystemStateStore.
// Pages never write to SystemStateStore.
// ---------------------------------------------------------------------------

class Page {
public:
    virtual ~Page() = default;

    /// Called once when the page becomes active.
    virtual void onEnter() {}

    /// Called once when the page is deactivated.
    virtual void onLeave() {}

    /// Render the page to the framebuffer.
    virtual void render(FrameBuffer& fb, const SystemSnapshot& snap) = 0;

    /// Set callback for switching pages.
    void setPageSwitcher(std::function<void(int)> fn) { switch_page_ = fn; }

    /// Set store reference.
    void setStore(SystemStateStore* store) { store_ = store; }

    /// Handle touch at (x, y). Return true if consumed.
    virtual bool onTouch(int x, int y) { (void)x; (void)y; return false; }

    /// Page name for debug logging.
    virtual const char* name() const = 0;

protected:
    std::function<void(int)> switch_page_;
    SystemStateStore* store_ = nullptr;
};

} // namespace edgefocus

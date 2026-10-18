#pragma once
#include "app/Logger.h"

namespace edgefocus {
class ScreenController {
public:
    static bool isAvailable();
    void flashScreen();
    int flashCount() const { return flash_count_; }

private:
    int flash_count_ = 0;
};
} // namespace edgefocus

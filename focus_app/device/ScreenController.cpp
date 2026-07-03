#include "ScreenController.h"
#include <cstdlib>
#include <fstream>

namespace edgefocus {
bool ScreenController::isAvailable() {
#if defined(__linux__)
    return std::ifstream("/dev/fb0").good() || std::ifstream("/dev/spidev0.0").good();
#else
    return false;
#endif
}

void ScreenController::flashScreen() {
    ++flash_count_;
    LOGI("ScreenController", "Flashing screen (visual flash should be implemented in UIManager)");
    // Removed hazardous system() call to lcd_spi_touch_test which locks the SPI bus.
}
} // namespace edgefocus

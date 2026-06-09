#include "ScreenController.h"
#include <cstdlib>

namespace edgefocus {
void ScreenController::flashScreen() {
    LOGI("ScreenController", "Flashing screen (visual flash should be implemented in UIManager)");
    // Removed hazardous system() call to lcd_spi_touch_test which locks the SPI bus.
}
} // namespace edgefocus

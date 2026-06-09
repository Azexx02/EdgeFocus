#include "BleBandController.h"
#include <cstdlib>

namespace edgefocus {
void BleBandController::sendAlert() {
    LOGI("BleBandController", "Sending BLE alert");
#ifdef __linux__
    system("bash /root/scripts/send_ble_signal.sh &");
#endif
}
} // namespace edgefocus

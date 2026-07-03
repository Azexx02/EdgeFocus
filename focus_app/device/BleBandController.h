#pragma once
#include "app/Logger.h"
#include <cstdint>
#include <atomic>
#include <thread>
#include <array>
#include <mutex>
#include <string>

namespace edgefocus {
class BleBandController {
public:
    BleBandController();
    ~BleBandController();
    void startConnection();
    void stopConnection();
    bool sendAlert(uint8_t level);
    bool isConnected() const { return connected_.load(); }
    bool isEnabled() const { return enabled_.load(); }
    uint8_t lastAlertPattern() const { return last_alert_pattern_.load(); }
    uint8_t lastAlertLevel() const { return last_alert_level_.load(); }
    int writeAttemptCount() const { return write_attempt_count_.load(); }
    std::array<uint8_t, 3> lastAlertFrame() const;
    std::string lastWriteStatus() const;
private:
    void connectionThreadFunc();
    bool writeAlertFrame(uint8_t pattern, uint8_t level);

    std::atomic<bool> connected_{false};
    std::atomic<bool> enabled_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<uint8_t> last_alert_pattern_{0};
    std::atomic<uint8_t> last_alert_level_{0};
    std::atomic<int> write_attempt_count_{0};
    mutable std::mutex state_mutex_;
    std::array<uint8_t, 3> last_alert_frame_{{0, 0, 0}};
    std::string last_write_status_;
#if defined(__linux__) && defined(EDGEFOCUS_WITH_BLUEZ)
    void* bluez_bus_{nullptr};
#endif
    std::thread conn_thread_;
};
} // namespace edgefocus

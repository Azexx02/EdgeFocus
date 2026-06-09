#pragma once
#include "app/Logger.h"
#include <cstdint>
#include <atomic>
#include <thread>

namespace edgefocus {
class BleBandController {
public:
    BleBandController();
    ~BleBandController();
    void startConnection();
    void stopConnection();
    void sendAlert(uint8_t level);
    bool isConnected() const { return connected_.load(); }
    bool isEnabled() const { return enabled_.load(); }
private:
    void connectionThreadFunc();
    std::atomic<bool> connected_{false};
    std::atomic<bool> enabled_{false};
    std::atomic<bool> stop_requested_{false};
    std::thread conn_thread_;
    static constexpr const char* TARGET_NAME = "ESP32-Band";
    static constexpr const char* FIFO_PATH = "/tmp/ble_cmd_fifo";
    static constexpr const char* STATUS_PATH = "/tmp/ble_status";
};
} // namespace edgefocus

#pragma once
#include "app/Logger.h"
#include <cstdint>
#include <atomic>
#include <thread>
#include <array>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>

namespace edgefocus {
class BleBandController {
public:
    BleBandController();
    ~BleBandController();
    void startConnection();
    void stopConnection();
    bool requestImmediateConnection(bool reconnect);
    bool sendAlert(uint8_t level);
    bool isConnected() const { return connected_.load(); }
    bool isEnabled() const { return enabled_.load(); }
    uint8_t lastAlertPattern() const { return last_alert_pattern_.load(); }
    uint8_t lastAlertLevel() const { return last_alert_level_.load(); }
    int writeAttemptCount() const { return write_attempt_count_.load(); }
    std::uint64_t sentSequence() const { return sent_sequence_.load(); }
    std::array<uint8_t, 3> lastAlertFrame() const;
    std::string lastWriteStatus() const;
private:
    struct QueuedAlert {
        std::uint64_t sequence{};
        uint8_t pattern{};
        uint8_t level{};
        std::chrono::steady_clock::time_point expires_at;
        int failure_count{};
    };

    void connectionThreadFunc();
    void writerThreadFunc();
    bool writeAlertFrame(uint8_t pattern, uint8_t level);
    void setWriteStatus(const std::string& status);

    std::atomic<bool> connected_{false};
    std::atomic<bool> enabled_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<uint8_t> last_alert_pattern_{0};
    std::atomic<uint8_t> last_alert_level_{0};
    std::atomic<int> write_attempt_count_{0};
    std::atomic<std::uint64_t> sent_sequence_{0};
    mutable std::mutex state_mutex_;
    std::condition_variable writer_cv_;
    std::condition_variable connection_cv_;
    std::atomic<bool> search_requested_{false};
    std::atomic<bool> reconnect_requested_{false};
    std::optional<QueuedAlert> pending_alert_;
    std::uint64_t alert_sequence_{};
    std::array<uint8_t, 3> last_alert_frame_{{0, 0, 0}};
    std::string last_write_status_;
#if defined(__linux__) && defined(EDGEFOCUS_WITH_BLUEZ)
    void* bluez_bus_{nullptr};
#endif
    std::thread conn_thread_;
    std::thread writer_thread_;
};
} // namespace edgefocus

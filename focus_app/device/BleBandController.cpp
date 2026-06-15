#include "BleBandController.h"
#include <chrono>
#include <thread>
#include <signal.h>
#include <fstream>
#include <cstdio>
#include <cstring>

#ifdef __linux__
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace edgefocus {

BleBandController::BleBandController() = default;
BleBandController::~BleBandController() { stopConnection(); }

void BleBandController::startConnection() {
    if (enabled_.load()) return;
    stop_requested_ = false;
    enabled_ = true;
    conn_thread_ = std::thread(&BleBandController::connectionThreadFunc, this);
    LOGI("BleBandController", "BLE connection thread started");
}

void BleBandController::stopConnection() {
    if (!enabled_.load()) return;
    stop_requested_ = true;
    enabled_ = false;
    connected_ = false;
    // Signal daemon to quit
    system("echo quit > /tmp/ble_cmd_fifo 2>/dev/null");
    if (conn_thread_.joinable()) conn_thread_.join();
    // Cleanup
    system("pkill -9 -f ble_connect 2>/dev/null");
    unlink(FIFO_PATH);
    unlink(STATUS_PATH);
    LOGI("BleBandController", "BLE connection stopped");
}

void BleBandController::sendAlert(uint8_t level) {
    if (!connected_.load()) return;
    (void)level;
    system("echo buzz > /tmp/ble_cmd_fifo 2>/dev/null");
    LOGI("BleBandController", "BLE alert sent (level=%d)", level);
}

static std::string scanForDevice(const char* target_name) {
    // Use ble_scan.sh script which uses coproc for proper async BLE scanning
    std::string cmd = std::string("bash /root/scripts/ble_scan.sh ") + target_name + " 12";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    char line[256];
    std::string mac;
    if (fgets(line, sizeof(line), pipe)) {
        mac = line;
        // Trim whitespace
        while (!mac.empty() && (mac.back() == '\n' || mac.back() == '\r' || mac.back() == ' '))
            mac.pop_back();
    }
    int ret = pclose(pipe);
    if (ret != 0) return "";  // scan script returned non-zero = not found
    return mac;
}

void BleBandController::connectionThreadFunc() {
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGINT);
    sigaddset(&sigset, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sigset, nullptr);

    while (!stop_requested_.load()) {
        // Scan for device
        LOGI("BleBandController", "Scanning for %s...", TARGET_NAME);
        std::string mac = scanForDevice(TARGET_NAME);

        if (mac.empty() || stop_requested_.load()) {
            if (!stop_requested_.load()) LOGW("BleBandController", "%s not found, retrying...", TARGET_NAME);
            std::this_thread::sleep_for(std::chrono::seconds(3));
            continue;
        }

        LOGI("BleBandController", "Found %s at %s", TARGET_NAME, mac.c_str());

        // Create FIFO for daemon communication
        unlink(FIFO_PATH);
        mkfifo(FIFO_PATH, 0666);

        // Start connect script via popen (coproc works in popen context)
        std::string cmd = "bash /root/scripts/ble_connect.sh " + mac + " 2>&1";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            LOGE("BleBandController", "Failed to start connect script");
            std::this_thread::sleep_for(std::chrono::seconds(3));
            continue;
        }

        // Read output to monitor connection status
        bool script_connected = false;
        char line[256];
        while (fgets(line, sizeof(line), pipe) && !stop_requested_.load()) {
            std::string s(line);
            // Trim newline
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            LOGI("BleBandController", "BLE: %s", s.c_str());

            if (s.find("Connected") != std::string::npos && s.find("failed") == std::string::npos) {
                script_connected = true;
                connected_ = true;
                LOGI("BleBandController", "Connected to %s (%s)", TARGET_NAME, mac.c_str());
            }
            if (s.find("Disconnected") != std::string::npos || s.find("Exited") != std::string::npos) {
                break;
            }
            if (s.find("Connection failed") != std::string::npos) {
                break;
            }
        }

        // Script exited - connection lost
        pclose(pipe);
        connected_ = false;
        unlink(FIFO_PATH);
        unlink(STATUS_PATH);

        if (script_connected) {
            LOGW("BleBandController", "Connection lost");
        }

        if (!stop_requested_.load()) {
            LOGI("BleBandController", "Reconnecting in 3s...");
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
    }
    connected_ = false;
}

} // namespace edgefocus

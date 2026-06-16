#include "BleBandController.h"
#include <chrono>
#include <thread>
#include <fstream>
#include <cstdio>
#include <cstring>

#ifdef __linux__
#include <signal.h>
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
#ifdef __linux__
    conn_thread_ = std::thread(&BleBandController::connectionThreadFunc, this);
#endif
    LOGI("BleBandController", "BLE connection thread started");
}

void BleBandController::stopConnection() {
    if (!enabled_.load()) return;
    stop_requested_ = true;
    enabled_ = false;
    connected_ = false;
#ifdef __linux__
    system("echo quit > /tmp/ble_cmd_fifo 2>/dev/null");
    if (conn_thread_.joinable()) conn_thread_.join();
    system("pkill -9 -f ble_connect 2>/dev/null");
    unlink(FIFO_PATH);
    unlink(STATUS_PATH);
#endif
    LOGI("BleBandController", "BLE connection stopped");
}

void BleBandController::sendAlert(uint8_t level) {
    if (!connected_.load()) return;
    uint8_t pattern = (level >= 3) ? 0x03 : (level >= 2) ? 0x02 : 0x01;
#ifdef __linux__
    std::string cmd = "echo 'alert " + std::to_string(pattern) + "' > /tmp/ble_cmd_fifo 2>/dev/null";
    system(cmd.c_str());
#endif
    LOGI("BleBandController", "BLE alert sent (level=%d, pattern=0x%02x)", level, pattern);
}

#ifdef __linux__
static std::string scanForDevice(const char* target_name) {
    std::string cmd = std::string("bash /root/scripts/ble_scan.sh ") + target_name + " 12";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    char line[256];
    std::string mac;
    if (fgets(line, sizeof(line), pipe)) {
        mac = line;
        while (!mac.empty() && (mac.back() == '\n' || mac.back() == '\r' || mac.back() == ' '))
            mac.pop_back();
    }
    int ret = pclose(pipe);
    if (ret != 0) return "";
    return mac;
}

void BleBandController::connectionThreadFunc() {
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGINT);
    sigaddset(&sigset, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sigset, nullptr);

    while (!stop_requested_.load()) {
        LOGI("BleBandController", "Scanning for %s...", TARGET_NAME);
        std::string mac = scanForDevice(TARGET_NAME);

        if (mac.empty() || stop_requested_.load()) {
            if (!stop_requested_.load()) LOGW("BleBandController", "%s not found, retrying...", TARGET_NAME);
            std::this_thread::sleep_for(std::chrono::seconds(3));
            continue;
        }

        LOGI("BleBandController", "Found %s at %s", TARGET_NAME, mac.c_str());

        unlink(FIFO_PATH);
        mkfifo(FIFO_PATH, 0666);

        std::string cmd = "bash /root/scripts/ble_connect.sh " + mac + " 2>&1";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            LOGE("BleBandController", "Failed to start connect script");
            std::this_thread::sleep_for(std::chrono::seconds(3));
            continue;
        }

        bool script_connected = false;
        char line[256];
        while (fgets(line, sizeof(line), pipe) && !stop_requested_.load()) {
            std::string s(line);
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
#endif

} // namespace edgefocus

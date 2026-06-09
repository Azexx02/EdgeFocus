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
    system("pkill -9 -f ble_daemon 2>/dev/null");
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

        // Start daemon with known MAC
        std::string cmd = "nohup bash /root/scripts/ble_daemon.sh " + mac
            + " > /tmp/ble_daemon.log 2>&1 &";
        system(cmd.c_str());

        // Wait for daemon to connect (check status file)
        LOGI("BleBandController", "Waiting for daemon to connect...");
        bool daemon_connected = false;
        for (int i = 0; i < 30 && !stop_requested_.load(); i++) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::ifstream f(STATUS_PATH);
            if (f.is_open()) {
                std::string status;
                std::getline(f, status);
                if (status.find("connected") == 0 && status.find("disconnected") == std::string::npos) {
                    daemon_connected = true;
                    break;
                }
            }
        }

        if (daemon_connected && !stop_requested_.load()) {
            connected_ = true;
            LOGI("BleBandController", "Connected to %s (%s)", TARGET_NAME, mac.c_str());

            // Keep alive - monitor connection status
            while (!stop_requested_.load()) {
                std::this_thread::sleep_for(std::chrono::seconds(3));
                std::ifstream f(STATUS_PATH);
                if (f.is_open()) {
                    std::string status;
                    std::getline(f, status);
                    if (status.find("disconnected") == 0) {
                        LOGW("BleBandController", "Connection lost");
                        break;
                    }
                }
            }
        }

        connected_ = false;
        // Stop daemon
        system("echo quit > /tmp/ble_cmd_fifo 2>/dev/null");
        system("pkill -9 -f ble_daemon 2>/dev/null");
        unlink(FIFO_PATH);
        unlink(STATUS_PATH);

        if (!stop_requested_.load()) {
            LOGI("BleBandController", "Reconnecting in 3s...");
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
    }
    connected_ = false;
}

} // namespace edgefocus

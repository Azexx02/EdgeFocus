#pragma once

#include <cstdint>
#include <string>

namespace edgefocus {

struct GimbalSnapshot {
    bool connected = false;
    bool online = false;
    bool safe = false;
    bool xhome = false;
    bool yhome = false;
    bool cal = false;
    int boost = 0;
    int motor = 0;
    int en = 0;
    int ix = 0;
    int iy = 0;
    float angle_x = 0.0f;
    float angle_y = 0.0f;
    int64_t last_seen_ms = 0;
    std::string last_error;
};

struct GimbalCommandResult {
    bool success = false;
    bool timeout = false;
    std::string command;
    std::string start_response;
    std::string final_response;
    std::string error;
};

class GimbalController {
public:
    enum class PollState {
        OFFLINE,
        VER,
        PING,
        STOP,
        STAT,
        FAULT,
        ANGLE
    };

    GimbalController();
    ~GimbalController();

    const GimbalSnapshot& status() const { return status_; }
    PollState state() const { return state_; }

    // One-cycle polling function for the frozen V1 startup/query sequence:
    // VER -> PING -> STOP -> STAT -> FAULT -> ANGLE -> STAT -> ...
    bool pollOnce();
    void shutdown();
    GimbalCommandResult executeBusinessCommand(const std::string& command, int timeout_ms);

    std::string nextCommand() const;
    static std::string commandForState(PollState state);

    // Pure parse helpers for unit tests and future protocol extension.
    static bool parseStat(const std::string& line, GimbalSnapshot& out, std::string* error = nullptr);
    static bool parseAngle(const std::string& line, float& angle_x, float& angle_y, std::string* error = nullptr);

    static int readTimeoutMsFromEnv();

private:
    struct Response {
        std::string line;
        bool timeout = false;
        bool ok = false;
        bool empty = false;
    };

#ifdef __linux__
    bool openDevice();
    void closeDevice();
    bool isDeviceOpen() const;
    bool writeCommand(const std::string& cmd);
    bool writeEmergencyStop();
    Response readLine();
    Response readLineWithTimeout(int timeout_ms);
    bool initializeSerial();
#else
    bool openDevice() { return false; }
    void closeDevice() {}
    bool isDeviceOpen() const { return false; }
    bool writeCommand(const std::string& cmd) { (void)cmd; return false; }
    bool writeEmergencyStop() { return false; }
    Response readLine() { return {}; }
#endif

    bool processPingResponse(const Response& response, std::string& error);
    bool processSafeResponse(const Response& response, std::string& error);
    bool processStatResponse(const Response& response, std::string& error);
    void recordTimeout();
    void recordFailure(const std::string& reason, bool timeout);
    void clearFailureState();
    static bool parseIntValue(const std::string& token, int& out);
    static bool parseFloatValue(const std::string& token, float& out);
    static bool parseBoolValue(const std::string& token, bool& out);

    static int64_t nowMs();

    const std::string uart_device_;
    const int read_timeout_ms_;

    PollState state_ = PollState::OFFLINE;
    GimbalSnapshot status_;
    int consecutive_timeouts_ = 0;

#ifdef __linux__
    int fd_ = -1;
#endif
};

}  // namespace edgefocus

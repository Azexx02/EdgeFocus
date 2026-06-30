#include "GimbalController.h"

#include "app/Logger.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef __linux__
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace edgefocus {

namespace {
constexpr int kDefaultReadTimeoutMs = 500;
constexpr int kMinReadTimeoutMs = 300;
constexpr int kMaxReadTimeoutMs = 1000;
constexpr const char* kDefaultUartDevice = "/dev/ttySTM1";

std::string trimCopy(const std::string& value) {
    auto begin = std::find_if_not(value.begin(), value.end(),
                                  [](unsigned char c) { return std::isspace(c); });
    auto end = std::find_if_not(value.rbegin(), value.rend(),
                                [](unsigned char c) { return std::isspace(c); }).base();
    if (begin >= end) return "";
    return std::string(begin, end);
}

std::string toUpper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

std::vector<std::string> splitTokens(const std::string& value) {
    std::vector<std::string> tokens;
    std::string current;
    for (char ch : value) {
        if (ch == ',' || ch == ';' || ch == ' ' || ch == '\t') {
            if (!current.empty()) {
                tokens.push_back(trimCopy(current));
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        tokens.push_back(trimCopy(current));
    }
    return tokens;
}

std::string getenvOrDefault(const char* key, const char* default_value) {
#ifdef _WIN32
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, key) != 0 || !value) {
        return default_value;
    }
    std::unique_ptr<char, decltype(&std::free)> guard(value, &std::free);
    return guard.get();
#else
    const char* value = std::getenv(key);
    return value ? value : default_value;
#endif
}

}  // namespace

int64_t GimbalController::nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

GimbalController::GimbalController()
    : uart_device_(getenvOrDefault("EDGEFOCUS_GIMBAL_UART_DEVICE", kDefaultUartDevice)),
      read_timeout_ms_(readTimeoutMsFromEnv()) {
    status_.last_error = "INIT";
}

GimbalController::~GimbalController() {
    shutdown();
}

void GimbalController::shutdown() {
    closeDevice();
}

int GimbalController::readTimeoutMsFromEnv() {
    const std::string raw =
        getenvOrDefault("EDGEFOCUS_GIMBAL_READ_TIMEOUT_MS",
                        std::to_string(kDefaultReadTimeoutMs).c_str());
    try {
        const int parsed = std::stoi(trimCopy(raw));
        if (parsed < kMinReadTimeoutMs) return kMinReadTimeoutMs;
        if (parsed > kMaxReadTimeoutMs) return kMaxReadTimeoutMs;
        return parsed;
    } catch (...) {
        return kDefaultReadTimeoutMs;
    }
}

std::string GimbalController::commandForState(PollState state) {
    switch (state) {
        case PollState::OFFLINE:
        case PollState::PING:
            return "PING";
        case PollState::SAFE:
            return "SAFE";
        case PollState::STAT:
            return "STAT";
        default:
            return "PING";
    }
}

std::string GimbalController::nextCommand() const {
    return commandForState(state_);
}

bool GimbalController::parseBoolValue(const std::string& value, bool& out) {
    const std::string upper = toUpper(trimCopy(value));
    if (upper == "1" || upper == "TRUE" || upper == "ON" || upper == "YES") {
        out = true;
        return true;
    }
    if (upper == "0" || upper == "FALSE" || upper == "OFF" || upper == "NO") {
        out = false;
        return true;
    }
    return false;
}

bool GimbalController::parseIntValue(const std::string& value, int& out) {
    try {
        out = std::stoi(trimCopy(value));
        return true;
    } catch (...) {
        return false;
    }
}

bool GimbalController::parseFloatValue(const std::string& value, float& out) {
    try {
        out = std::stof(trimCopy(value));
        return true;
    } catch (...) {
        return false;
    }
}

bool GimbalController::parseStat(const std::string& line, GimbalSnapshot& out, std::string* error) {
    out = GimbalSnapshot{};
    const std::string trimmed = trimCopy(line);
    if (trimmed.empty()) {
        if (error) *error = "empty line";
        return false;
    }

    const std::string upper = toUpper(trimmed);
    if (upper.rfind("ERR", 0) == 0) {
        if (error) *error = trimmed;
        return false;
    }

    if (upper.rfind("STAT", 0) != 0) {
        if (error) *error = "not a STAT response";
        return false;
    }

    std::string payload = trimCopy(trimmed.substr(4));
    if (!payload.empty() &&
        (payload.front() == ':' || payload.front() == ',' || payload.front() == '=')) {
        payload = trimCopy(payload.substr(1));
    }
    if (payload.empty()) {
        if (error) *error = "empty STAT payload";
        return false;
    }

    GimbalSnapshot parsed;
    bool parsed_any = false;
    auto tokens = splitTokens(payload);
    bool key_value_mode = false;
    for (const auto& token : tokens) {
        if (token.find('=') != std::string::npos) {
            key_value_mode = true;
            break;
        }
    }

    if (key_value_mode) {
        bool ok = false;
        for (const auto& token : tokens) {
            const size_t eq = token.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = toUpper(trimCopy(token.substr(0, eq)));
            const std::string raw_value = trimCopy(token.substr(eq + 1));

            if (key == "CONNECTED") {
                if (parseBoolValue(raw_value, parsed.connected)) parsed_any = true;
            } else if (key == "ONLINE") {
                if (parseBoolValue(raw_value, parsed.online)) parsed_any = true;
            } else if (key == "SAFE") {
                if (parseBoolValue(raw_value, parsed.safe)) parsed_any = true;
            } else if (key == "BOOST") {
                if (parseIntValue(raw_value, parsed.boost)) parsed_any = true;
            } else if (key == "MOTOR") {
                if (parseIntValue(raw_value, parsed.motor)) parsed_any = true;
            } else if (key == "EN") {
                if (parseIntValue(raw_value, parsed.en)) parsed_any = true;
            } else if (key == "IX") {
                if (parseIntValue(raw_value, parsed.ix)) parsed_any = true;
            } else if (key == "IY") {
                if (parseIntValue(raw_value, parsed.iy)) parsed_any = true;
            } else if (key == "ANGLE_X") {
                if (parseFloatValue(raw_value, parsed.angle_x)) parsed_any = true;
            } else if (key == "ANGLE_Y") {
                if (parseFloatValue(raw_value, parsed.angle_y)) parsed_any = true;
            }
        }
    } else {
        if (tokens.size() < 3) {
            if (error) *error = "insufficient positional STAT tokens";
            return false;
        }

        bool parsed_field = false;
        if (parseBoolValue(tokens[0], parsed.connected)) parsed_field = true;
        parsed_any = parsed_any || parsed_field;
        if (!parsed_field) parsed.connected = (tokens[0] == "1" || toUpper(tokens[0]) == "TRUE");

        parsed_field = false;
        if (parseBoolValue(tokens[1], parsed.online)) parsed_field = true;
        else parsed.online = (tokens[1] == "1" || toUpper(tokens[1]) == "TRUE");
        parsed_any = parsed_any || parsed_field;

        parsed_field = false;
        if (parseBoolValue(tokens[2], parsed.safe)) parsed_field = true;
        else parsed.safe = (tokens[2] == "1" || toUpper(tokens[2]) == "TRUE");
        parsed_any = parsed_any || parsed_field;

        if (tokens.size() >= 4 && parseIntValue(tokens[3], parsed.boost)) parsed_any = true;
        if (tokens.size() >= 5 && parseIntValue(tokens[4], parsed.motor)) parsed_any = true;
        if (tokens.size() >= 6 && parseIntValue(tokens[5], parsed.en)) parsed_any = true;
        if (tokens.size() >= 7 && parseIntValue(tokens[6], parsed.ix)) parsed_any = true;
        if (tokens.size() >= 8 && parseIntValue(tokens[7], parsed.iy)) parsed_any = true;
        if (tokens.size() >= 9 && parseFloatValue(tokens[8], parsed.angle_x)) parsed_any = true;
        if (tokens.size() >= 10 && parseFloatValue(tokens[9], parsed.angle_y)) parsed_any = true;
    }

    if (!parsed_any) {
        if (error) *error = "no STAT field parsed";
        return false;
    }
    parsed.last_seen_ms = nowMs();
    parsed.last_error.clear();
    out = parsed;
    return true;
}

bool GimbalController::parseAngle(const std::string& line, float& angle_x, float& angle_y,
                                 std::string* error) {
    const std::string trimmed = trimCopy(line);
    if (trimmed.empty()) {
        if (error) *error = "empty line";
        return false;
    }

    const std::string upper = toUpper(trimmed);
    if (upper.rfind("ANGLE", 0) != 0) {
        if (error) *error = "not an ANGLE response";
        return false;
    }

    std::string payload = trimCopy(trimmed.substr(5));
    if (!payload.empty() &&
        (payload.front() == ':' || payload.front() == ',' || payload.front() == '=')) {
        payload = trimCopy(payload.substr(1));
    }
    if (payload.empty()) {
        if (error) *error = "empty ANGLE payload";
        return false;
    }

    auto tokens = splitTokens(payload);
    if (tokens.size() < 2) {
        if (error) *error = "ANGLE payload too short";
        return false;
    }

    if (tokens[0].find('=') != std::string::npos || tokens[1].find('=') != std::string::npos) {
        float x = 0.0f;
        float y = 0.0f;
        bool has_x = false;
        bool has_y = false;
        for (const auto& token : tokens) {
            const size_t eq = token.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = toUpper(trimCopy(token.substr(0, eq)));
            const std::string raw_value = trimCopy(token.substr(eq + 1));
            if (key == "X" || key == "ANGLE_X") {
                if (parseFloatValue(raw_value, x)) has_x = true;
            } else if (key == "Y" || key == "ANGLE_Y") {
                if (parseFloatValue(raw_value, y)) has_y = true;
            }
        }
        if (has_x && has_y) {
            angle_x = x;
            angle_y = y;
            if (error) error->clear();
            return true;
        }
    }

    if (!parseFloatValue(tokens[0], angle_x) || !parseFloatValue(tokens[1], angle_y)) {
        if (error) *error = "ANGLE parse failed";
        return false;
    }
    if (error) error->clear();
    return true;
}

void GimbalController::clearFailureState() {
    consecutive_timeouts_ = 0;
    status_.last_error.clear();
}

void GimbalController::recordFailure(const std::string& reason, bool timeout) {
    if (timeout) {
        ++consecutive_timeouts_;
        status_.online = false;
        status_.safe = false;
        status_.last_error = "TIMEOUT";
        if (consecutive_timeouts_ >= 3) {
            status_.last_error = "FAULT";
            consecutive_timeouts_ = 0;
            state_ = PollState::OFFLINE;
            closeDevice();
        }
        return;
    }

    consecutive_timeouts_ = 0;
    status_.online = false;
    status_.safe = false;
    status_.last_error = reason;
}

#ifdef __linux__
bool GimbalController::isDeviceOpen() const {
    return fd_ >= 0;
}

bool GimbalController::initializeSerial() {
    if (!isDeviceOpen()) return false;
    termios tty{};
    if (tcgetattr(fd_, &tty) != 0) return false;

    cfmakeraw(&tty);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= static_cast<tcflag_t>(~(PARENB | PARODD | CSTOPB | CRTSCTS | CSIZE));
    tty.c_cflag |= CS8;
    tty.c_iflag &= static_cast<tcflag_t>(~(IXON | IXOFF | IXANY));
    tty.c_oflag = 0;
    tty.c_lflag = 0;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (cfsetispeed(&tty, B115200) != 0) return false;
    if (cfsetospeed(&tty, B115200) != 0) return false;
    return tcsetattr(fd_, TCSANOW, &tty) == 0;
}

bool GimbalController::openDevice() {
    if (isDeviceOpen()) return true;
    fd_ = open(uart_device_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
        status_.last_error = "open_failed";
        return false;
    }
    if (!initializeSerial()) {
        status_.last_error = "open_failed:serial_init";
        closeDevice();
        return false;
    }
    status_.connected = true;
    status_.last_error.clear();
    return true;
}

void GimbalController::closeDevice() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    status_.connected = false;
}

bool GimbalController::writeCommand(const std::string& cmd) {
    if (!isDeviceOpen()) return false;
    std::string payload = cmd + "\n";
    const ssize_t written = ::write(fd_, payload.c_str(), payload.size());
    return written == static_cast<ssize_t>(payload.size());
}

GimbalController::Response GimbalController::readLine() {
    Response result;
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(read_timeout_ms_);
    std::string line;

    while (std::chrono::steady_clock::now() < deadline) {
        char ch = 0;
        const ssize_t n = ::read(fd_, &ch, 1);
        if (n > 0) {
            if (ch == '\r') {
                continue;
            }
            if (ch == '\n') {
                break;
            }
            line.push_back(ch);
            continue;
        }
        if (n == 0 || (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        status_.last_error = "read_failed";
        return result;
    }

    if (line.empty()) {
        if (std::chrono::steady_clock::now() < deadline) {
            result.empty = true;
            result.ok = true;
            return result;
        }
        result.timeout = true;
        return result;
    }
    result.ok = true;
    result.line = trimCopy(line);
    return result;
}
#endif

bool GimbalController::processPingResponse(const Response& response, std::string& error) {
    const std::string upper = toUpper(response.line);
    if (upper.rfind("ERR", 0) == 0) {
        error = response.line;
        return false;
    }
    if (upper.find("OK") != std::string::npos || upper.find("PONG") != std::string::npos
        || upper == "PING") {
        status_.online = true;
        return true;
    }
    error = "unexpected ping response";
    return false;
}

bool GimbalController::processSafeResponse(const Response& response, std::string& error) {
    const std::string upper = toUpper(response.line);
    if (upper.rfind("ERR", 0) == 0) {
        error = response.line;
        return false;
    }
    if (upper.find("OK") != std::string::npos || upper.find("SAFE") != std::string::npos
        || upper == "READY") {
        status_.safe = true;
        return true;
    }
    error = "unexpected safe response";
    return false;
}

bool GimbalController::processStatResponse(const Response& response, std::string& error) {
    GimbalSnapshot next = status_;
    float angle_x = status_.angle_x;
    float angle_y = status_.angle_y;
    bool parsed_stat = parseStat(response.line, next, &error);
    if (parsed_stat) {
        // The G431 STAT frame does not include connected/online fields in the
        // frozen protocol. A valid STAT response itself proves the UART link.
        next.connected = true;
        next.online = true;
        status_ = next;
        return true;
    }
    if (parseAngle(response.line, angle_x, angle_y, &error)) {
        status_.angle_x = angle_x;
        status_.angle_y = angle_y;
        status_.last_seen_ms = nowMs();
        status_.online = true;
        error.clear();
        return true;
    }
    return false;
}

bool GimbalController::pollOnce() {
#ifdef __linux__
    if (!openDevice()) {
        recordFailure("open_failed", false);
        return false;
    }
#else
    recordFailure("offline_platform", false);
    return false;
#endif

    const std::string cmd = nextCommand();
    if (!writeCommand(cmd)) {
        recordFailure("write_failed", false);
        return false;
    }

    Response response = readLine();
    if (response.timeout) {
        recordFailure("read_timeout", true);
        return false;
    }
    if (response.empty) {
        recordFailure("empty_response", false);
        return false;
    }
    if (!response.ok) {
        recordFailure("read_failed", false);
        return false;
    }

    std::string error;
    bool ok = false;
    if (state_ == PollState::OFFLINE || state_ == PollState::PING) {
        ok = processPingResponse(response, error);
        if (ok) state_ = PollState::STAT;
    } else if (state_ == PollState::SAFE) {
        ok = processSafeResponse(response, error);
        if (ok) state_ = PollState::STAT;
    } else {
        ok = processStatResponse(response, error);
        if (ok) state_ = PollState::SAFE;
    }

    if (!ok) {
        status_.last_error = error.empty() ? "unknown" : error;
        recordFailure(error, false);
        return false;
    }

    clearFailureState();
    status_.last_seen_ms = nowMs();
    status_.last_error.clear();
    return true;
}

}  // namespace edgefocus

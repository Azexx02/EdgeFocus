#include "platform/gimbal/GimbalProtocol.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace edgefocus::gimbal {
namespace {

std::string trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(),
                                        [](unsigned char c) { return std::isspace(c); });
    const auto last = std::find_if_not(value.rbegin(), value.rend(),
                                       [](unsigned char c) { return std::isspace(c); }).base();
    return first < last ? std::string(first, last) : std::string{};
}

std::string upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

std::vector<std::string> tokens(std::string_view input) {
    std::vector<std::string> out;
    std::string current;
    for (const char ch : input) {
        if (ch == ',' || ch == ';' || ch == ' ' || ch == '\t' || ch == ':') {
            if (!current.empty()) {
                out.push_back(trim(std::move(current)));
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) out.push_back(trim(std::move(current)));
    return out;
}

std::optional<bool> parseBool(const std::string& value) {
    const std::string normalized = upper(trim(value));
    if (normalized == "1" || normalized == "TRUE" || normalized == "ON" ||
        normalized == "YES" || normalized == "ACTIVE") {
        return true;
    }
    if (normalized == "0" || normalized == "FALSE" || normalized == "OFF" ||
        normalized == "NO" || normalized == "NONE" || normalized == "INACTIVE") {
        return false;
    }
    return std::nullopt;
}

std::optional<double> parseDouble(const std::string& value) {
    try {
        std::size_t used = 0;
        const double parsed = std::stod(value, &used);
        if (used != value.size() || !std::isfinite(parsed)) return std::nullopt;
        return parsed;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<unsigned> parseUnsigned(const std::string& value) {
    try {
        std::size_t used = 0;
        const auto parsed = std::stoul(value, &used);
        if (used != value.size()) return std::nullopt;
        return static_cast<unsigned>(parsed);
    } catch (...) {
        return std::nullopt;
    }
}

void parseFields(const std::vector<std::string>& parts, std::size_t begin, Event& event) {
    for (std::size_t i = begin; i < parts.size(); ++i) {
        const auto separator = parts[i].find('=');
        if (separator == std::string::npos) continue;
        const auto key = upper(trim(parts[i].substr(0, separator)));
        const auto value = trim(parts[i].substr(separator + 1));
        if (!key.empty()) event.fields[key] = value;
    }

    const auto read_bool = [&](const char* key) -> std::optional<bool> {
        const auto it = event.fields.find(key);
        return it == event.fields.end() ? std::nullopt : parseBool(it->second);
    };
    event.fault_active = read_bool("ACTIVE");
    if (!event.fault_active) event.fault_active = read_bool("FAULT");
    event.enable_in = read_bool("ENABLE_IN");
    if (!event.enable_in) event.enable_in = read_bool("EN");
    event.boot0 = read_bool("BOOT0");

    const auto read_angle = [&](const char* first, const char* second) -> std::optional<double> {
        auto it = event.fields.find(first);
        if (it == event.fields.end()) it = event.fields.find(second);
        return it == event.fields.end() ? std::nullopt : parseDouble(it->second);
    };
    event.yaw_deg = read_angle("Y_DEG", "YAW");
    if (!event.yaw_deg) event.yaw_deg = read_angle("Y", "Y");
    event.pitch_deg = read_angle("P_DEG", "PITCH");
    if (!event.pitch_deg) event.pitch_deg = read_angle("P", "P");
    if (!event.pitch_deg) event.pitch_deg = read_angle("X", "X");

    const auto read_unsigned = [&](const char* key) -> std::optional<unsigned> {
        const auto it = event.fields.find(key);
        return it == event.fields.end() ? std::nullopt : parseUnsigned(it->second);
    };
    event.uart_errors = read_unsigned("UARTERR");
    event.rx_drops = read_unsigned("RXDROP");
    event.tx_drops = read_unsigned("TXDROP");
}

long roundedAngle(double value) {
    return std::lround(value);
}

}  // namespace

LineFramer::LineFramer(std::size_t maximum_line_bytes)
    : maximum_line_bytes_(std::max<std::size_t>(1, maximum_line_bytes)) {}

std::vector<std::string> LineFramer::feed(std::string_view bytes) {
    std::vector<std::string> lines;
    for (const char ch : bytes) {
        if (ch == '\r') continue;
        if (ch == '\n') {
            if (dropping_line_) {
                dropping_line_ = false;
            } else {
                const auto line = trim(std::move(buffer_));
                if (!line.empty()) lines.push_back(line);
            }
            buffer_.clear();
            continue;
        }
        if (dropping_line_) continue;
        if (buffer_.size() >= maximum_line_bytes_) {
            buffer_.clear();
            dropping_line_ = true;
            ++dropped_lines_;
            continue;
        }
        buffer_.push_back(ch);
    }
    return lines;
}

void LineFramer::reset() {
    buffer_.clear();
    dropping_line_ = false;
}

Event parseEvent(std::string_view line) {
    Event event;
    event.raw = trim(std::string(line));
    if (event.raw.empty()) return event;

    const auto parts = tokens(event.raw);
    if (parts.empty()) return event;
    const auto head = upper(parts.front());
    event.subject = parts.size() > 1 ? upper(parts[1]) : std::string{};

    if (head == "BOOT_WAIT_ENABLE") {
        event.kind = EventKind::BootWaitEnable;
    } else if (head == "BOOT_ALIGN" && event.subject == "START") {
        event.kind = EventKind::BootAlignStart;
    } else if (head == "BOOT_ALIGN" && event.subject == "OK") {
        event.kind = EventKind::BootAlignOk;
    } else if (head == "OK") {
        event.kind = EventKind::Ok;
    } else if (head == "ERR" || head == "ERROR") {
        event.kind = EventKind::Error;
    } else if (head == "PING" || head == "PONG" ||
               (head == "OK" && event.subject == "PING")) {
        event.kind = EventKind::Ping;
    } else if (head == "VER" || head == "VERSION") {
        event.kind = EventKind::Version;
    } else if (head == "STAT" || head == "STATUS") {
        event.kind = EventKind::Status;
    } else if (head == "FAULT") {
        event.kind = EventKind::FaultStatus;
    } else if (head == "ANGLE") {
        event.kind = EventKind::Angle;
    } else if (head == "SET" && event.subject == "YP_OK") {
        event.kind = EventKind::SetAck;
    } else if (head == "TRACK" && event.subject == "YP_OK") {
        event.kind = EventKind::TrackAck;
    } else if (head == "DEG") {
        event.kind = EventKind::DegreeAck;
    }

    parseFields(parts, 1, event);
    if (event.kind == EventKind::Angle && !event.yaw_deg && !event.pitch_deg) {
        if (parts.size() >= 3) {
            event.yaw_deg = parseDouble(parts[1]);
            event.pitch_deg = parseDouble(parts[2]);
        }
    }
    return event;
}

bool isHealthyPingEvent(const Event& event) noexcept {
    return event.kind == EventKind::Ping ||
           (event.kind == EventKind::Ok &&
            (event.subject == "PING" || event.subject == "G431"));
}

bool isHealthyFaultEvent(const Event& event) noexcept {
    return event.kind == EventKind::FaultStatus && event.fault_active.has_value() &&
           !*event.fault_active;
}

bool isHealthyStatusEvent(const Event& event) noexcept {
    if (event.kind != EventKind::Status) return false;
    return !event.fault_active.value_or(false);
}

std::string diagnosticCommand(EventKind expected) {
    switch (expected) {
        case EventKind::Ping: return "PING";
        case EventKind::Version: return "VER";
        case EventKind::Status: return "STAT";
        case EventKind::FaultStatus: return "FAULT";
        case EventKind::Angle: return "ANGLE";
        default: return {};
    }
}

std::string stopCommand() { return "STOP"; }
std::string armCommand() { return "ARM"; }
std::string holdCommand() { return "HOLD 1"; }

std::string moveCommand(double yaw_deg, double pitch_deg) {
    return "SET YP " + std::to_string(roundedAngle(yaw_deg)) + " " +
           std::to_string(roundedAngle(pitch_deg));
}

std::string trackCommand(double yaw_deg, double pitch_deg) {
    return "TRACK YP " + std::to_string(roundedAngle(yaw_deg * 10.0)) + " " +
           std::to_string(roundedAngle(pitch_deg * 10.0));
}

double normalizeAngle(double angle_deg) noexcept {
    double normalized = std::fmod(angle_deg, 360.0);
    if (normalized < 0.0) normalized += 360.0;
    return normalized;
}

double shortestAngleDelta(double from_deg, double to_deg) noexcept {
    double delta = normalizeAngle(to_deg) - normalizeAngle(from_deg);
    if (delta > 180.0) delta -= 360.0;
    if (delta < -180.0) delta += 360.0;
    return delta;
}

double offsetAngle(double reference_deg, double offset_deg) noexcept {
    return normalizeAngle(reference_deg + offset_deg);
}

const char* stateName(State state) noexcept {
    switch (state) {
        case State::Disabled: return "DISABLED";
        case State::LinkCheck: return "LINK_CHECK";
        case State::Aligning: return "ALIGNING";
        case State::Ready: return "READY";
        case State::Moving: return "MOVING";
        case State::Holding: return "HOLDING";
        case State::Stopping: return "STOPPING";
        case State::Fault: return "FAULT";
        case State::RecoveryCheck: return "RECOVERY_CHECK";
    }
    return "DISABLED";
}

const char* controlModeName(ControlMode mode) noexcept {
    switch (mode) {
        case ControlMode::UartOnly: return "uart_only";
        case ControlMode::UartGpioInterlock: return "uart_gpio_interlock";
    }
    return "uart_only";
}

}  // namespace edgefocus::gimbal

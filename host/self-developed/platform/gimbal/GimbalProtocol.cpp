#include "platform/gimbal/GimbalProtocol.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <set>

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

std::vector<std::string> split(std::string_view input, char delimiter) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= input.size()) {
        const auto end = input.find(delimiter, start);
        const auto count = end == std::string_view::npos ? input.size() - start : end - start;
        out.push_back(trim(std::string(input.substr(start, count))));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return out;
}

std::optional<bool> parseBool(const std::string& value) {
    const auto normalized = upper(trim(value));
    if (normalized == "1" || normalized == "TRUE" || normalized == "ON" ||
        normalized == "YES" || normalized == "ACTIVE") return true;
    if (normalized == "0" || normalized == "FALSE" || normalized == "OFF" ||
        normalized == "NO" || normalized == "NONE" || normalized == "INACTIVE") return false;
    return std::nullopt;
}

std::optional<double> parseDouble(const std::string& value) {
    try {
        std::size_t used = 0;
        const auto parsed = std::stod(value, &used);
        if (used != value.size() || !std::isfinite(parsed)) return std::nullopt;
        return parsed;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::uint32_t> parseUint32(const std::string& value) {
    try {
        std::size_t used = 0;
        const auto parsed = std::stoull(value, &used);
        if (used != value.size() || parsed > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(parsed);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<State> parseState(const std::string& value) {
    const auto text = upper(value);
    if (text == "DISABLED") return State::Disabled;
    if (text == "ARMING") return State::Aligning;
    if (text == "READY") return State::Ready;
    if (text == "HOLDING") return State::Holding;
    if (text == "MOVING") return State::Moving;
    if (text == "CALIBRATING") return State::Calibrating;
    if (text == "FAULT") return State::Fault;
    return std::nullopt;
}

ArmProfile parseArmProfile(const std::string& value) {
    const auto text = upper(value);
    if (text == "PRODUCTION") return ArmProfile::Production;
    if (text == "MAINTENANCE") return ArmProfile::Maintenance;
    return ArmProfile::None;
}

ErrorClass parseErrorClass(const std::string& value) {
    const auto text = upper(value);
    if (text == "COMMAND") return ErrorClass::Command;
    if (text == "TRANSIENT") return ErrorClass::Transient;
    if (text == "SAFETY") return ErrorClass::Safety;
    if (text == "FAULT") return ErrorClass::Fault;
    if (text == "LINK") return ErrorClass::Link;
    if (text == "TIMEOUT") return ErrorClass::Timeout;
    if (text == "BUSINESS") return ErrorClass::Business;
    return text.empty() ? ErrorClass::None : ErrorClass::Unknown;
}

std::optional<std::string> field(const Event& event, const char* key) {
    const auto it = event.fields.find(key);
    if (it == event.fields.end()) return std::nullopt;
    return it->second;
}

void parseFields(const std::vector<std::string>& parts, std::size_t begin, Event& event) {
    for (std::size_t i = begin; i < parts.size(); ++i) {
        const auto separator = parts[i].find('=');
        if (separator == std::string::npos) continue;
        const auto key = upper(trim(parts[i].substr(0, separator)));
        const auto value = trim(parts[i].substr(separator + 1));
        if (!key.empty()) event.fields[key] = value;
    }

    if (const auto value = field(event, "PROTO")) event.protocol_version = *value;
    if (const auto value = field(event, "CAPS")) {
        for (auto capability : split(*value, '|')) {
            capability = upper(std::move(capability));
            if (!capability.empty()) event.capabilities.push_back(std::move(capability));
        }
    }
    if (const auto value = field(event, "STATE")) event.device_state = parseState(*value);
    if (const auto value = field(event, "ARM_PROFILE")) {
        event.arm_profile = parseArmProfile(*value);
    }
    if (const auto value = field(event, "CLASS")) event.error_class = parseErrorClass(*value);
    if (const auto value = field(event, "CODE")) event.error_code = upper(*value);
    if (const auto value = field(event, "ACTIVE")) event.fault_active = parseBool(*value);
    if (!event.fault_active) {
        if (const auto value = field(event, "FAULT")) event.fault_active = parseBool(*value);
    }
    if (const auto value = field(event, "ENABLE_IN")) event.enable_in = parseBool(*value);
    if (!event.enable_in) {
        if (const auto value = field(event, "EN")) event.enable_in = parseBool(*value);
    }
    if (const auto value = field(event, "BOOT0")) event.boot0 = parseBool(*value);

    const auto read_double = [&](const char* key) -> std::optional<double> {
        const auto value = field(event, key);
        return value ? parseDouble(*value) : std::nullopt;
    };
    const auto read_u32 = [&](const char* key) -> std::optional<std::uint32_t> {
        const auto value = field(event, key);
        return value ? parseUint32(*value) : std::nullopt;
    };
    const auto read_bool = [&](const char* key) -> std::optional<bool> {
        const auto value = field(event, key);
        return value ? parseBool(*value) : std::nullopt;
    };

    if (const auto value = read_u32("YAW10")) event.yaw_deg = *value / 10.0;
    if (const auto value = read_u32("PITCH10")) event.pitch_deg = *value / 10.0;
    if (const auto value = read_u32("TYAW10")) event.target_yaw_deg = *value / 10.0;
    if (const auto value = read_u32("TPITCH10")) event.target_pitch_deg = *value / 10.0;
    if (!event.yaw_deg) event.yaw_deg = read_double("Y_DEG");
    if (!event.yaw_deg) event.yaw_deg = read_double("YAW");
    if (!event.yaw_deg) event.yaw_deg = read_double("Y");
    if (!event.pitch_deg) event.pitch_deg = read_double("P_DEG");
    if (!event.pitch_deg) event.pitch_deg = read_double("PITCH");
    if (!event.pitch_deg) event.pitch_deg = read_double("P");
    if (!event.pitch_deg) event.pitch_deg = read_double("X");

    event.motion_generation = read_u32("GEN");
    event.settled = read_bool("SETTLED");
    event.calibration_epoch = read_u32("CAL_EPOCH");
    event.origin_epoch = read_u32("ORIGIN_EPOCH");
    event.calibration_valid = read_bool("CAL_VALID");
    event.origin_valid = read_bool("ORIGIN_VALID");
    event.sample_age_ms = read_u32("SAMPLE_AGE_MS");
    if (const auto value = field(event, "CURRENT_PROTECTION")) {
        event.current_protection_state = upper(*value);
    }
    if (const auto value = field(event, "OP")) event.operation = upper(*value);
    event.operation_generation = read_u32("OP_GEN");
    if (const auto value = read_u32("OP_PROGRESS")) {
        event.operation_progress = static_cast<unsigned>(*value);
    }
    if (const auto value = field(event, "LAST_OP_STATUS")) {
        event.last_operation_status = upper(*value);
    }
    if (const auto value = read_u32("UARTERR")) event.uart_errors = *value;
    if (const auto value = read_u32("ORE")) event.uart_overruns = *value;
    if (const auto value = read_u32("FE")) event.uart_framing_errors = *value;
    if (const auto value = read_u32("NE")) event.uart_noise_errors = *value;
    if (const auto value = read_u32("PE")) event.uart_parity_errors = *value;
    if (const auto value = read_u32("RXREARM")) event.uart_rx_rearm_failures = *value;
    if (const auto value = read_u32("RXDROP")) event.rx_drops = *value;
    if (const auto value = read_u32("TXDROP")) event.tx_drops = *value;
    event.pitch_current_raw = read_u32("P_RAW");
    event.pitch_current_average = read_u32("P_AVG");
    event.pitch_current_peak = read_u32("P_PEAK");
    event.yaw_current_raw = read_u32("Y_RAW");
    event.yaw_current_average = read_u32("Y_AVG");
    event.yaw_current_peak = read_u32("Y_PEAK");
    event.current_window_ms = read_u32("WINDOW_MS");
    event.current_samples = read_u32("SAMPLES");
    event.current_valid = read_bool("VALID");
    event.current_saturated = read_bool("SATURATED");
    event.maintenance_assist_attempted = read_bool("ATTEMPTED");
    event.maintenance_assist_exhausted = read_bool("EXHAUSTED");
    event.maintenance_assist_active_ms = read_u32("ACTIVE_MS");
    event.maintenance_assist_peak_floor = read_u32("PEAK_FLOOR");
}

long ringTenths(double value) {
    auto tenths = std::lround(normalizeAngle(value) * 10.0);
    if (tenths >= 3600) tenths -= 3600;
    return tenths;
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
    const auto parts = split(event.raw, ',');
    if (parts.empty()) return event;
    const auto head = upper(parts.front());
    event.subject = parts.size() > 1 ? upper(parts[1]) : std::string{};
    parseFields(parts, 1, event);

    if (head == "BOOT_WAIT_ENABLE") event.kind = EventKind::BootWaitEnable;
    else if (head == "BOOT_ALIGN" && event.subject == "START") event.kind = EventKind::BootAlignStart;
    else if (head == "BOOT_ALIGN" && event.subject == "OK") event.kind = EventKind::BootAlignOk;
    else if (head == "ERR" || head == "ERROR") event.kind = EventKind::Error;
    else if (head == "VER" || head == "VERSION") event.kind = EventKind::Version;
    else if (head == "POSE") event.kind = EventKind::Pose;
    else if (head == "CURRENT") event.kind = EventKind::Current;
    else if (head == "MAINTASSIST") event.kind = EventKind::MaintenanceAssist;
    else if (head == "NVM") event.kind = EventKind::Nvm;
    else if (head == "FAULT") event.kind = EventKind::FaultStatus;
    else if (head == "STAT" || head == "STATUS") event.kind = EventKind::Status;
    else if (head == "ANGLE") event.kind = EventKind::Angle;
    else if (head == "TRACK" && event.subject == "ACCEPTED") event.kind = EventKind::MotionAccepted;
    else if (head == "CAL" && event.subject == "ACCEPTED") event.kind = EventKind::CalibrationAccepted;
    else if (head == "TRACK" && event.subject == "YP_OK") event.kind = EventKind::TrackAck;
    else if (head == "SET" && event.subject == "YP_OK") event.kind = EventKind::SetAck;
    else if (head == "DEG") event.kind = EventKind::DegreeAck;
    else if (head == "PING" || head == "PONG") event.kind = EventKind::Ping;
    else if (head == "OK") {
        if (event.subject == "HELLO") event.kind = EventKind::HelloAck;
        else if (event.subject == "ARM") event.kind = EventKind::ArmAck;
        else if (event.subject == "HOLD_NOW") event.kind = EventKind::HoldAck;
        else event.kind = EventKind::Ok;
    }

    if (event.kind == EventKind::Angle && !event.yaw_deg && !event.pitch_deg &&
        parts.size() >= 3) {
        event.yaw_deg = parseDouble(parts[1]);
        event.pitch_deg = parseDouble(parts[2]);
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
    return (event.kind == EventKind::Status || event.kind == EventKind::Pose) &&
           !event.fault_active.value_or(false) &&
           event.device_state.value_or(State::Disabled) != State::Fault;
}

bool hasRequiredProtocol3Capabilities(const Event& event) noexcept {
    if (event.kind != EventKind::Version || event.protocol_version != "3.0") return false;
    const std::set<std::string> available(event.capabilities.begin(), event.capabilities.end());
    static const char* required[] = {
        "ASYNC_MOTION", "POSE", "ORIGIN", "FAULT_CLASS", "HOLD_NOW",
        "NVM_V3", "FULL_RANGE_POSITION", "NEGOTIATION_REQUIRED",
    };
    return std::all_of(std::begin(required), std::end(required),
                       [&](const char* value) { return available.count(value) != 0; });
}

std::string diagnosticCommand(EventKind expected) {
    switch (expected) {
        case EventKind::Ping: return "PING";
        case EventKind::Version: return "VER";
        case EventKind::Status:
        case EventKind::Pose: return "POSE";
        case EventKind::FaultStatus: return "FAULT";
        case EventKind::Angle: return "POSE";
        case EventKind::Current: return "CURRENT";
        case EventKind::Nvm: return "NVM STATUS";
        default: return {};
    }
}

std::string helloCommand() { return "HELLO HOST PROTO=3.0"; }
std::string armCommand(bool maintenance) { return maintenance ? "ARM MAINTENANCE" : "ARM"; }
std::string stopCommand() { return "STOP"; }
std::string holdCommand() { return "HOLD NOW"; }
std::string moveCommand(double yaw_deg, double pitch_deg) {
    return trackCommand(yaw_deg, pitch_deg);
}
std::string trackCommand(double yaw_deg, double pitch_deg) {
    return "TRACK YP " + std::to_string(ringTenths(yaw_deg)) + " " +
           std::to_string(ringTenths(pitch_deg));
}
std::string poseCommand() { return "POSE"; }
std::string maintenanceAssistCommand() { return "MAINTASSIST?"; }

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
        case State::Aligning: return "ARMING";
        case State::Ready: return "READY";
        case State::Moving: return "MOVING";
        case State::Holding: return "HOLDING";
        case State::Calibrating: return "CALIBRATING";
        case State::Stopping: return "STOPPING";
        case State::Fault: return "FAULT";
        case State::RecoveryCheck: return "RECOVERY_CHECK";
    }
    return "DISABLED";
}

const char* controlModeName(ControlMode mode) noexcept {
    return mode == ControlMode::UartGpioInterlock ? "uart_gpio_interlock" : "uart_only";
}

const char* errorClassName(ErrorClass value) noexcept {
    switch (value) {
        case ErrorClass::None: return "NONE";
        case ErrorClass::Command: return "COMMAND";
        case ErrorClass::Transient: return "TRANSIENT";
        case ErrorClass::Safety: return "SAFETY";
        case ErrorClass::Fault: return "FAULT";
        case ErrorClass::Link: return "LINK";
        case ErrorClass::Timeout: return "TIMEOUT";
        case ErrorClass::Business: return "BUSINESS";
        case ErrorClass::Unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}

}  // namespace edgefocus::gimbal

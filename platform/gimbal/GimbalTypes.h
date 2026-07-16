#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace edgefocus::gimbal {

enum class ControlMode : std::uint8_t {
    UartOnly,
    UartGpioInterlock,
};

enum class State : std::uint8_t {
    Disabled,
    LinkCheck,
    Aligning,
    Ready,
    Moving,
    Holding,
    Stopping,
    Fault,
    RecoveryCheck,
};

enum class EventKind : std::uint8_t {
    Unknown,
    BootWaitEnable,
    BootAlignStart,
    BootAlignOk,
    Ok,
    Error,
    Ping,
    Version,
    Status,
    FaultStatus,
    Angle,
    SetAck,
    TrackAck,
    DegreeAck,
};

struct Event {
    EventKind kind{EventKind::Unknown};
    std::string raw;
    std::string subject;
    std::unordered_map<std::string, std::string> fields;
    std::optional<bool> fault_active;
    std::optional<bool> enable_in;
    std::optional<bool> boot0;
    std::optional<double> yaw_deg;
    std::optional<double> pitch_deg;
    std::optional<unsigned> uart_errors;
    std::optional<unsigned> rx_drops;
    std::optional<unsigned> tx_drops;
};

struct Status {
    State state{State::Disabled};
    bool fault_active{};
    bool recoverable{};
    bool link_healthy{};
    std::string fault_latched_reason;
    std::optional<double> yaw_deg;
    std::optional<double> pitch_deg;
    std::int64_t state_since_ms{};
    std::int64_t last_event_ms{};
    std::int64_t last_recovery_probe_ms{};
    unsigned recovery_healthy_count{};
    std::uint64_t generation{};
};

struct CommandResult {
    bool success{};
    bool timeout{};
    std::string error;
    std::vector<std::string> transcript;
    std::vector<std::string> failed_steps;
};

const char* stateName(State state) noexcept;
const char* controlModeName(ControlMode mode) noexcept;

}  // namespace edgefocus::gimbal

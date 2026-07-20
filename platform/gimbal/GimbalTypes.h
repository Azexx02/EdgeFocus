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

enum class ProtocolCompatibility : std::uint8_t {
    Unknown,
    Protocol2DiagnosticOnly,
    Protocol3Control,
};

enum class LinkHealth : std::uint8_t {
    Unavailable,
    Negotiating,
    Healthy,
    Degraded,
};

enum class State : std::uint8_t {
    Disabled,
    LinkCheck,
    Aligning,
    Ready,
    Moving,
    Holding,
    Calibrating,
    Stopping,
    Fault,
    RecoveryCheck,
};

enum class ArmProfile : std::uint8_t {
    None,
    Production,
    Maintenance,
};

enum class ErrorClass : std::uint8_t {
    None,
    Command,
    Transient,
    Safety,
    Fault,
    Link,
    Timeout,
    Business,
    Unknown,
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
    HelloAck,
    ArmAck,
    HoldAck,
    Status,
    FaultStatus,
    Angle,
    Pose,
    Current,
    Nvm,
    MotionAccepted,
    CalibrationAccepted,
    SetAck,
    TrackAck,
    DegreeAck,
};

struct Event {
    EventKind kind{EventKind::Unknown};
    std::string raw;
    std::string subject;
    std::unordered_map<std::string, std::string> fields;
    std::string protocol_version;
    std::vector<std::string> capabilities;
    std::optional<State> device_state;
    std::optional<ArmProfile> arm_profile;
    ErrorClass error_class{ErrorClass::None};
    std::string error_code;
    std::optional<bool> fault_active;
    std::optional<bool> enable_in;
    std::optional<bool> boot0;
    std::optional<double> yaw_deg;
    std::optional<double> pitch_deg;
    std::optional<double> target_yaw_deg;
    std::optional<double> target_pitch_deg;
    std::optional<std::uint32_t> motion_generation;
    std::optional<bool> settled;
    std::optional<std::uint32_t> calibration_epoch;
    std::optional<std::uint32_t> origin_epoch;
    std::optional<bool> calibration_valid;
    std::optional<bool> origin_valid;
    std::string current_protection_state;
    std::optional<std::uint32_t> sample_age_ms;
    std::string operation;
    std::optional<std::uint32_t> operation_generation;
    std::optional<unsigned> operation_progress;
    std::string last_operation_status;
    std::optional<unsigned> uart_errors;
    std::optional<unsigned> rx_drops;
    std::optional<unsigned> tx_drops;
};

struct GimbalSnapshot {
    std::string protocol_version;
    std::vector<std::string> capabilities;
    ProtocolCompatibility compatibility{ProtocolCompatibility::Unknown};
    bool available{};
    std::string degraded_reason;
    LinkHealth link_health{LinkHealth::Unavailable};
    ControlMode control_mode{ControlMode::UartOnly};
    State state{State::Disabled};
    ArmProfile arm_profile{ArmProfile::None};
    std::optional<std::int32_t> current_yaw10;
    std::optional<std::int32_t> current_pitch10;
    std::optional<std::int32_t> target_yaw10;
    std::optional<std::int32_t> target_pitch10;
    std::optional<std::uint32_t> motion_generation;
    bool settled{};
    bool calibration_valid{};
    std::uint32_t calibration_epoch{};
    bool origin_valid{};
    std::uint32_t origin_epoch{};
    ErrorClass fault_class{ErrorClass::None};
    std::string fault_code;
    std::int64_t last_pose_at_ms{};
    std::int64_t last_pose_age_ms{-1};
    std::uint64_t last_queued_sequence{};
    std::uint64_t acked_through_sequence{};
    std::string current_protection_state;
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
    GimbalSnapshot snapshot;
};

struct CommandResult {
    bool success{};
    bool timeout{};
    std::string error;
    std::vector<std::string> transcript;
    std::vector<std::string> failed_steps;
    bool queued{};
    bool firmware_accepted{};
    bool settled{};
    std::uint64_t request_sequence{};
    std::optional<std::uint32_t> motion_generation;
    ErrorClass error_class{ErrorClass::None};
    std::string error_code;
};

const char* stateName(State state) noexcept;
const char* controlModeName(ControlMode mode) noexcept;
const char* errorClassName(ErrorClass value) noexcept;

}  // namespace edgefocus::gimbal

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace edgefocus::fan {

enum class ControlState : std::uint8_t {
    BootSafe = 0,
    Controlled = 1,
    FailSafe = 2,
};

enum class CoolingMode : std::uint8_t {
    Normal = 0,
    BoostRamp = 1,
    BoostHold = 2,
    BoostCritical = 3,
};

enum class Fault : std::uint32_t {
    None = 0,
    TempNotReceived = 1u << 0,
    TempStale = 1u << 1,
    TempRangeInvalid = 1u << 2,
    ProtocolIncompatible = 1u << 3,
    RpmsgDisconnected = 1u << 4,
    TachUnavailable = 1u << 5,
    FanStall = 1u << 6,
    ControlDeadlineMissed = 1u << 7,
    PwmWriteFailed = 1u << 8,
};

constexpr Fault operator|(Fault lhs, Fault rhs) {
    return static_cast<Fault>(static_cast<std::uint32_t>(lhs) |
                              static_cast<std::uint32_t>(rhs));
}

constexpr Fault operator&(Fault lhs, Fault rhs) {
    return static_cast<Fault>(static_cast<std::uint32_t>(lhs) &
                              static_cast<std::uint32_t>(rhs));
}

constexpr Fault operator~(Fault value) {
    return static_cast<Fault>(~static_cast<std::uint32_t>(value));
}

inline Fault& operator|=(Fault& lhs, Fault rhs) {
    lhs = lhs | rhs;
    return lhs;
}

inline Fault& operator&=(Fault& lhs, Fault rhs) {
    lhs = lhs & rhs;
    return lhs;
}

constexpr bool hasFault(Fault value, Fault flag) {
    return (value & flag) != Fault::None;
}

struct FanConfig {
    std::uint32_t pwm_period_ns{40000};
    std::uint32_t safe_duty_ns{8000};
    std::array<std::int32_t, 5> curve_threshold_mc{{-40000, 45000, 55000, 65000, 75000}};
    std::array<std::uint32_t, 5> curve_duty_ns{{34000, 30000, 22000, 14000, 8000}};
    std::int32_t curve_fall_hysteresis_mc{2000};

    std::int32_t min_temperature_mc{-40000};
    std::int32_t max_temperature_mc{150000};
    std::uint32_t temperature_timeout_ms{3000};
    std::uint8_t recovery_samples{2};

    std::int32_t boost_enter_mc{70000};
    std::uint8_t boost_enter_samples{3};
    std::int32_t boost_exit_mc{62000};
    std::uint32_t boost_exit_hold_ms{10000};
    std::int32_t critical_mc{80000};
    std::uint32_t ramp_interval_ms{5000};
    std::uint32_t ramp_step_ns{2000};
    std::uint32_t trend_window_ms{10000};
    std::int32_t cooling_trend_mc{-500};
    std::int32_t reheating_trend_mc{300};
    std::uint8_t reheating_windows{2};

    bool tach_enabled{false};
    std::uint8_t tach_pulses_per_revolution{2};
    std::uint32_t stall_duty_threshold_ns{14000};
    std::uint8_t stall_windows{3};
};

struct FanOutput {
    ControlState state{ControlState::BootSafe};
    CoolingMode mode{CoolingMode::Normal};
    std::uint32_t duty_ns{8000};
    std::uint8_t curve_band{0};
    std::int32_t filtered_temperature_mc{0};
    std::int32_t trend_mc{0};
    bool trend_valid{false};
    std::uint32_t rpm{0};
    std::uint32_t sample_age_ms{0};
    Fault current_faults{Fault::TempNotReceived};
    Fault latched_faults{Fault::TempNotReceived};
};

class FanControlCore {
public:
    explicit FanControlCore(FanConfig config = {});

    // Returns false for invalid, duplicate, or out-of-order samples. Rejected
    // samples never refresh the temperature TTL.
    bool acceptTemperature(std::uint32_t sequence,
                           std::int32_t temperature_mc,
                           std::uint32_t received_at_ms);
    // Starts a new producer sequence domain (for example after Linux bridge
    // boot_id changes) and immediately returns to BOOT_SAFE.
    void resetTemperatureSource();

    // Tach windows are supplied by the non-blocking hardware adapter.
    bool acceptTachWindow(std::uint32_t pulse_count,
                          std::uint32_t window_ms,
                          std::uint32_t received_at_ms);

    void setExternalFault(Fault fault, bool active);
    void clearLatchedFaults();

    // Must be called periodically by the owning control task.
    FanOutput update(std::uint32_t now_ms);
    const FanOutput& output() const { return output_; }

private:
    struct TrendSample {
        std::uint32_t time_ms{0};
        std::int32_t temperature_mc{0};
    };

    static constexpr std::size_t kHistoryCapacity = 24;

    bool sequenceIsNewer(std::uint32_t sequence) const;
    void pushRawTemperature(std::int32_t temperature_mc);
    void pushTrendSample(std::uint32_t time_ms, std::int32_t temperature_mc);
    std::int32_t filteredFromRaw() const;
    bool calculateTrend(std::uint32_t now_ms, std::int32_t& trend_mc) const;
    void updateNormalBand(std::int32_t temperature_mc);
    void processControlled(std::uint32_t now_ms);
    void processBoost(std::uint32_t now_ms);
    void enterFailSafe(Fault cause);
    void enterControlled(std::uint32_t now_ms);
    void applyNormalTarget(std::uint32_t now_ms);
    bool safetyFaultActive() const;

    FanConfig config_;
    FanOutput output_;

    bool has_sequence_{false};
    std::uint32_t last_sequence_{0};
    std::uint32_t last_temperature_ms_{0};
    std::uint8_t valid_recovery_count_{0};
    std::uint8_t boost_enter_count_{0};

    std::array<std::int32_t, 3> raw_temperatures_{};
    std::size_t raw_temperature_count_{0};
    std::size_t raw_temperature_next_{0};

    std::array<TrendSample, kHistoryCapacity> trend_history_{};
    std::size_t trend_history_count_{0};
    std::size_t trend_history_next_{0};

    std::uint32_t last_processed_sequence_{0};
    std::uint32_t last_trend_sequence_{0};
    std::uint32_t last_ramp_ms_{0};
    std::uint32_t boost_exit_since_ms_{0};
    std::uint8_t reheating_count_{0};
    bool boost_exit_timer_active_{false};
    bool normal_slew_active_{false};

    std::uint8_t zero_rpm_windows_{0};
};

}  // namespace edgefocus::fan

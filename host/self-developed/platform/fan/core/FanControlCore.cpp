#include "FanControlCore.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace edgefocus::fan {
namespace {

constexpr Fault kSafetyFaults = Fault::TempNotReceived |
                                Fault::TempStale |
                                Fault::TempRangeInvalid |
                                Fault::ProtocolIncompatible |
                                Fault::RpmsgDisconnected |
                                Fault::FanStall |
                                Fault::ControlDeadlineMissed |
                                Fault::PwmWriteFailed;

std::uint32_t elapsed(std::uint32_t now, std::uint32_t then) {
    return now - then;
}

}  // namespace

FanControlCore::FanControlCore(FanConfig config) : config_(std::move(config)) {
    output_.duty_ns = config_.safe_duty_ns;
    output_.current_faults = Fault::TempNotReceived;
    output_.latched_faults = Fault::TempNotReceived;
    if (!config_.tach_enabled) {
        output_.current_faults |= Fault::TachUnavailable;
        output_.latched_faults |= Fault::TachUnavailable;
    }
}

bool FanControlCore::sequenceIsNewer(std::uint32_t sequence) const {
    if (!has_sequence_) {
        return true;
    }
    return static_cast<std::int32_t>(sequence - last_sequence_) > 0;
}

bool FanControlCore::acceptTemperature(std::uint32_t sequence,
                                       std::int32_t temperature_mc,
                                       std::uint32_t received_at_ms) {
    if (!sequenceIsNewer(sequence)) {
        return false;
    }
    has_sequence_ = true;
    last_sequence_ = sequence;

    if (temperature_mc < config_.min_temperature_mc ||
        temperature_mc > config_.max_temperature_mc) {
        output_.current_faults |= Fault::TempRangeInvalid;
        output_.latched_faults |= Fault::TempRangeInvalid;
        valid_recovery_count_ = 0;
        return false;
    }

    last_temperature_ms_ = received_at_ms;
    valid_recovery_count_ = static_cast<std::uint8_t>(
        std::min<unsigned>(config_.recovery_samples,
                           static_cast<unsigned>(valid_recovery_count_) + 1u));

    pushRawTemperature(temperature_mc);
    output_.filtered_temperature_mc = filteredFromRaw();
    pushTrendSample(received_at_ms, output_.filtered_temperature_mc);
    return true;
}

void FanControlCore::resetTemperatureSource() {
    has_sequence_ = false;
    last_sequence_ = 0;
    last_temperature_ms_ = 0;
    valid_recovery_count_ = 0;
    boost_enter_count_ = 0;
    raw_temperature_count_ = 0;
    raw_temperature_next_ = 0;
    trend_history_count_ = 0;
    trend_history_next_ = 0;
    last_processed_sequence_ = 0;
    last_trend_sequence_ = 0;
    boost_exit_timer_active_ = false;
    normal_slew_active_ = false;
    reheating_count_ = 0;
    output_.state = ControlState::BootSafe;
    output_.mode = CoolingMode::Normal;
    output_.duty_ns = config_.safe_duty_ns;
    output_.curve_band = 0;
    output_.filtered_temperature_mc = 0;
    output_.trend_mc = 0;
    output_.trend_valid = false;
    output_.sample_age_ms = 0;
    output_.current_faults &= ~(Fault::TempStale | Fault::TempRangeInvalid);
    output_.current_faults |= Fault::TempNotReceived;
    output_.latched_faults |= Fault::TempNotReceived;
}

bool FanControlCore::acceptTachWindow(std::uint32_t pulse_count,
                                      std::uint32_t window_ms,
                                      std::uint32_t /*received_at_ms*/) {
    if (!config_.tach_enabled || window_ms == 0 ||
        config_.tach_pulses_per_revolution == 0) {
        return false;
    }

    output_.current_faults &= ~Fault::TachUnavailable;
    const std::uint64_t numerator = static_cast<std::uint64_t>(pulse_count) * 60000u;
    const std::uint64_t denominator =
        static_cast<std::uint64_t>(window_ms) * config_.tach_pulses_per_revolution;
    output_.rpm = static_cast<std::uint32_t>(numerator / denominator);

    if (output_.duty_ns <= config_.stall_duty_threshold_ns && pulse_count == 0) {
        zero_rpm_windows_ = static_cast<std::uint8_t>(std::min<unsigned>(
            config_.stall_windows, static_cast<unsigned>(zero_rpm_windows_) + 1u));
        if (zero_rpm_windows_ >= config_.stall_windows) {
            output_.current_faults |= Fault::FanStall;
            output_.latched_faults |= Fault::FanStall;
        }
    } else {
        zero_rpm_windows_ = 0;
        output_.current_faults &= ~Fault::FanStall;
    }
    return true;
}

void FanControlCore::setExternalFault(Fault fault, bool active) {
    if (active) {
        output_.current_faults |= fault;
        output_.latched_faults |= fault;
    } else {
        output_.current_faults &= ~fault;
    }
}

void FanControlCore::clearLatchedFaults() {
    output_.latched_faults = output_.current_faults;
}

void FanControlCore::pushRawTemperature(std::int32_t temperature_mc) {
    raw_temperatures_[raw_temperature_next_] = temperature_mc;
    raw_temperature_next_ = (raw_temperature_next_ + 1u) % raw_temperatures_.size();
    raw_temperature_count_ = std::min(raw_temperature_count_ + 1u,
                                      raw_temperatures_.size());
}

std::int32_t FanControlCore::filteredFromRaw() const {
    if (raw_temperature_count_ == 0) {
        return 0;
    }
    if (raw_temperature_count_ == 1) {
        return raw_temperatures_[0];
    }
    if (raw_temperature_count_ == 2) {
        const std::int64_t sum = static_cast<std::int64_t>(raw_temperatures_[0]) +
                                 raw_temperatures_[1];
        return static_cast<std::int32_t>(sum / 2);
    }
    std::array<std::int32_t, 3> sorted = raw_temperatures_;
    std::sort(sorted.begin(), sorted.end());
    return sorted[1];
}

void FanControlCore::pushTrendSample(std::uint32_t time_ms,
                                     std::int32_t temperature_mc) {
    trend_history_[trend_history_next_] = {time_ms, temperature_mc};
    trend_history_next_ = (trend_history_next_ + 1u) % trend_history_.size();
    trend_history_count_ = std::min(trend_history_count_ + 1u,
                                    trend_history_.size());
}

bool FanControlCore::calculateTrend(std::uint32_t now_ms,
                                    std::int32_t& trend_mc) const {
    if (trend_history_count_ < 2) {
        return false;
    }

    const std::size_t oldest =
        (trend_history_next_ + trend_history_.size() - trend_history_count_) %
        trend_history_.size();
    bool found = false;
    TrendSample reference{};
    for (std::size_t i = 0; i < trend_history_count_; ++i) {
        const TrendSample& sample = trend_history_[(oldest + i) % trend_history_.size()];
        if (elapsed(now_ms, sample.time_ms) >= config_.trend_window_ms) {
            reference = sample;
            found = true;
        } else {
            break;
        }
    }
    if (!found) {
        return false;
    }

    trend_mc = output_.filtered_temperature_mc - reference.temperature_mc;
    return true;
}

void FanControlCore::updateNormalBand(std::int32_t temperature_mc) {
    while (output_.curve_band + 1u < config_.curve_threshold_mc.size() &&
           temperature_mc >= config_.curve_threshold_mc[output_.curve_band + 1u]) {
        ++output_.curve_band;
    }
    while (output_.curve_band > 0 &&
           temperature_mc < config_.curve_threshold_mc[output_.curve_band] -
                                config_.curve_fall_hysteresis_mc) {
        --output_.curve_band;
    }
}

bool FanControlCore::safetyFaultActive() const {
    return (output_.current_faults & kSafetyFaults) != Fault::None;
}

void FanControlCore::enterFailSafe(Fault cause) {
    if (cause != Fault::None) {
        output_.current_faults |= cause;
        output_.latched_faults |= cause;
    }
    output_.state = has_sequence_ ? ControlState::FailSafe : ControlState::BootSafe;
    output_.duty_ns = config_.safe_duty_ns;
    normal_slew_active_ = true;
}

void FanControlCore::enterControlled(std::uint32_t now_ms) {
    output_.state = ControlState::Controlled;
    output_.current_faults &= ~(Fault::TempNotReceived | Fault::TempStale |
                                Fault::TempRangeInvalid);
    output_.mode = CoolingMode::Normal;
    boost_enter_count_ = 0;
    reheating_count_ = 0;
    boost_exit_timer_active_ = false;
    last_ramp_ms_ = now_ms;
    normal_slew_active_ = false;
    updateNormalBand(output_.filtered_temperature_mc);
}

void FanControlCore::applyNormalTarget(std::uint32_t now_ms) {
    const std::uint32_t target = config_.curve_duty_ns[output_.curve_band];
    if (!normal_slew_active_ || target <= output_.duty_ns) {
        output_.duty_ns = target;
        if (target <= output_.duty_ns) {
            normal_slew_active_ = false;
        }
        return;
    }

    if (elapsed(now_ms, last_ramp_ms_) < config_.ramp_interval_ms) {
        return;
    }
    const std::uint32_t intervals = elapsed(now_ms, last_ramp_ms_) /
                                    config_.ramp_interval_ms;
    last_ramp_ms_ += intervals * config_.ramp_interval_ms;
    const std::uint64_t increase = static_cast<std::uint64_t>(intervals) *
                                   config_.ramp_step_ns;
    output_.duty_ns = static_cast<std::uint32_t>(std::min<std::uint64_t>(
        target, static_cast<std::uint64_t>(output_.duty_ns) + increase));
    normal_slew_active_ = output_.duty_ns != target;
}

void FanControlCore::processBoost(std::uint32_t now_ms) {
    if (output_.filtered_temperature_mc <= config_.boost_exit_mc) {
        if (!boost_exit_timer_active_) {
            boost_exit_timer_active_ = true;
            boost_exit_since_ms_ = now_ms;
        } else if (elapsed(now_ms, boost_exit_since_ms_) >= config_.boost_exit_hold_ms) {
            output_.mode = CoolingMode::Normal;
            boost_enter_count_ = 0;
            reheating_count_ = 0;
            boost_exit_timer_active_ = false;
            normal_slew_active_ = true;
            last_ramp_ms_ = now_ms;
            return;
        }
    } else {
        boost_exit_timer_active_ = false;
    }

    if (output_.mode == CoolingMode::BoostCritical) {
        output_.duty_ns = config_.safe_duty_ns;
        return;
    }

    if (output_.filtered_temperature_mc >= config_.critical_mc) {
        output_.mode = CoolingMode::BoostCritical;
        output_.duty_ns = config_.safe_duty_ns;
        return;
    }

    const bool new_sample = last_trend_sequence_ != last_sequence_;
    if (new_sample) {
        last_trend_sequence_ = last_sequence_;
        output_.trend_valid = calculateTrend(now_ms, output_.trend_mc);
    }

    if (output_.mode == CoolingMode::BoostRamp) {
        if (new_sample && output_.trend_valid &&
            output_.trend_mc <= config_.cooling_trend_mc) {
            output_.mode = CoolingMode::BoostHold;
            reheating_count_ = 0;
            return;
        }
        if (elapsed(now_ms, last_ramp_ms_) >= config_.ramp_interval_ms) {
            const std::uint32_t intervals = elapsed(now_ms, last_ramp_ms_) /
                                            config_.ramp_interval_ms;
            last_ramp_ms_ += intervals * config_.ramp_interval_ms;
            const std::uint64_t reduction = static_cast<std::uint64_t>(intervals) *
                                            config_.ramp_step_ns;
            output_.duty_ns = reduction >= output_.duty_ns - config_.safe_duty_ns
                                  ? config_.safe_duty_ns
                                  : output_.duty_ns - static_cast<std::uint32_t>(reduction);
        }
        return;
    }

    if (output_.mode == CoolingMode::BoostHold && new_sample && output_.trend_valid) {
        if (output_.trend_mc >= config_.reheating_trend_mc) {
            reheating_count_ = static_cast<std::uint8_t>(std::min<unsigned>(
                config_.reheating_windows, static_cast<unsigned>(reheating_count_) + 1u));
            if (reheating_count_ >= config_.reheating_windows) {
                output_.mode = CoolingMode::BoostRamp;
                last_ramp_ms_ = now_ms;
                reheating_count_ = 0;
            }
        } else {
            reheating_count_ = 0;
        }
    }
}

void FanControlCore::processControlled(std::uint32_t now_ms) {
    updateNormalBand(output_.filtered_temperature_mc);

    if (output_.filtered_temperature_mc >= config_.critical_mc) {
        output_.mode = CoolingMode::BoostCritical;
        output_.duty_ns = config_.safe_duty_ns;
        boost_exit_timer_active_ = false;
        normal_slew_active_ = false;
        return;
    }

    if (output_.mode != CoolingMode::Normal) {
        processBoost(now_ms);
        if (output_.mode == CoolingMode::Normal) {
            applyNormalTarget(now_ms);
        }
        return;
    }

    if (last_processed_sequence_ != last_sequence_) {
        last_processed_sequence_ = last_sequence_;
        if (output_.filtered_temperature_mc >= config_.boost_enter_mc) {
            boost_enter_count_ = static_cast<std::uint8_t>(std::min<unsigned>(
                config_.boost_enter_samples, static_cast<unsigned>(boost_enter_count_) + 1u));
        } else {
            boost_enter_count_ = 0;
        }
        if (boost_enter_count_ >= config_.boost_enter_samples) {
            output_.mode = output_.filtered_temperature_mc >= config_.critical_mc
                               ? CoolingMode::BoostCritical
                               : CoolingMode::BoostRamp;
            last_ramp_ms_ = now_ms;
            boost_exit_timer_active_ = false;
            normal_slew_active_ = false;
            if (output_.mode == CoolingMode::BoostCritical) {
                output_.duty_ns = config_.safe_duty_ns;
            } else {
                output_.duty_ns = std::min(output_.duty_ns,
                                           config_.curve_duty_ns[output_.curve_band]);
            }
            return;
        }
    }
    applyNormalTarget(now_ms);
}

FanOutput FanControlCore::update(std::uint32_t now_ms) {
    if (!has_sequence_) {
        enterFailSafe(Fault::TempNotReceived);
    } else {
        output_.sample_age_ms = elapsed(now_ms, last_temperature_ms_);
        if (output_.sample_age_ms >= config_.temperature_timeout_ms) {
            if (!hasFault(output_.current_faults, Fault::TempStale)) {
                valid_recovery_count_ = 0;
            }
            output_.current_faults |= Fault::TempStale;
            output_.latched_faults |= Fault::TempStale;
            enterFailSafe(Fault::TempStale);
        } else if (safetyFaultActive()) {
            if (valid_recovery_count_ >= config_.recovery_samples &&
                !hasFault(output_.current_faults,
                          Fault::ProtocolIncompatible | Fault::RpmsgDisconnected |
                              Fault::FanStall | Fault::ControlDeadlineMissed |
                              Fault::PwmWriteFailed)) {
                enterControlled(now_ms);
            } else {
                enterFailSafe(Fault::None);
            }
        } else if (output_.state != ControlState::Controlled) {
            if (valid_recovery_count_ >= config_.recovery_samples) {
                enterControlled(now_ms);
            } else {
                enterFailSafe(Fault::None);
            }
        }
    }

    if (output_.state == ControlState::Controlled) {
        processControlled(now_ms);
    } else {
        output_.duty_ns = config_.safe_duty_ns;
    }
    output_.latched_faults |= output_.current_faults;
    return output_;
}

}  // namespace edgefocus::fan

#include "FanControllerApp.h"

namespace edgefocus::fan::m33 {
namespace {

bool timeReached(std::uint32_t now, std::uint32_t deadline) {
    return static_cast<std::int32_t>(now - deadline) >= 0;
}

}  // namespace

FanControllerApp::FanControllerApp(PlatformCallbacks callbacks,
                                   ControllerAppConfig app_config,
                                   FanConfig fan_config)
    : callbacks_(callbacks),
      app_config_(app_config),
      core_(fan_config),
      safe_duty_ns_(fan_config.safe_duty_ns) {}

bool FanControllerApp::writeDuty(std::uint32_t duty_ns) {
    if (callbacks_.write_pwm_duty_ns == nullptr ||
        !callbacks_.write_pwm_duty_ns(callbacks_.context, duty_ns)) {
        core_.setExternalFault(Fault::PwmWriteFailed, true);
        watchdog_healthy_ = false;
        return false;
    }
    applied_duty_ns_ = duty_ns;
    core_.setExternalFault(Fault::PwmWriteFailed, false);
    return true;
}

bool FanControllerApp::initialize(std::uint32_t now_ms) {
    // The core always starts at the calibrated safe high-speed duty.
    if (!writeDuty(core_.output().duty_ns)) {
        return false;
    }
    initialized_ = true;
    last_tick_ms_ = now_ms;
    next_status_ms_ = now_ms;
    watchdog_healthy_ = true;
    return true;
}

bool FanControllerApp::sendHello(std::uint32_t now_ms) {
    if (!transport_ready_ || callbacks_.send_rpmsg_frame == nullptr) {
        return false;
    }
    std::uint32_t capabilities = protocol::CapabilityM33Controller;
    if (app_config_.advertise_tachometer) {
        capabilities |= protocol::CapabilityTachometer;
    }
    protocol::FrameBuffer frame;
    if (!protocol::encodeHello(tx_sequence_++, now_ms,
                               {capabilities, app_config_.boot_id}, frame)) {
        return false;
    }
    return callbacks_.send_rpmsg_frame(callbacks_.context, frame.bytes.data(), frame.size);
}

void FanControllerApp::onTransportReady(std::uint32_t now_ms) {
    transport_ready_ = true;
    protocol_ready_ = false;
    core_.setExternalFault(Fault::RpmsgDisconnected, false);
    if (!sendHello(now_ms)) {
        core_.setExternalFault(Fault::RpmsgDisconnected, true);
        transport_ready_ = false;
    }
}

void FanControllerApp::onTransportLost() {
    transport_ready_ = false;
    protocol_ready_ = false;
    core_.setExternalFault(Fault::RpmsgDisconnected, true);
}

bool FanControllerApp::onRpmsgFrame(const std::uint8_t* data,
                                    std::size_t size,
                                    std::uint32_t received_at_ms) {
    protocol::DecodedFrame frame;
    const protocol::DecodeError error = protocol::decodeFrame(data, size, frame);
    if (error != protocol::DecodeError::None) {
        if (error == protocol::DecodeError::MajorVersionMismatch) {
            core_.setExternalFault(Fault::ProtocolIncompatible, true);
        }
        return false;
    }

    if (frame.type == protocol::MessageType::Hello) {
        protocol::HelloPayload hello;
        if (!protocol::decodeHello(frame, hello) ||
            (hello.capabilities & protocol::CapabilityLinuxBridge) == 0) {
            return false;
        }
        if (!has_bridge_boot_id_ || hello.boot_id != bridge_boot_id_) {
            core_.resetTemperatureSource();
            bridge_boot_id_ = hello.boot_id;
            has_bridge_boot_id_ = true;
        }
        // A valid bridge HELLO is also the transport re-attach signal.  The
        // previous bridge may have sent ShutdownNotice before closing the
        // persistent rpmsg-tty endpoint, which deliberately clears this flag.
        transport_ready_ = true;
        protocol_ready_ = true;
        core_.setExternalFault(Fault::ProtocolIncompatible, false);
        core_.setExternalFault(Fault::RpmsgDisconnected, false);
        return sendHello(received_at_ms);
    }

    if (frame.type == protocol::MessageType::TempSample && protocol_ready_) {
        protocol::TempSamplePayload temperature;
        return protocol::decodeTempSample(frame, temperature) &&
               core_.acceptTemperature(frame.sequence, temperature.temperature_mc,
                                       received_at_ms);
    }
    if (frame.type == protocol::MessageType::ShutdownNotice) {
        onTransportLost();
        return true;
    }
    // Unknown and directionally invalid messages are safely ignored.
    return false;
}

void FanControllerApp::onTachWindow(std::uint32_t pulse_count,
                                    std::uint32_t window_ms,
                                    std::uint32_t received_at_ms) {
    core_.acceptTachWindow(pulse_count, window_ms, received_at_ms);
}

bool FanControllerApp::sendStatus(std::uint32_t now_ms) {
    if (!transport_ready_ || !protocol_ready_ || callbacks_.send_rpmsg_frame == nullptr) {
        return false;
    }
    const FanOutput& output = core_.output();
    const protocol::StatusPayload status{
        static_cast<std::uint8_t>(output.state),
        static_cast<std::uint8_t>(output.mode),
        output.curve_band,
        0,
        output.duty_ns,
        output.rpm,
        output.filtered_temperature_mc,
        output.trend_mc,
        output.sample_age_ms,
        static_cast<std::uint32_t>(output.current_faults),
        static_cast<std::uint32_t>(output.latched_faults),
    };
    protocol::FrameBuffer frame;
    return protocol::encodeStatus(tx_sequence_++, now_ms, status, frame) &&
           callbacks_.send_rpmsg_frame(callbacks_.context, frame.bytes.data(), frame.size);
}

void FanControllerApp::tick(std::uint32_t now_ms) {
    if (!initialized_) {
        watchdog_healthy_ = false;
        return;
    }

    const bool deadline_missed =
        has_ticked_ && now_ms - last_tick_ms_ > app_config_.control_deadline_ms;
    core_.setExternalFault(Fault::ControlDeadlineMissed, deadline_missed);
    last_tick_ms_ = now_ms;
    has_ticked_ = true;

    const FanOutput output = core_.update(now_ms);
    bool pwm_ok = true;
    if (output.duty_ns != applied_duty_ns_) {
        pwm_ok = writeDuty(output.duty_ns);
        if (!pwm_ok) {
            // Best effort: retry the calibrated safe value. Failure remains
            // latched and prevents the board adapter from feeding WWDG.
            writeDuty(safe_duty_ns_);
        }
    }

    bool protocol_ok = true;
    if (transport_ready_ && protocol_ready_ && timeReached(now_ms, next_status_ms_)) {
        protocol_ok = sendStatus(now_ms);
        next_status_ms_ = now_ms + app_config_.status_interval_ms;
        if (!protocol_ok) {
            onTransportLost();
        }
    }
    watchdog_healthy_ = pwm_ok && !deadline_missed && protocol_ok;
}

}  // namespace edgefocus::fan::m33

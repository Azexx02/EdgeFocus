#pragma once

#include "FanControlCore.h"
#include "FanProtocol.h"

#include <cstddef>
#include <cstdint>

namespace edgefocus::fan::m33 {

struct PlatformCallbacks {
    void* context{nullptr};
    bool (*write_pwm_duty_ns)(void* context, std::uint32_t duty_ns){nullptr};
    bool (*send_rpmsg_frame)(void* context,
                             const std::uint8_t* data,
                             std::size_t size){nullptr};
};

struct ControllerAppConfig {
    std::uint64_t boot_id{0};
    std::uint32_t status_interval_ms{1000};
    std::uint32_t control_deadline_ms{250};
    bool advertise_tachometer{false};
};

// Portable M33 task logic. The board project owns clocks, RIF, pinmux, TIM,
// input capture, OpenAMP endpoint creation, scheduler, and WWDG. This class
// owns only protocol/control sequencing and never guesses board registers.
class FanControllerApp {
public:
    FanControllerApp(PlatformCallbacks callbacks,
                     ControllerAppConfig app_config,
                     FanConfig fan_config = {});

    // The platform must call this immediately after gaining PWM ownership.
    // It writes safe duty before any OpenAMP operation is required.
    bool initialize(std::uint32_t now_ms);

    void onTransportReady(std::uint32_t now_ms);
    void onTransportLost();
    bool onRpmsgFrame(const std::uint8_t* data,
                      std::size_t size,
                      std::uint32_t received_at_ms);
    void onTachWindow(std::uint32_t pulse_count,
                      std::uint32_t window_ms,
                      std::uint32_t received_at_ms);

    void tick(std::uint32_t now_ms);

    const FanOutput& output() const { return core_.output(); }
    bool protocolReady() const { return protocol_ready_; }
    bool healthyForWatchdog() const { return watchdog_healthy_; }

private:
    bool sendHello(std::uint32_t now_ms);
    bool sendStatus(std::uint32_t now_ms);
    bool writeDuty(std::uint32_t duty_ns);

    PlatformCallbacks callbacks_;
    ControllerAppConfig app_config_;
    FanControlCore core_;
    bool initialized_{false};
    bool transport_ready_{false};
    bool protocol_ready_{false};
    bool watchdog_healthy_{false};
    bool has_ticked_{false};
    std::uint32_t last_tick_ms_{0};
    std::uint32_t next_status_ms_{0};
    std::uint32_t tx_sequence_{1};
    std::uint32_t applied_duty_ns_{0};
    std::uint32_t safe_duty_ns_{8000};
    bool has_bridge_boot_id_{false};
    std::uint64_t bridge_boot_id_{0};
};

}  // namespace edgefocus::fan::m33

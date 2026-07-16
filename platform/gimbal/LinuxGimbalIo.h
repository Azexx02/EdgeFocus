#pragma once

#include "device/GpioHelper.h"
#include "platform/gimbal/IGimbalIo.h"

#include <string>

namespace edgefocus::gimbal {

class LinuxGimbalIo final : public IGimbalIo {
public:
    struct Options {
        ControlMode control_mode{ControlMode::UartOnly};
        std::string uart_device{"/dev/ttySTM3"};
        std::string boot0_line{"PD4"};
        std::string fault_line{"PD6"};
        std::string enable_line{"PD7"};
    };

    LinuxGimbalIo();
    explicit LinuxGimbalIo(Options options);
    ~LinuxGimbalIo() override;

    bool open(std::string& error) override;
    void close() noexcept override;
    bool isOpen() const noexcept override;
    ControlMode controlMode() const noexcept override { return options_.control_mode; }
    bool setBoot0(bool high, std::string& error) override;
    bool setEnable(bool high, std::string& error) override;
    bool readFaultActive(bool& active, std::string& error) override;
    bool writeLine(const std::string& line, std::string& error) override;
    bool writeEmergencyStop(std::string& error) override;
    bool readBytes(std::string& bytes, int timeout_ms, std::string& error) override;

private:
    bool configureSerial(std::string& error);

    Options options_;
    device::GpioLine boot0_;
    device::GpioLine fault_;
    device::GpioLine enable_;
    int uart_fd_{-1};
};

}  // namespace edgefocus::gimbal

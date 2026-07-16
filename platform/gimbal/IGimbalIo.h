#pragma once

#include "platform/gimbal/GimbalTypes.h"

#include <string>

namespace edgefocus::gimbal {

class IGimbalIo {
public:
    virtual ~IGimbalIo() = default;

    virtual bool open(std::string& error) = 0;
    virtual void close() noexcept = 0;
    virtual bool isOpen() const noexcept = 0;
    virtual ControlMode controlMode() const noexcept {
        return ControlMode::UartGpioInterlock;
    }
    virtual bool setBoot0(bool high, std::string& error) = 0;
    virtual bool setEnable(bool high, std::string& error) = 0;
    virtual bool readFaultActive(bool& active, std::string& error) = 0;
    virtual bool writeLine(const std::string& line, std::string& error) = 0;
    virtual bool writeEmergencyStop(std::string& error) { return writeLine("!", error); }
    virtual bool readBytes(std::string& bytes, int timeout_ms, std::string& error) = 0;
};

}  // namespace edgefocus::gimbal

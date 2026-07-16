#pragma once

#include "platform/power/IPowerSourceIo.h"

#include <string>

namespace edgefocus::power {

class LinuxI2cPowerSourceIo final : public IPowerSourceIo {
public:
    explicit LinuxI2cPowerSourceIo(std::string device = "/dev/i2c-1",
                                   std::uint8_t address = 0x2A);
    ~LinuxI2cPowerSourceIo() override;

    bool open(std::string& error) override;
    void close() noexcept override;
    bool readStatus(std::uint8_t& port_status, std::uint8_t& monitoring_status,
                    std::string& error) override;

private:
    bool readRegister(std::uint8_t reg, std::uint8_t& value, std::string& error);

    std::string device_;
    std::uint8_t address_;
    int fd_{-1};
};

}  // namespace edgefocus::power

#include "platform/power/LinuxI2cPowerSourceIo.h"

#include <cerrno>
#include <cstring>
#include <utility>

#ifdef __linux__
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace edgefocus::power {

LinuxI2cPowerSourceIo::LinuxI2cPowerSourceIo(std::string device, std::uint8_t address)
    : device_(std::move(device)), address_(address) {}
LinuxI2cPowerSourceIo::~LinuxI2cPowerSourceIo() { close(); }

bool LinuxI2cPowerSourceIo::open(std::string& error) {
    close();
#ifdef __linux__
    fd_ = ::open(device_.c_str(), O_RDWR | O_CLOEXEC);
    if (fd_ < 0) {
        error = "open " + device_ + ": " + std::strerror(errno);
        return false;
    }
    std::uint8_t device_id = 0;
    if (!readRegister(0x2F, device_id, error) || device_id != 0x25) {
        if (error.empty()) error = "unexpected STUSB4500 device id";
        close();
        return false;
    }
    error.clear();
    return true;
#else
    error = "Linux I2C unavailable on this platform";
    return false;
#endif
}

void LinuxI2cPowerSourceIo::close() noexcept {
#ifdef __linux__
    if (fd_ >= 0) ::close(fd_);
#endif
    fd_ = -1;
}

bool LinuxI2cPowerSourceIo::readRegister(std::uint8_t reg, std::uint8_t& value,
                                         std::string& error) {
#ifdef __linux__
    if (fd_ < 0) {
        error = "I2C device not open";
        return false;
    }
    i2c_msg messages[2]{};
    messages[0].addr = address_;
    messages[0].flags = 0;
    messages[0].len = 1;
    messages[0].buf = &reg;
    messages[1].addr = address_;
    messages[1].flags = I2C_M_RD;
    messages[1].len = 1;
    messages[1].buf = &value;
    i2c_rdwr_ioctl_data transfer{};
    transfer.msgs = messages;
    transfer.nmsgs = 2;
    if (ioctl(fd_, I2C_RDWR, &transfer) < 0) {
        error = "STUSB4500 register read failed: " + std::string(std::strerror(errno));
        return false;
    }
    error.clear();
    return true;
#else
    (void)reg;
    (void)value;
    error = "Linux I2C unavailable on this platform";
    return false;
#endif
}

bool LinuxI2cPowerSourceIo::readStatus(std::uint8_t& port_status,
                                       std::uint8_t& monitoring_status,
                                       std::string& error) {
    return readRegister(0x0E, port_status, error) &&
           readRegister(0x10, monitoring_status, error);
}

}  // namespace edgefocus::power

#include "platform/gimbal/LinuxGimbalIo.h"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <utility>

#ifdef __linux__
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace edgefocus::gimbal {

LinuxGimbalIo::LinuxGimbalIo() : LinuxGimbalIo(Options{}) {}
LinuxGimbalIo::LinuxGimbalIo(Options options) : options_(std::move(options)) {}
LinuxGimbalIo::~LinuxGimbalIo() { close(); }

bool LinuxGimbalIo::open(std::string& error) {
    close();
#ifdef __linux__
    if (options_.control_mode == ControlMode::UartGpioInterlock) {
        // Request outputs with their safe values atomically. Enable is requested
        // before any UART traffic and never defaults high.
        boot0_ = device::GpioHelper::openLine(options_.boot0_line, true, 0);
        enable_ = device::GpioHelper::openLine(options_.enable_line, true, 0);
        fault_ = device::GpioHelper::openLine(options_.fault_line, false, 0);
        if (!boot0_.isValid() || !enable_.isValid() || !fault_.isValid()) {
            error = "failed to acquire PD4/PD6/PD7";
            close();
            return false;
        }
        if (!device::GpioHelper::trySetValue(boot0_, 0, &error) ||
            !device::GpioHelper::trySetValue(enable_, 0, &error)) {
            close();
            return false;
        }
    }

    uart_fd_ = ::open(options_.uart_device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (uart_fd_ < 0) {
        error = "open " + options_.uart_device + ": " + std::strerror(errno);
        close();
        return false;
    }
    if (!configureSerial(error)) {
        close();
        return false;
    }
    error.clear();
    return true;
#else
    error = "Linux gimbal I/O is unavailable on this platform";
    return false;
#endif
}

void LinuxGimbalIo::close() noexcept {
#ifdef __linux__
    if (enable_.isValid()) {
        std::string ignored;
        (void)device::GpioHelper::trySetValue(enable_, 0, &ignored);
    }
    if (uart_fd_ >= 0) {
        ::close(uart_fd_);
        uart_fd_ = -1;
    }
#endif
    device::GpioHelper::closeLine(fault_);
    device::GpioHelper::closeLine(enable_);
    device::GpioHelper::closeLine(boot0_);
}

bool LinuxGimbalIo::isOpen() const noexcept {
    if (uart_fd_ < 0) return false;
    return options_.control_mode == ControlMode::UartOnly ||
           (boot0_.isValid() && fault_.isValid() && enable_.isValid());
}

bool LinuxGimbalIo::setBoot0(bool high, std::string& error) {
    if (high) {
        error = "BOOT0 high is forbidden by the production gimbal interface";
        return false;
    }
    if (options_.control_mode == ControlMode::UartOnly) {
        error.clear();
        return true;
    }
    return device::GpioHelper::trySetValue(boot0_, 0, &error);
}

bool LinuxGimbalIo::setEnable(bool high, std::string& error) {
    if (options_.control_mode == ControlMode::UartOnly) {
        error.clear();
        return true;
    }
    return device::GpioHelper::trySetValue(enable_, high ? 1 : 0, &error);
}

bool LinuxGimbalIo::readFaultActive(bool& active, std::string& error) {
    if (options_.control_mode == ControlMode::UartOnly) {
        active = false;
        error.clear();
        return true;
    }
    int value = 0;
    if (!device::GpioHelper::readValue(fault_, value, &error)) return false;
    active = value == 0;
    return true;
}

bool LinuxGimbalIo::writeEmergencyStop(std::string& error) {
#ifdef __linux__
    const char stop = '!';
    return writeAll(&stop, 1, 50, error);
#else
    error = "UART unavailable on this platform";
    return false;
#endif
}

bool LinuxGimbalIo::writeLine(const std::string& line, std::string& error) {
#ifdef __linux__
    const std::string framed = line + "\r\n";
    return writeAll(framed.data(), framed.size(), 200, error);
#else
    (void)line;
    error = "UART unavailable on this platform";
    return false;
#endif
}

bool LinuxGimbalIo::writeAll(const char* data, std::size_t size, int timeout_ms,
                             std::string& error) {
#ifdef __linux__
    if (uart_fd_ < 0) {
        error = "UART is not open";
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(std::max(1, timeout_ms));
    std::size_t written = 0;
    while (written < size) {
        const auto count = ::write(uart_fd_, data + written, size - written);
        if (count > 0) {
            written += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count();
            if (remaining <= 0) {
                error = "UART write deadline exceeded";
                return false;
            }
            pollfd descriptor{};
            descriptor.fd = uart_fd_;
            descriptor.events = POLLOUT;
            int ready = 0;
            do {
                ready = ::poll(&descriptor, 1, static_cast<int>(remaining));
            } while (ready < 0 && errno == EINTR);
            if (ready == 0) {
                error = "UART write deadline exceeded";
                return false;
            }
            if (ready < 0 ||
                (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                error = ready < 0 ?
                    "UART write poll failed: " + std::string(std::strerror(errno)) :
                    "UART disconnected during write";
                return false;
            }
            continue;
        }
        error = "UART write failed: " + std::string(std::strerror(errno));
        return false;
    }
    error.clear();
    return true;
#else
    (void)data;
    (void)size;
    (void)timeout_ms;
    error = "UART unavailable on this platform";
    return false;
#endif
}

bool LinuxGimbalIo::readBytes(std::string& bytes, int timeout_ms, std::string& error) {
    bytes.clear();
#ifdef __linux__
    if (uart_fd_ < 0) {
        error = "UART is not open";
        return false;
    }
    pollfd descriptor{};
    descriptor.fd = uart_fd_;
    descriptor.events = POLLIN;
    int result = 0;
    do {
        result = ::poll(&descriptor, 1, std::max(0, timeout_ms));
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
        error = "UART poll failed: " + std::string(std::strerror(errno));
        return false;
    }
    if (result == 0) {
        error.clear();
        return true;
    }
    if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        error = "UART disconnected";
        return false;
    }
    char buffer[256];
    ssize_t count = 0;
    do {
        count = ::read(uart_fd_, buffer, sizeof(buffer));
    } while (count < 0 && errno == EINTR);
    if (count > 0) {
        bytes.assign(buffer, static_cast<std::size_t>(count));
        error.clear();
        return true;
    }
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        error.clear();
        return true;
    }
    error = count == 0 ? "UART closed" : "UART read failed: " + std::string(std::strerror(errno));
    return false;
#else
    (void)timeout_ms;
    error = "UART unavailable on this platform";
    return false;
#endif
}

bool LinuxGimbalIo::configureSerial(std::string& error) {
#ifdef __linux__
    termios tty{};
    if (tcgetattr(uart_fd_, &tty) != 0) {
        error = "tcgetattr failed: " + std::string(std::strerror(errno));
        return false;
    }
    cfmakeraw(&tty);
    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;
    if (tcsetattr(uart_fd_, TCSANOW, &tty) != 0) {
        error = "tcsetattr failed: " + std::string(std::strerror(errno));
        return false;
    }
    tcflush(uart_fd_, TCIOFLUSH);
    return true;
#else
    error = "serial configuration unavailable";
    return false;
#endif
}

}  // namespace edgefocus::gimbal

#include "RpmsgTransport.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <utility>

namespace edgefocus::fan::bridge {

RpmsgTransport::RpmsgTransport(std::string device_path)
    : device_path_(std::move(device_path)) {}

RpmsgTransport::~RpmsgTransport() {
    closeDevice();
}

bool RpmsgTransport::openDevice(std::string& error) {
    closeDevice();
    descriptor_ = ::open(device_path_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (descriptor_ < 0) {
        error = "cannot open " + device_path_ + ": " + std::strerror(errno);
        return false;
    }

    termios attributes{};
    if (::tcgetattr(descriptor_, &attributes) == 0) {
        ::cfmakeraw(&attributes);
        if (::tcsetattr(descriptor_, TCSANOW, &attributes) != 0) {
            error = "cannot set raw mode on " + device_path_ + ": " +
                    std::strerror(errno);
            closeDevice();
            return false;
        }
    }
    decoder_.reset();
    return true;
}

void RpmsgTransport::closeDevice() {
    if (descriptor_ >= 0) {
        ::close(descriptor_);
        descriptor_ = -1;
    }
    decoder_.reset();
}

bool RpmsgTransport::send(const protocol::FrameBuffer& frame, std::string& error) {
    if (!isOpen()) {
        error = "RPMsg device is not open";
        return false;
    }
    std::size_t written = 0;
    while (written < frame.size) {
        const ssize_t result = ::write(descriptor_, frame.bytes.data() + written,
                                       frame.size - written);
        if (result > 0) {
            written += static_cast<std::size_t>(result);
            continue;
        }
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pollfd descriptor{descriptor_, POLLOUT, 0};
            const int poll_result = ::poll(&descriptor, 1, 200);
            if (poll_result > 0) {
                continue;
            }
            error = poll_result == 0 ? "RPMsg write timed out" : std::strerror(errno);
            return false;
        }
        error = "RPMsg write failed: " + std::string(std::strerror(errno));
        return false;
    }
    return true;
}

bool RpmsgTransport::receive(protocol::FrameBuffer& frame, std::string& error) {
    if (decoder_.pop(frame)) {
        return true;
    }
    if (!isOpen()) {
        error = "RPMsg device is not open";
        return false;
    }

    std::array<std::uint8_t, 512> buffer{};
    for (;;) {
        const ssize_t result = ::read(descriptor_, buffer.data(), buffer.size());
        if (result > 0) {
            decoder_.append(buffer.data(), static_cast<std::size_t>(result));
            if (decoder_.pop(frame)) {
                return true;
            }
            continue;
        }
        if (result == 0) {
            error = "RPMsg endpoint closed";
            closeDevice();
            return false;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            error.clear();
            return false;
        }
        error = "RPMsg read failed: " + std::string(std::strerror(errno));
        closeDevice();
        return false;
    }
}

}  // namespace edgefocus::fan::bridge

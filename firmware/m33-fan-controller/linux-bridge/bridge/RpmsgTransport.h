#pragma once

#include "FanProtocol.h"
#include "FrameStreamDecoder.h"

#include <string>

namespace edgefocus::fan::bridge {

class RpmsgTransport {
public:
    explicit RpmsgTransport(std::string device_path);
    ~RpmsgTransport();

    RpmsgTransport(const RpmsgTransport&) = delete;
    RpmsgTransport& operator=(const RpmsgTransport&) = delete;

    bool openDevice(std::string& error);
    void closeDevice();
    bool isOpen() const { return descriptor_ >= 0; }
    bool send(const protocol::FrameBuffer& frame, std::string& error);
    bool receive(protocol::FrameBuffer& frame, std::string& error);

private:
    std::string device_path_;
    int descriptor_{-1};
    FrameStreamDecoder decoder_;
};

}  // namespace edgefocus::fan::bridge

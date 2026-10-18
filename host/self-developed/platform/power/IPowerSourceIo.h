#pragma once

#include <cstdint>
#include <string>

namespace edgefocus::power {

class IPowerSourceIo {
public:
    virtual ~IPowerSourceIo() = default;
    virtual bool open(std::string& error) = 0;
    virtual void close() noexcept = 0;
    virtual bool readStatus(std::uint8_t& port_status, std::uint8_t& monitoring_status,
                            std::string& error) = 0;
};

}  // namespace edgefocus::power

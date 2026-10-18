#pragma once

#include "FanProtocol.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace edgefocus::fan::bridge {

class FrameStreamDecoder {
public:
    static constexpr std::size_t kCapacity = protocol::kMaxFrameSize * 4;

    void reset();
    void append(const std::uint8_t* data, std::size_t size);
    bool pop(protocol::FrameBuffer& frame);
    std::uint32_t droppedBytes() const { return dropped_bytes_; }

private:
    void discard(std::size_t count);
    bool startsWithMagic() const;

    std::array<std::uint8_t, kCapacity> bytes_{};
    std::size_t size_{0};
    std::uint32_t dropped_bytes_{0};
};

}  // namespace edgefocus::fan::bridge

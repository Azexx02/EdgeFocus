#include "FrameStreamDecoder.h"

#include <algorithm>

namespace edgefocus::fan::bridge {
namespace {

std::uint16_t get16(const std::uint8_t* source) {
    return static_cast<std::uint16_t>(source[0]) |
           static_cast<std::uint16_t>(source[1]) << 8u;
}

}  // namespace

void FrameStreamDecoder::reset() {
    size_ = 0;
    dropped_bytes_ = 0;
}

void FrameStreamDecoder::discard(std::size_t count) {
    count = std::min(count, size_);
    std::move(bytes_.begin() + static_cast<std::ptrdiff_t>(count),
              bytes_.begin() + static_cast<std::ptrdiff_t>(size_), bytes_.begin());
    size_ -= count;
    dropped_bytes_ += static_cast<std::uint32_t>(count);
}

bool FrameStreamDecoder::startsWithMagic() const {
    if (size_ < 4) {
        return false;
    }
    return bytes_[0] == static_cast<std::uint8_t>(protocol::kMagic) &&
           bytes_[1] == static_cast<std::uint8_t>(protocol::kMagic >> 8u) &&
           bytes_[2] == static_cast<std::uint8_t>(protocol::kMagic >> 16u) &&
           bytes_[3] == static_cast<std::uint8_t>(protocol::kMagic >> 24u);
}

void FrameStreamDecoder::append(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size == 0) {
        return;
    }
    if (size >= bytes_.size()) {
        data += size - bytes_.size();
        dropped_bytes_ += static_cast<std::uint32_t>(size_ + size - bytes_.size());
        size = bytes_.size();
        size_ = 0;
    } else if (size_ + size > bytes_.size()) {
        discard(size_ + size - bytes_.size());
    }
    std::copy(data, data + size, bytes_.begin() + static_cast<std::ptrdiff_t>(size_));
    size_ += size;
}

bool FrameStreamDecoder::pop(protocol::FrameBuffer& frame) {
    while (size_ >= 4 && !startsWithMagic()) {
        discard(1);
    }
    if (size_ < protocol::kHeaderSize) {
        return false;
    }

    const std::uint16_t payload_size = get16(bytes_.data() + 8);
    const std::uint16_t header_size = get16(bytes_.data() + 10);
    if (header_size != protocol::kHeaderSize || payload_size > protocol::kMaxPayloadSize) {
        discard(1);
        return pop(frame);
    }
    const std::size_t frame_size = header_size + payload_size;
    if (size_ < frame_size) {
        return false;
    }

    protocol::DecodedFrame decoded;
    if (protocol::decodeFrame(bytes_.data(), frame_size, decoded) !=
        protocol::DecodeError::None) {
        discard(1);
        return pop(frame);
    }

    std::copy_n(bytes_.begin(), frame_size, frame.bytes.begin());
    frame.size = frame_size;
    discard(frame_size);
    return true;
}

}  // namespace edgefocus::fan::bridge

#include "FanProtocol.h"

#include <algorithm>

namespace edgefocus::fan::protocol {
namespace {

void put16(std::uint8_t* destination, std::uint16_t value) {
    destination[0] = static_cast<std::uint8_t>(value);
    destination[1] = static_cast<std::uint8_t>(value >> 8u);
}

void put32(std::uint8_t* destination, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        destination[i] = static_cast<std::uint8_t>(value >> (i * 8u));
    }
}

void put64(std::uint8_t* destination, std::uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) {
        destination[i] = static_cast<std::uint8_t>(value >> (i * 8u));
    }
}

std::uint16_t get16(const std::uint8_t* source) {
    return static_cast<std::uint16_t>(source[0]) |
           static_cast<std::uint16_t>(source[1]) << 8u;
}

std::uint32_t get32(const std::uint8_t* source) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(source[i]) << (i * 8u);
    }
    return value;
}

std::uint64_t get64(const std::uint8_t* source) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(source[i]) << (i * 8u);
    }
    return value;
}

bool typeMatches(const DecodedFrame& frame, MessageType type, std::size_t size) {
    return frame.type == type && frame.payload != nullptr && frame.payload_size == size;
}

}  // namespace

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

bool encodeFrame(MessageType type,
                 std::uint32_t sequence,
                 std::uint32_t sender_monotonic_ms,
                 const std::uint8_t* payload,
                 std::uint16_t payload_size,
                 FrameBuffer& output,
                 std::uint8_t flags,
                 std::uint8_t protocol_minor) {
    if (payload_size > kMaxPayloadSize || (payload_size != 0 && payload == nullptr)) {
        return false;
    }

    output.size = kHeaderSize + payload_size;
    put32(output.bytes.data(), kMagic);
    output.bytes[4] = kProtocolMajor;
    output.bytes[5] = protocol_minor;
    output.bytes[6] = static_cast<std::uint8_t>(type);
    output.bytes[7] = flags;
    put16(output.bytes.data() + 8, payload_size);
    put16(output.bytes.data() + 10, static_cast<std::uint16_t>(kHeaderSize));
    put32(output.bytes.data() + 12, sequence);
    put32(output.bytes.data() + 16, sender_monotonic_ms);
    put32(output.bytes.data() + 20, crc32(payload, payload_size));
    if (payload_size != 0) {
        std::copy(payload, payload + payload_size, output.bytes.begin() + kHeaderSize);
    }
    return true;
}

DecodeError decodeFrame(const std::uint8_t* data,
                        std::size_t size,
                        DecodedFrame& output) {
    if (data == nullptr || size < kHeaderSize) {
        return DecodeError::Truncated;
    }
    if (get32(data) != kMagic) {
        return DecodeError::BadMagic;
    }
    if (get16(data + 10) != kHeaderSize) {
        return DecodeError::BadHeaderLength;
    }
    const std::uint16_t payload_size = get16(data + 8);
    if (payload_size > kMaxPayloadSize) {
        return DecodeError::PayloadTooLarge;
    }
    if (size != kHeaderSize + payload_size) {
        return DecodeError::LengthMismatch;
    }
    if (data[4] != kProtocolMajor) {
        return DecodeError::MajorVersionMismatch;
    }
    const std::uint8_t* payload = data + kHeaderSize;
    if (get32(data + 20) != crc32(payload, payload_size)) {
        return DecodeError::CrcMismatch;
    }

    output.type = static_cast<MessageType>(data[6]);
    output.protocol_minor = data[5];
    output.flags = data[7];
    output.sequence = get32(data + 12);
    output.sender_monotonic_ms = get32(data + 16);
    output.payload = payload;
    output.payload_size = payload_size;
    return DecodeError::None;
}

bool encodeHello(std::uint32_t sequence,
                 std::uint32_t monotonic_ms,
                 const HelloPayload& payload,
                 FrameBuffer& output) {
    std::array<std::uint8_t, 12> bytes{};
    put32(bytes.data(), payload.capabilities);
    put64(bytes.data() + 4, payload.boot_id);
    return encodeFrame(MessageType::Hello, sequence, monotonic_ms,
                       bytes.data(), static_cast<std::uint16_t>(bytes.size()), output);
}

bool decodeHello(const DecodedFrame& frame, HelloPayload& output) {
    if (!typeMatches(frame, MessageType::Hello, 12)) {
        return false;
    }
    output.capabilities = get32(frame.payload);
    output.boot_id = get64(frame.payload + 4);
    return true;
}

bool encodeTempSample(std::uint32_t sequence,
                      std::uint32_t monotonic_ms,
                      const TempSamplePayload& payload,
                      FrameBuffer& output) {
    std::array<std::uint8_t, 8> bytes{};
    put32(bytes.data(), static_cast<std::uint32_t>(payload.temperature_mc));
    put32(bytes.data() + 4, payload.thermal_source_id);
    return encodeFrame(MessageType::TempSample, sequence, monotonic_ms,
                       bytes.data(), static_cast<std::uint16_t>(bytes.size()), output);
}

bool decodeTempSample(const DecodedFrame& frame, TempSamplePayload& output) {
    if (!typeMatches(frame, MessageType::TempSample, 8)) {
        return false;
    }
    output.temperature_mc = static_cast<std::int32_t>(get32(frame.payload));
    output.thermal_source_id = get32(frame.payload + 4);
    return true;
}

bool encodeStatus(std::uint32_t sequence,
                  std::uint32_t monotonic_ms,
                  const StatusPayload& payload,
                  FrameBuffer& output) {
    std::array<std::uint8_t, 32> bytes{};
    bytes[0] = payload.control_state;
    bytes[1] = payload.cooling_mode;
    bytes[2] = payload.curve_band;
    bytes[3] = payload.reserved;
    put32(bytes.data() + 4, payload.duty_ns);
    put32(bytes.data() + 8, payload.rpm);
    put32(bytes.data() + 12, static_cast<std::uint32_t>(payload.filtered_temperature_mc));
    put32(bytes.data() + 16, static_cast<std::uint32_t>(payload.trend_mc));
    put32(bytes.data() + 20, payload.sample_age_ms);
    put32(bytes.data() + 24, payload.current_faults);
    put32(bytes.data() + 28, payload.latched_faults);
    return encodeFrame(MessageType::Status, sequence, monotonic_ms,
                       bytes.data(), static_cast<std::uint16_t>(bytes.size()), output);
}

bool decodeStatus(const DecodedFrame& frame, StatusPayload& output) {
    if (!typeMatches(frame, MessageType::Status, 32)) {
        return false;
    }
    output.control_state = frame.payload[0];
    output.cooling_mode = frame.payload[1];
    output.curve_band = frame.payload[2];
    output.reserved = frame.payload[3];
    output.duty_ns = get32(frame.payload + 4);
    output.rpm = get32(frame.payload + 8);
    output.filtered_temperature_mc = static_cast<std::int32_t>(get32(frame.payload + 12));
    output.trend_mc = static_cast<std::int32_t>(get32(frame.payload + 16));
    output.sample_age_ms = get32(frame.payload + 20);
    output.current_faults = get32(frame.payload + 24);
    output.latched_faults = get32(frame.payload + 28);
    return true;
}

bool encodeFault(std::uint32_t sequence,
                 std::uint32_t monotonic_ms,
                 const FaultPayload& payload,
                 FrameBuffer& output) {
    std::array<std::uint8_t, 12> bytes{};
    put32(bytes.data(), payload.current_faults);
    put32(bytes.data() + 4, payload.latched_faults);
    put32(bytes.data() + 8, payload.first_fault_monotonic_ms);
    return encodeFrame(MessageType::Fault, sequence, monotonic_ms,
                       bytes.data(), static_cast<std::uint16_t>(bytes.size()), output);
}

bool decodeFault(const DecodedFrame& frame, FaultPayload& output) {
    if (!typeMatches(frame, MessageType::Fault, 12)) {
        return false;
    }
    output.current_faults = get32(frame.payload);
    output.latched_faults = get32(frame.payload + 4);
    output.first_fault_monotonic_ms = get32(frame.payload + 8);
    return true;
}

bool encodeShutdownNotice(std::uint32_t sequence,
                          std::uint32_t monotonic_ms,
                          const ShutdownNoticePayload& payload,
                          FrameBuffer& output) {
    std::array<std::uint8_t, 4> bytes{};
    put32(bytes.data(), payload.reason);
    return encodeFrame(MessageType::ShutdownNotice, sequence, monotonic_ms,
                       bytes.data(), static_cast<std::uint16_t>(bytes.size()), output);
}

bool decodeShutdownNotice(const DecodedFrame& frame, ShutdownNoticePayload& output) {
    if (!typeMatches(frame, MessageType::ShutdownNotice, 4)) {
        return false;
    }
    output.reason = get32(frame.payload);
    return true;
}

}  // namespace edgefocus::fan::protocol

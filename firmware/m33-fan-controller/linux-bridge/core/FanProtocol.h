#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace edgefocus::fan::protocol {

constexpr std::uint32_t kMagic = 0x4E464645u;  // "EFFN" on the wire.
constexpr std::uint8_t kProtocolMajor = 1;
constexpr std::uint8_t kProtocolMinor = 0;
constexpr std::size_t kHeaderSize = 24;
constexpr std::size_t kMaxPayloadSize = 96;
constexpr std::size_t kMaxFrameSize = kHeaderSize + kMaxPayloadSize;

enum class MessageType : std::uint8_t {
    Hello = 1,
    TempSample = 2,
    Status = 3,
    Fault = 4,
    ShutdownNotice = 5,
};

enum Capability : std::uint32_t {
    CapabilityLinuxBridge = 1u << 0,
    CapabilityM33Controller = 1u << 1,
    CapabilityTachometer = 1u << 2,
};

enum class DecodeError : std::uint8_t {
    None = 0,
    Truncated,
    BadMagic,
    BadHeaderLength,
    PayloadTooLarge,
    LengthMismatch,
    MajorVersionMismatch,
    CrcMismatch,
};

struct FrameBuffer {
    std::array<std::uint8_t, kMaxFrameSize> bytes{};
    std::size_t size{0};
};

struct DecodedFrame {
    MessageType type{MessageType::Hello};
    std::uint8_t protocol_minor{0};
    std::uint8_t flags{0};
    std::uint32_t sequence{0};
    std::uint32_t sender_monotonic_ms{0};
    const std::uint8_t* payload{nullptr};
    std::uint16_t payload_size{0};
};

struct HelloPayload {
    std::uint32_t capabilities{0};
    std::uint64_t boot_id{0};
};

struct TempSamplePayload {
    std::int32_t temperature_mc{0};
    std::uint32_t thermal_source_id{0};
};

struct StatusPayload {
    std::uint8_t control_state{0};
    std::uint8_t cooling_mode{0};
    std::uint8_t curve_band{0};
    std::uint8_t reserved{0};
    std::uint32_t duty_ns{0};
    std::uint32_t rpm{0};
    std::int32_t filtered_temperature_mc{0};
    std::int32_t trend_mc{0};
    std::uint32_t sample_age_ms{0};
    std::uint32_t current_faults{0};
    std::uint32_t latched_faults{0};
};

struct FaultPayload {
    std::uint32_t current_faults{0};
    std::uint32_t latched_faults{0};
    std::uint32_t first_fault_monotonic_ms{0};
};

struct ShutdownNoticePayload {
    std::uint32_t reason{0};
};

std::uint32_t crc32(const std::uint8_t* data, std::size_t size);

bool encodeFrame(MessageType type,
                 std::uint32_t sequence,
                 std::uint32_t sender_monotonic_ms,
                 const std::uint8_t* payload,
                 std::uint16_t payload_size,
                 FrameBuffer& output,
                 std::uint8_t flags = 0,
                 std::uint8_t protocol_minor = kProtocolMinor);

DecodeError decodeFrame(const std::uint8_t* data,
                        std::size_t size,
                        DecodedFrame& output);

bool encodeHello(std::uint32_t sequence,
                 std::uint32_t monotonic_ms,
                 const HelloPayload& payload,
                 FrameBuffer& output);
bool decodeHello(const DecodedFrame& frame, HelloPayload& output);

bool encodeTempSample(std::uint32_t sequence,
                      std::uint32_t monotonic_ms,
                      const TempSamplePayload& payload,
                      FrameBuffer& output);
bool decodeTempSample(const DecodedFrame& frame, TempSamplePayload& output);

bool encodeStatus(std::uint32_t sequence,
                  std::uint32_t monotonic_ms,
                  const StatusPayload& payload,
                  FrameBuffer& output);
bool decodeStatus(const DecodedFrame& frame, StatusPayload& output);

bool encodeFault(std::uint32_t sequence,
                 std::uint32_t monotonic_ms,
                 const FaultPayload& payload,
                 FrameBuffer& output);
bool decodeFault(const DecodedFrame& frame, FaultPayload& output);

bool encodeShutdownNotice(std::uint32_t sequence,
                          std::uint32_t monotonic_ms,
                          const ShutdownNoticePayload& payload,
                          FrameBuffer& output);
bool decodeShutdownNotice(const DecodedFrame& frame, ShutdownNoticePayload& output);

}  // namespace edgefocus::fan::protocol

#include "FanProtocol.h"

#include <cstdlib>
#include <iostream>

using namespace edgefocus::fan::protocol;

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

DecodedFrame decode(const FrameBuffer& frame) {
    DecodedFrame decoded;
    require(decodeFrame(frame.bytes.data(), frame.size, decoded) == DecodeError::None,
            "encoded frame must decode");
    return decoded;
}

void testTypedRoundTrips() {
    FrameBuffer frame;

    const HelloPayload hello{0x55AAu, 0x0123456789ABCDEFull};
    require(encodeHello(10, 20, hello, frame), "HELLO encode");
    auto decoded = decode(frame);
    HelloPayload hello_out;
    require(decoded.sequence == 10 && decoded.sender_monotonic_ms == 20,
            "header values must round-trip");
    require(decodeHello(decoded, hello_out), "HELLO decode");
    require(hello_out.capabilities == hello.capabilities && hello_out.boot_id == hello.boot_id,
            "HELLO payload must round-trip");

    const TempSamplePayload temperature{-1234, 7};
    require(encodeTempSample(11, 21, temperature, frame), "TEMP_SAMPLE encode");
    decoded = decode(frame);
    TempSamplePayload temperature_out;
    require(decodeTempSample(decoded, temperature_out), "TEMP_SAMPLE decode");
    require(temperature_out.temperature_mc == temperature.temperature_mc &&
                temperature_out.thermal_source_id == temperature.thermal_source_id,
            "TEMP_SAMPLE payload must round-trip");

    const StatusPayload status{1, 2, 3, 0, 14000, 3200, 71234, -567, 900,
                               0x12u, 0x34u};
    require(encodeStatus(12, 22, status, frame), "STATUS encode");
    decoded = decode(frame);
    StatusPayload status_out;
    require(decodeStatus(decoded, status_out), "STATUS decode");
    require(status_out.duty_ns == 14000 && status_out.rpm == 3200 &&
                status_out.filtered_temperature_mc == 71234 &&
                status_out.trend_mc == -567 && status_out.latched_faults == 0x34u,
            "STATUS payload must round-trip");

    const FaultPayload fault{1, 3, 99};
    require(encodeFault(13, 23, fault, frame), "FAULT encode");
    decoded = decode(frame);
    FaultPayload fault_out;
    require(decodeFault(decoded, fault_out) && fault_out.first_fault_monotonic_ms == 99,
            "FAULT payload must round-trip");

    const ShutdownNoticePayload shutdown{5};
    require(encodeShutdownNotice(14, 24, shutdown, frame), "SHUTDOWN encode");
    decoded = decode(frame);
    ShutdownNoticePayload shutdown_out;
    require(decodeShutdownNotice(decoded, shutdown_out) && shutdown_out.reason == 5,
            "SHUTDOWN payload must round-trip");
}

void testCorruptionAndCompatibility() {
    FrameBuffer frame;
    const TempSamplePayload temperature{70000, 1};
    require(encodeTempSample(1, 2, temperature, frame), "baseline encode");
    DecodedFrame decoded;

    require(decodeFrame(frame.bytes.data(), 10, decoded) == DecodeError::Truncated,
            "short header must be rejected");

    FrameBuffer bad = frame;
    bad.bytes[0] ^= 0x01u;
    require(decodeFrame(bad.bytes.data(), bad.size, decoded) == DecodeError::BadMagic,
            "bad magic must be rejected");

    bad = frame;
    bad.bytes[4] = kProtocolMajor + 1;
    require(decodeFrame(bad.bytes.data(), bad.size, decoded) ==
                DecodeError::MajorVersionMismatch,
            "major mismatch must be rejected");

    bad = frame;
    bad.bytes[kHeaderSize] ^= 0x80u;
    require(decodeFrame(bad.bytes.data(), bad.size, decoded) == DecodeError::CrcMismatch,
            "payload corruption must be rejected");

    require(decodeFrame(frame.bytes.data(), frame.size - 1, decoded) ==
                DecodeError::LengthMismatch,
            "truncated payload must be rejected");

    require(encodeFrame(MessageType::TempSample, 1, 2, nullptr,
                        static_cast<std::uint16_t>(kMaxPayloadSize + 1), frame) == false,
            "oversized payload must not be encoded");
}

void testKnownCrcVector() {
    const std::uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    require(crc32(data, sizeof(data)) == 0xCBF43926u,
            "CRC32 must match the standard IEEE test vector");
}

}  // namespace

int main() {
    testTypedRoundTrips();
    testCorruptionAndCompatibility();
    testKnownCrcVector();
    std::cout << "fan protocol tests passed\n";
    return 0;
}

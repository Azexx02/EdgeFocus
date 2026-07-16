#include "FanProtocol.h"
#include "FrameStreamDecoder.h"
#include "ThermalSource.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace edgefocus::fan::protocol;
using edgefocus::fan::bridge::FrameStreamDecoder;
using edgefocus::fan::bridge::ThermalSource;

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void writeText(const std::filesystem::path& path, const std::string& value) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    output << value;
}

void testExactThermalDiscovery() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("edgefocus-fan-thermal-" + std::to_string(unique));
    writeText(root / "thermal_zone0" / "type", "soc-thermal\n");
    writeText(root / "thermal_zone0" / "temp", "44000\n");
    writeText(root / "thermal_zone1" / "type", "cpu-thermal\n");
    writeText(root / "thermal_zone1" / "temp", "45123\n");

    ThermalSource source(root, "cpu-thermal");
    std::string error;
    require(source.discover(error), "exact cpu-thermal zone must be discovered");
    std::int32_t temperature = 0;
    require(source.read(temperature, error) && temperature == 45123,
            "thermal source must read millidegrees");

    writeText(root / "thermal_zone2" / "type", "cpu-thermal\n");
    writeText(root / "thermal_zone2" / "temp", "46000\n");
    ThermalSource ambiguous(root, "cpu-thermal");
    require(!ambiguous.discover(error), "duplicate cpu-thermal zones must be rejected");

    ThermalSource missing(root, "gpu-thermal");
    require(!missing.discover(error), "missing requested type must not fall back");
    std::filesystem::remove_all(root);
}

void testStreamChunkingConcatenationAndResync() {
    FrameBuffer hello;
    FrameBuffer temperature;
    require(encodeHello(1, 10, {CapabilityM33Controller, 42}, hello), "HELLO encode");
    require(encodeTempSample(2, 20, {70000, 1}, temperature), "TEMP encode");

    std::vector<std::uint8_t> stream{0x99, 0x88, 0x77};
    stream.insert(stream.end(), hello.bytes.begin(), hello.bytes.begin() + hello.size);
    stream.insert(stream.end(), temperature.bytes.begin(),
                  temperature.bytes.begin() + temperature.size);

    FrameStreamDecoder decoder;
    decoder.append(stream.data(), 7);
    FrameBuffer output;
    require(!decoder.pop(output), "partial header must wait for more data");
    decoder.append(stream.data() + 7, stream.size() - 7);
    require(decoder.pop(output), "first complete frame must be emitted");
    DecodedFrame decoded;
    require(decodeFrame(output.bytes.data(), output.size, decoded) == DecodeError::None &&
                decoded.type == MessageType::Hello,
            "first decoded frame must be HELLO");
    require(decoder.pop(output), "concatenated second frame must be emitted");
    require(decodeFrame(output.bytes.data(), output.size, decoded) == DecodeError::None &&
                decoded.type == MessageType::TempSample,
            "second decoded frame must be TEMP_SAMPLE");
    require(decoder.droppedBytes() >= 3, "leading garbage must be accounted for");

    FrameBuffer corrupted = hello;
    corrupted.bytes[kHeaderSize] ^= 0x01u;
    decoder.reset();
    decoder.append(corrupted.bytes.data(), corrupted.size);
    decoder.append(temperature.bytes.data(), temperature.size);
    require(decoder.pop(output), "decoder must resynchronize after a corrupt frame");
    require(decodeFrame(output.bytes.data(), output.size, decoded) == DecodeError::None &&
                decoded.type == MessageType::TempSample,
            "resynchronized frame must be the following valid frame");
}

}  // namespace

int main() {
    testExactThermalDiscovery();
    testStreamChunkingConcatenationAndResync();
    std::cout << "fan bridge tests passed\n";
    return 0;
}

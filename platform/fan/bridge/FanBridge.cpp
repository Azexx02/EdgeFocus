#include "FanProtocol.h"
#include "RpmsgTransport.h"
#include "ThermalSource.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

using namespace edgefocus::fan::protocol;
using edgefocus::fan::bridge::RpmsgTransport;
using edgefocus::fan::bridge::ThermalSource;

std::atomic<bool> running{true};

void stopHandler(int) {
    running.store(false);
}

std::uint32_t monotonicMs() {
    using namespace std::chrono;
    return static_cast<std::uint32_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

std::string jsonEscape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped += character; break;
        }
    }
    return escaped;
}

const char* stateName(std::uint8_t state) {
    switch (state) {
    case 0: return "BOOT_SAFE";
    case 1: return "CONTROLLED";
    case 2: return "FAIL_SAFE";
    default: return "UNKNOWN";
    }
}

const char* modeName(std::uint8_t mode) {
    switch (mode) {
    case 0: return "NORMAL";
    case 1: return "BOOST_RAMP";
    case 2: return "BOOST_HOLD";
    case 3: return "BOOST_CRITICAL";
    default: return "UNKNOWN";
    }
}

struct BridgeStatus {
    bool thermal_ready{false};
    bool transport_connected{false};
    bool protocol_ready{false};
    std::string thermal_type{"cpu-thermal"};
    std::string endpoint;
    std::string last_error;
    std::int32_t linux_temperature_mc{0};
    std::uint64_t m33_boot_id{0};
    std::uint32_t last_status_rx_ms{0};
    StatusPayload controller{};
};

bool writeStatusAtomic(const std::filesystem::path& destination,
                       const BridgeStatus& status,
                       std::string& error) {
    std::error_code filesystem_error;
    std::filesystem::create_directories(destination.parent_path(), filesystem_error);
    if (filesystem_error) {
        error = "cannot create status directory: " + filesystem_error.message();
        return false;
    }

    const std::filesystem::path temporary = destination.string() + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) {
        error = "cannot open temporary status file: " + temporary.string();
        return false;
    }
    output << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"thermal_ready\": " << (status.thermal_ready ? "true" : "false") << ",\n"
           << "  \"transport_connected\": "
           << (status.transport_connected ? "true" : "false") << ",\n"
           << "  \"protocol_ready\": " << (status.protocol_ready ? "true" : "false") << ",\n"
           << "  \"thermal_type\": \"" << jsonEscape(status.thermal_type) << "\",\n"
           << "  \"endpoint\": \"" << jsonEscape(status.endpoint) << "\",\n"
           << "  \"linux_temperature_mc\": " << status.linux_temperature_mc << ",\n"
           << "  \"m33_boot_id\": " << status.m33_boot_id << ",\n"
           << "  \"last_status_rx_ms\": " << status.last_status_rx_ms << ",\n"
           << "  \"control_state\": \"" << stateName(status.controller.control_state) << "\",\n"
           << "  \"cooling_mode\": \"" << modeName(status.controller.cooling_mode) << "\",\n"
           << "  \"curve_band\": " << static_cast<unsigned>(status.controller.curve_band) << ",\n"
           << "  \"duty_ns\": " << status.controller.duty_ns << ",\n"
           << "  \"rpm\": " << status.controller.rpm << ",\n"
           << "  \"filtered_temperature_mc\": "
           << status.controller.filtered_temperature_mc << ",\n"
           << "  \"trend_mc\": " << status.controller.trend_mc << ",\n"
           << "  \"sample_age_ms\": " << status.controller.sample_age_ms << ",\n"
           << "  \"current_faults\": " << status.controller.current_faults << ",\n"
           << "  \"latched_faults\": " << status.controller.latched_faults << ",\n"
           << "  \"last_error\": \"" << jsonEscape(status.last_error) << "\"\n"
           << "}\n";
    output.close();
    if (!output) {
        error = "cannot flush temporary status file";
        return false;
    }
    std::filesystem::rename(temporary, destination, filesystem_error);
    if (filesystem_error) {
        std::filesystem::remove(destination, filesystem_error);
        filesystem_error.clear();
        std::filesystem::rename(temporary, destination, filesystem_error);
    }
    if (filesystem_error) {
        error = "cannot publish status file: " + filesystem_error.message();
        return false;
    }
    return true;
}

struct Options {
    std::string endpoint{"/dev/ttyRPMSG0"};
    std::filesystem::path thermal_root{"/sys/class/thermal"};
    std::filesystem::path status_file{"/run/edgefocus/fan-status.json"};
};

bool parseOptions(int argc, char** argv, Options& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (index + 1 >= argc) {
            std::cerr << "missing value for " << argument << '\n';
            return false;
        }
        const std::string value = argv[++index];
        if (argument == "--endpoint") {
            options.endpoint = value;
        } else if (argument == "--thermal-root") {
            options.thermal_root = value;
        } else if (argument == "--status-file") {
            options.status_file = value;
        } else {
            std::cerr << "unknown argument: " << argument << '\n';
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseOptions(argc, argv, options)) {
        return 2;
    }
    std::signal(SIGINT, stopHandler);
    std::signal(SIGTERM, stopHandler);

    ThermalSource thermal(options.thermal_root, "cpu-thermal");
    RpmsgTransport transport(options.endpoint);
    BridgeStatus status;
    status.endpoint = options.endpoint;

    const std::uint64_t bridge_boot_id =
        (static_cast<std::uint64_t>(monotonicMs()) << 32u) ^
        static_cast<std::uint64_t>(::getpid());
    std::uint32_t tx_sequence = 1;
    std::uint32_t next_thermal_discovery_ms = 0;
    std::uint32_t next_connect_ms = 0;
    std::uint32_t next_temperature_ms = 0;
    std::uint32_t next_status_write_ms = 0;
    std::uint32_t hello_deadline_ms = 0;

    while (running.load()) {
        const std::uint32_t now = monotonicMs();

        if (!thermal.ready() && static_cast<std::int32_t>(now - next_thermal_discovery_ms) >= 0) {
            std::string error;
            status.thermal_ready = thermal.discover(error);
            status.last_error = status.thermal_ready ? "" : error;
            next_thermal_discovery_ms = now + 5000;
        }

        if (!transport.isOpen() && static_cast<std::int32_t>(now - next_connect_ms) >= 0) {
            std::string error;
            if (transport.openDevice(error)) {
                status.transport_connected = true;
                status.protocol_ready = false;
                HelloPayload hello{CapabilityLinuxBridge, bridge_boot_id};
                FrameBuffer frame;
                if (!encodeHello(tx_sequence++, now, hello, frame) ||
                    !transport.send(frame, error)) {
                    status.last_error = error.empty() ? "cannot encode HELLO" : error;
                    transport.closeDevice();
                    status.transport_connected = false;
                } else {
                    hello_deadline_ms = now + 3000;
                }
            } else {
                status.last_error = error;
            }
            next_connect_ms = now + 1000;
        }

        if (transport.isOpen()) {
            for (;;) {
                FrameBuffer received;
                std::string error;
                if (!transport.receive(received, error)) {
                    if (!error.empty()) {
                        status.last_error = error;
                        transport.closeDevice();
                        status.transport_connected = false;
                        status.protocol_ready = false;
                    }
                    break;
                }
                DecodedFrame decoded;
                const DecodeError decode_error =
                    decodeFrame(received.bytes.data(), received.size, decoded);
                if (decode_error != DecodeError::None) {
                    status.last_error = "invalid RPMsg frame";
                    continue;
                }
                if (decoded.type == MessageType::Hello) {
                    HelloPayload hello;
                    if (decodeHello(decoded, hello) &&
                        (hello.capabilities & CapabilityM33Controller) != 0) {
                        status.protocol_ready = true;
                        status.m33_boot_id = hello.boot_id;
                        status.last_error.clear();
                        next_temperature_ms = now;
                    }
                } else if (decoded.type == MessageType::Status) {
                    StatusPayload controller;
                    if (decodeStatus(decoded, controller)) {
                        status.controller = controller;
                        status.last_status_rx_ms = now;
                    }
                } else if (decoded.type == MessageType::Fault) {
                    FaultPayload fault;
                    if (decodeFault(decoded, fault)) {
                        status.controller.current_faults = fault.current_faults;
                        status.controller.latched_faults = fault.latched_faults;
                    }
                }
            }

            if (transport.isOpen() && !status.protocol_ready &&
                static_cast<std::int32_t>(now - hello_deadline_ms) >= 0) {
                status.last_error = "M33 controller HELLO timeout";
                transport.closeDevice();
                status.transport_connected = false;
            }

            if (transport.isOpen() && status.protocol_ready && thermal.ready() &&
                static_cast<std::int32_t>(now - next_temperature_ms) >= 0) {
                std::int32_t temperature_mc = 0;
                std::string error;
                if (thermal.read(temperature_mc, error)) {
                    status.thermal_ready = true;
                    status.linux_temperature_mc = temperature_mc;
                    TempSamplePayload temperature{temperature_mc, 1};
                    FrameBuffer frame;
                    if (!encodeTempSample(tx_sequence++, now, temperature, frame) ||
                        !transport.send(frame, error)) {
                        status.last_error = error.empty() ? "cannot encode TEMP_SAMPLE" : error;
                        transport.closeDevice();
                        status.transport_connected = false;
                        status.protocol_ready = false;
                    } else {
                        status.last_error.clear();
                    }
                } else {
                    status.last_error = error;
                    status.thermal_ready = false;
                    thermal.invalidate();
                }
                next_temperature_ms = now + 1000;
            }
        }

        if (static_cast<std::int32_t>(now - next_status_write_ms) >= 0) {
            std::string error;
            if (!writeStatusAtomic(options.status_file, status, error)) {
                std::cerr << error << '\n';
            }
            next_status_write_ms = now + 1000;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (transport.isOpen()) {
        FrameBuffer frame;
        const ShutdownNoticePayload shutdown{0};
        std::string ignored;
        if (encodeShutdownNotice(tx_sequence++, monotonicMs(), shutdown, frame)) {
            transport.send(frame, ignored);
        }
    }
    return 0;
}

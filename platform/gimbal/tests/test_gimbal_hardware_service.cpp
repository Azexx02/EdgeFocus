#include "platform/gimbal/GimbalHardwareService.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <deque>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace edgefocus::gimbal;

namespace {

class FakeIo final : public IGimbalIo {
public:
    bool open(std::string& error) override {
        recordThread();
        std::lock_guard<std::mutex> lock(mutex_);
        opened_ = open_ok;
        error = open_ok ? "" : "open_failed";
        return open_ok;
    }

    void close() noexcept override {
        recordThread();
        std::lock_guard<std::mutex> lock(mutex_);
        opened_ = false;
    }

    bool isOpen() const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        return opened_;
    }

    ControlMode controlMode() const noexcept override { return mode; }

    bool setBoot0(bool high, std::string& error) override {
        recordThread();
        if (high) {
            error = "boot0_high_forbidden";
            return false;
        }
        return true;
    }

    bool setEnable(bool high, std::string& error) override {
        recordThread();
        std::lock_guard<std::mutex> lock(mutex_);
        enable_ = high;
        enable_history.push_back(high);
        error.clear();
        return true;
    }

    bool readFaultActive(bool& active, std::string& error) override {
        recordThread();
        std::lock_guard<std::mutex> lock(mutex_);
        active = fault_active;
        error.clear();
        return true;
    }

    bool writeEmergencyStop(std::string& error) override {
        recordThread();
        std::lock_guard<std::mutex> lock(mutex_);
        ++emergency_count;
        state_ = "DISABLED";
        enable_ = false;
        error.clear();
        return true;
    }

    bool writeLine(const std::string& line, std::string& error) override {
        recordThread();
        std::lock_guard<std::mutex> lock(mutex_);
        writes.push_back(line);
        error.clear();
        if (line == "PING") {
            reads_.push_back("OK,G431\n");
        } else if (line == "VER") {
            if (protocol_version == "3.0") {
                reads_.push_back(
                    "VER,PROTO=3.0,FW=G431-GIMBAL,"
                    "CAPS=ASYNC_MOTION|POSE|ORIGIN|FAULT_CLASS|HOLD_NOW|NVM_V3|MAINT_ASSIST_DIAG|FULL_RANGE_POSITION|NEGOTIATION_REQUIRED\n");
            } else {
                reads_.push_back("VER,PROTO=2.0,CAPS=UART_ONLY|ARM|TRACK\n");
            }
        } else if (line == "HELLO HOST PROTO=3.0") {
            reads_.push_back("OK,HELLO,PROTO=3.0\n");
        } else if (line == "ARM" || line == "ARM MAINTENANCE") {
            state_ = "READY";
            arm_profile_ = line == "ARM" ? "PRODUCTION" : "MAINTENANCE";
            reads_.push_back("OK,ARM,STATE=READY,CAL_EPOCH=7,ORIGIN_EPOCH=9\n");
        } else if (line == "HOLD NOW") {
            state_ = "HOLDING";
            ++generation_;
            reads_.push_back("OK,HOLD_NOW,GEN=" + std::to_string(generation_) +
                             ",YAW10=" + std::to_string(yaw10_) +
                             ",PITCH10=" + std::to_string(pitch10_) + "\n");
        } else if (line.rfind("TRACK YP ", 0) == 0) {
            state_ = "MOVING";
            std::istringstream stream(line.substr(9));
            stream >> yaw10_ >> pitch10_;
            ++generation_;
            if (!next_track_error.empty()) {
                reads_.push_back(next_track_error + "\n");
                next_track_error.clear();
            } else if (!suppress_track_response) {
                reads_.push_back("TRACK,ACCEPTED,GEN=" + std::to_string(generation_) +
                                 ",YAW10=" + std::to_string(yaw10_) +
                                 ",PITCH10=" + std::to_string(pitch10_) + "\n");
            }
        } else if (line == "POSE") {
            if (!pose_overrides.empty()) {
                reads_.push_back(pose_overrides.front());
                pose_overrides.pop_front();
            } else {
                reads_.push_back(poseLine(true));
            }
        } else if (line == "FAULT") {
            reads_.push_back("FAULT,STATE=" + state_ +
                             ",ACTIVE=0,BOOST=0,MOTOR=0,HOLD=0,UARTERR=0,RXDROP=0,TXDROP=0\n");
        } else if (line == "NVM STATUS") {
            reads_.push_back("NVM,SCHEMA=3,ACTIVE_SLOT=0,GEN=3,A=VALID,B=EMPTY,"
                             "FLAGS=7,CAL_EPOCH=7,ORIGIN_EPOCH=9\n");
        } else if (line == "STAT") {
            reads_.push_back("STAT,FAULT=0,EN=0,BOOT0=0\n");
        } else if (line == "ANGLE") {
            reads_.push_back("ANGLE,Y_DEG=0,P_DEG=0\n");
        } else if (line == "STOP") {
            state_ = "DISABLED";
            arm_profile_ = "NONE";
            reads_.push_back("OK,STOP,STATE=DISABLED,BOOST=0,MOTOR=0,HOLD=0\n");
        } else if (line == "FAULT CLEAR") {
            state_ = "DISABLED";
            reads_.push_back("OK,FAULT_CLEAR,STATE=DISABLED\n");
        } else if (line == "CAL ELECTRICAL ALL") {
            reads_.push_back("CAL,ACCEPTED,AXIS=ALL,OP_GEN=12\n");
        } else if (line == "ORIGIN CAPTURE") {
            reads_.push_back("OK,ORIGIN_CAPTURE,ORIGIN_EPOCH=10\n");
        } else if (line == "CURRENT RESET") {
            reads_.push_back("OK,CURRENT_RESET\n");
        } else if (line == "CURRENT") {
            reads_.push_back(
                "CURRENT,P_RAW=101,P_AVG=82,P_PEAK=130,Y_RAW=99,Y_AVG=77,"
                "Y_PEAK=125,WINDOW_MS=1200,SAMPLES=120,VALID=1,SATURATED=0,"
                "SAMPLE_AGE_MS=3\n");
        } else if (line == "MAINTASSIST?") {
            reads_.push_back(
                "MAINTASSIST,GEN=" + std::to_string(generation_) +
                ",ATTEMPTED=1,EXHAUSTED=0,ACTIVE_MS=85,PEAK_FLOOR=137\n");
        } else if (line.rfind("PROTECT CURRENT SET ", 0) == 0) {
            reads_.push_back("OK,PROTECT_CURRENT_SET\n");
        }
        return true;
    }

    bool readBytes(std::string& bytes, int, std::string& error) override {
        recordThread();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            error.clear();
            bytes.clear();
            if (!reads_.empty()) {
                bytes = reads_.front();
                reads_.pop_front();
                if (read_chunk_bytes > 0 && bytes.size() > read_chunk_bytes) {
                    const auto remainder = bytes.substr(read_chunk_bytes);
                    bytes.resize(read_chunk_bytes);
                    reads_.push_front(remainder);
                }
                return true;
            }
        }
        if (empty_read_delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(empty_read_delay_ms));
        }
        return true;
    }

    std::string poseLine(bool settled) const {
        return "POSE,STATE=" + state_ +
               ",ARM_PROFILE=" + arm_profile_ +
               ",P_RAW=1,Y_RAW=2,P_RAW_VALID=1,Y_RAW_VALID=1,"
               "YAW10=" + std::to_string(yaw10_) +
               ",PITCH10=" + std::to_string(pitch10_) +
               ",TYAW10=" + std::to_string(yaw10_) +
               ",TPITCH10=" + std::to_string(pitch10_) +
               ",CURRENT_VALID=1,TARGET_VALID=1,GEN=" + std::to_string(generation_) +
               ",SETTLED=" + (settled ? "1" : "0") +
               ",CAL_EPOCH=7,ORIGIN_EPOCH=9,CAL_VALID=1,ORIGIN_VALID=1,"
               "CURRENT_PROTECTION=VALID,SAMPLE_AGE_MS=2,FAULT=NONE,OP=NONE,"
               "OP_GEN=0,OP_PROGRESS=0,LAST_OP_STATUS=OK\n";
    }

    std::set<std::thread::id> ioThreads() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return io_threads_;
    }

    bool enabled() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return enable_;
    }

    std::vector<std::string> writesCopy() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return writes;
    }

    bool open_ok{true};
    bool fault_active{};
    ControlMode mode{ControlMode::UartOnly};
    std::string protocol_version{"3.0"};
    bool suppress_track_response{};
    std::string next_track_error;
    std::size_t read_chunk_bytes{};
    int empty_read_delay_ms{};
    std::deque<std::string> pose_overrides;
    std::vector<bool> enable_history;
    std::vector<std::string> writes;
    int emergency_count{};

private:
    void recordThread() const {
        std::lock_guard<std::mutex> lock(mutex_);
        io_threads_.insert(std::this_thread::get_id());
    }

    mutable std::mutex mutex_;
    mutable std::set<std::thread::id> io_threads_;
    bool opened_{};
    bool enable_{};
    std::string arm_profile_{"NONE"};
    std::string state_{"DISABLED"};
    unsigned generation_{};
    unsigned yaw10_{};
    unsigned pitch10_{};
    std::deque<std::string> reads_;
};

GimbalHardwareService::MotionLimits limits() {
    GimbalHardwareService::MotionLimits result;
    result.yaw_min_offset_deg = -70.0;
    result.yaw_max_offset_deg = 70.0;
    result.pitch_min_offset_deg = -70.0;
    result.pitch_max_offset_deg = 70.0;
    return result;
}

void testProtocol3AckAndSettled() {
    std::int64_t now = 1000;
    auto fake = std::make_unique<FakeIo>();
    auto* io = fake.get();
    io->read_chunk_bytes = 17;
    GimbalHardwareService service(
        std::move(fake), limits(), [&] { return ++now; },
        [&](std::chrono::milliseconds delay) { now += delay.count(); });
    std::string error;
    assert(service.start(&error));
    auto result = service.enableAndAlign();
    if (!result.success) {
        std::cerr << "enable failed: " << result.error << " class="
                  << errorClassName(result.error_class) << " code="
                  << result.error_code << "\n";
        for (const auto& line : result.transcript) std::cerr << "  " << line << "\n";
    }
    assert(result.success && result.firmware_accepted);
    auto snapshot = service.snapshot();
    assert(snapshot.protocol_version == "3.0");
    assert(snapshot.compatibility == ProtocolCompatibility::Protocol3Control);
    assert(snapshot.calibration_epoch == 7 && snapshot.origin_epoch == 9);
    assert(snapshot.current_protection_state == "VALID");
    assert(snapshot.yaw_raw_valid.value_or(false));
    assert(snapshot.pitch_raw_valid.value_or(false));

    result = service.moveTo(10.0, -5.0, 500);
    assert(result.success && result.firmware_accepted && !result.settled);
    assert(result.motion_generation == 1);
    assert(service.status().state == State::Moving);

    io->pose_overrides.push_back(
        "POSE,STATE=HOLDING,YAW10=100,PITCH10=3550,TYAW10=100,TPITCH10=3550,"
        "GEN=1,SETTLED=1,CAL_EPOCH=7,ORIGIN_EPOCH=9,CAL_VALID=1,ORIGIN_VALID=1,"
        "CURRENT_PROTECTION=VALID,SAMPLE_AGE_MS=2,FAULT=NONE\n");
    io->pose_overrides.push_back(
        "POSE,STATE=MOVING,YAW10=120,PITCH10=30,TYAW10=120,TPITCH10=30,"
        "GEN=2,SETTLED=0,CAL_EPOCH=7,ORIGIN_EPOCH=9,CAL_VALID=1,ORIGIN_VALID=1,"
        "CURRENT_PROTECTION=VALID,SAMPLE_AGE_MS=2,FAULT=NONE\n");
    io->pose_overrides.push_back(
        "POSE,STATE=HOLDING,YAW10=120,PITCH10=30,TYAW10=120,TPITCH10=30,"
        "GEN=2,SETTLED=1,CAL_EPOCH=7,ORIGIN_EPOCH=9,CAL_VALID=1,ORIGIN_VALID=1,"
        "CURRENT_PROTECTION=VALID,SAMPLE_AGE_MS=2,FAULT=NONE\n");
    result = service.moveToVerified(12.0, 3.0, 2000, 1.0);
    assert(result.success && result.settled && result.motion_generation == 2);
    assert(service.status().state == State::Holding);

    result = service.hold();
    assert(result.success && result.settled);
    assert(service.stopAndDisable().success);
    assert(service.status().state == State::Disabled);

    const auto writes = io->writesCopy();
    assert(std::find(writes.begin(), writes.end(), "HELLO HOST PROTO=3.0") != writes.end());
    assert(std::find(writes.begin(), writes.end(), "HOLD NOW") != writes.end());
    assert(std::none_of(writes.begin(), writes.end(), [](const std::string& command) {
        return command.rfind("SET ", 0) == 0 || command == "HOLD 1";
    }));
    assert(io->ioThreads().size() == 1);
    assert(io->ioThreads().count(std::this_thread::get_id()) == 0);
    service.stop();
}

void testProtocol2ReadOnlyDowngrade() {
    std::int64_t now = 100;
    auto fake = std::make_unique<FakeIo>();
    auto* io = fake.get();
    io->protocol_version = "2.0";
    GimbalHardwareService service(
        std::move(fake), limits(), [&] { return ++now; },
        [&](std::chrono::milliseconds delay) { now += delay.count(); });
    std::string error;
    assert(service.start(&error));
    auto diagnostic = service.probeDiagnostics();
    assert(diagnostic.success);
    const auto snapshot = service.snapshot();
    assert(snapshot.compatibility == ProtocolCompatibility::Protocol2DiagnosticOnly);
    assert(!snapshot.available && snapshot.degraded_reason == "protocol2_diagnostic_only");
    auto result = service.enableAndAlign();
    assert(!result.success && result.error_code == "PROTOCOL2_READ_ONLY");
    result = service.moveTo(1.0, 1.0);
    assert(!result.success && result.error_code == "MOTION_UNAVAILABLE");
    const auto writes = io->writesCopy();
    assert(std::find(writes.begin(), writes.end(), "ARM") == writes.end());
    assert(std::none_of(writes.begin(), writes.end(), [](const std::string& command) {
        return command.rfind("TRACK ", 0) == 0;
    }));
    service.stop();
}

void testCommandErrorDoesNotLatchHardwareFault() {
    std::int64_t now = 100;
    auto fake = std::make_unique<FakeIo>();
    auto* io = fake.get();
    GimbalHardwareService service(
        std::move(fake), limits(), [&] { return ++now; },
        [&](std::chrono::milliseconds delay) { now += delay.count(); });
    std::string error;
    assert(service.start(&error));
    assert(service.enableAndAlign().success);
    io->next_track_error = "ERR,CLASS=COMMAND,CODE=INVALID_STATE";
    const auto result = service.moveTo(5.0, 5.0);
    assert(!result.success);
    assert(result.error_class == ErrorClass::Command);
    assert(result.error_code == "INVALID_STATE");
    assert(service.status().state != State::Fault);
    service.stop();
}

void testEmergencySlotInterruptsWait() {
    auto fake = std::make_unique<FakeIo>();
    auto* io = fake.get();
    io->empty_read_delay_ms = 5;
    GimbalHardwareService service(std::move(fake), limits());
    std::string error;
    assert(service.start(&error));
    assert(service.enableAndAlign().success);
    io->suppress_track_response = true;
    auto moving = std::async(std::launch::async, [&] {
        return service.moveTo(5.0, 5.0, 2000);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    service.requestEmergencyStop();
    const auto result = moving.get();
    assert(!result.success && result.error_code == "EMERGENCY_STOP");
    assert(io->emergency_count >= 1);
    assert(!io->enabled());
    assert(service.snapshot().degraded_reason == "emergency_stop_resync_required");
    service.stop();
}

void testTypedMaintenanceCommandsAndTelemetry() {
    std::int64_t now = 100;
    auto fake = std::make_unique<FakeIo>();
    auto* io = fake.get();
    GimbalHardwareService service(
        std::move(fake), limits(), [&] { return ++now; },
        [&](std::chrono::milliseconds delay) { now += delay.count(); });
    std::string error;
    assert(service.start(&error));
    auto result = service.executeMaintenance({MaintenanceOperation::Probe});
    assert(result.success);
    result = service.executeMaintenance({MaintenanceOperation::CalibrateElectricalAll});
    assert(result.success && result.firmware_accepted);
    assert(result.motion_generation == 12);

    result = service.executeMaintenance({MaintenanceOperation::CaptureOrigin});
    assert(result.success && service.snapshot().origin_epoch == 10);

    result = service.executeMaintenance({MaintenanceOperation::ArmMaintenance});
    assert(result.success && result.firmware_accepted);

    result = service.executeMaintenance({MaintenanceOperation::CurrentReset});
    assert(result.success);
    result = service.executeMaintenance({MaintenanceOperation::Hold});
    assert(result.success && result.settled);
    result = service.moveToMaintenanceVerified(11.0, 0.0, 2000, 2.0);
    assert(!result.success && result.error_code == "TARGET_OUT_OF_BOUNDS");
    result = service.moveToMaintenanceVerified(1.0, 0.0, 2000, 2.0, true);
    assert(!result.success &&
           result.error_code == "AUTO_CENTER_TARGET_INVALID");
    const std::string stable_maintenance_pose =
        "POSE,STATE=MOVING,ARM_PROFILE=MAINTENANCE,YAW10=30,PITCH10=0,"
        "TYAW10=30,TPITCH10=0,GEN=2,SETTLED=0,CAL_EPOCH=7,ORIGIN_EPOCH=9,"
        "CAL_VALID=1,ORIGIN_VALID=1,CURRENT_PROTECTION=VALID,SAMPLE_AGE_MS=2,"
        "FAULT=NONE,OP=NONE,OP_GEN=0,OP_PROGRESS=0,LAST_OP_STATUS=OK\n";
    for (int i = 0; i < 3; ++i) io->pose_overrides.push_back(stable_maintenance_pose);
    result = service.moveToMaintenanceVerified(3.0, 0.0, 2000, 2.0);
    assert(result.success && !result.settled && result.firmware_accepted);
    assert(service.snapshot().arm_profile == ArmProfile::Maintenance);
    result = service.executeMaintenance({MaintenanceOperation::VerifyMotionPattern});
    assert(result.success && result.settled);
    result = service.executeMaintenance({MaintenanceOperation::CurrentRead});
    assert(result.success && result.current);
    assert(result.current->valid && !result.current->saturated);
    assert(result.current->window_ms == 1200 && result.current->samples == 120);
    result = service.executeMaintenance({MaintenanceOperation::AssistRead});
    assert(result.success && result.maintenance_assist);
    assert(result.maintenance_assist->target_generation == 8);
    assert(result.maintenance_assist->attempted);
    assert(!result.maintenance_assist->exhausted);
    assert(result.maintenance_assist->active_ms == 85);
    assert(result.maintenance_assist->peak_floor == 137);

    MaintenanceRequest protection{MaintenanceOperation::ProtectCurrentSet};
    protection.protection = CurrentProtectionConfig{130, 82, 125, 77, 250};
    result = service.executeMaintenance(protection);
    assert(result.success && result.firmware_accepted);
    assert(service.snapshot().current_protection_state == "CONFIGURED");

    const auto writes = io->writesCopy();
    assert(std::find(writes.begin(), writes.end(), "ARM MAINTENANCE") != writes.end());
    assert(std::find(writes.begin(), writes.end(), "CAL ELECTRICAL ALL") != writes.end());
    assert(std::find(writes.begin(), writes.end(), "CURRENT") != writes.end());
    assert(std::find(writes.begin(), writes.end(), "MAINTASSIST?") != writes.end());
    assert(service.stopAndDisable().success);
    assert(service.snapshot().arm_profile == ArmProfile::None);
    assert(service.executeMaintenance({MaintenanceOperation::ArmProduction}).success);
    assert(service.snapshot().arm_profile == ArmProfile::Production);
    assert(service.stopAndDisable().success);
    const auto final_writes = io->writesCopy();
    assert(std::find(final_writes.begin(), final_writes.end(), "ARM") !=
           final_writes.end());
    service.stop();
}

}  // namespace

int main() {
    testProtocol3AckAndSettled();
    testProtocol2ReadOnlyDowngrade();
    testCommandErrorDoesNotLatchHardwareFault();
    testEmergencySlotInterruptsWait();
    testTypedMaintenanceCommandsAndTelemetry();
    std::cout << "gimbal protocol 3 hardware service tests passed\n";
    return 0;
}

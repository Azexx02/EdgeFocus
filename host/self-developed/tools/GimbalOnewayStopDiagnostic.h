#pragma once

#include "platform/gimbal/IGimbalIo.h"

#include <string>
#include <vector>

namespace edgefocus::gimbal::diagnostic {

struct OnewayStopTiming {
    int safe_low_ms{500};
    int alignment_observation_ms{10000};
    int hold_observation_ms{500};
    int pitch_observation_ms{1000};
    int stop_observation_ms{1000};
    int poll_ms{25};
};

struct OnewayStopResult {
    bool sequence_completed{false};
    bool fault_active{false};
    std::string error;
    std::vector<std::string> commands_sent;
    std::string uart_transcript;
};

// Deliberately isolated from GimbalHardwareService: this diagnostic is the
// only path allowed to continue after PING without a UART reply. The optional
// PITCH 5 test is separately guarded by the command-line tool; neither path
// sends tracking or multi-turn commands.
class GimbalOnewayStopDiagnostic {
public:
    explicit GimbalOnewayStopDiagnostic(IGimbalIo& io,
                                        OnewayStopTiming timing = {});

    OnewayStopResult run();
    OnewayStopResult runPitchFive();

private:
    OnewayStopResult runSequence(bool include_pitch_five);
    bool observe(int duration_ms, OnewayStopResult& result);
    bool send(const char* command, OnewayStopResult& result);

    IGimbalIo& io_;
    OnewayStopTiming timing_;
};

}  // namespace edgefocus::gimbal::diagnostic

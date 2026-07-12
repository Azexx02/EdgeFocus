#pragma once

#include "SystemStateStore.h"
#include "storage/SQLiteStore.h"
#include "storage/ReportGenerator.h"
#include <string>

namespace edgefocus {

class SessionController {
public:
    struct StartResult {
        bool success = false;
        bool already_active = false;
        SessionInfo session;
        std::string message;
    };

    struct EndResult {
        bool success = false;
        SessionReport report;
        int64_t target_duration_ms = 0;
        std::string message;
    };

    SessionController(SystemStateStore& store, SQLiteStore& sqlite_store);

    StartResult startSession(int target_duration_min, const std::string& source);
    bool skipPrecheck(const std::string& reason);
    bool pauseSession(const std::string& reason);
    bool resumeSession(const std::string& reason);
    EndResult endSession(const std::string& reason);
    int closeInterruptedSessions();

private:
    SystemStateStore& store_;
    SQLiteStore& sqlite_store_;
};

} // namespace edgefocus

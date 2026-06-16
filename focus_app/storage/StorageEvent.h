#pragma once
#include <cstdint>
#include <string>

namespace edgefocus {

enum class StorageEventType {
    SessionStart,
    SessionEnd,
    StateChange,
    ReminderIssued
};

struct StorageEvent {
    StorageEventType type;
    std::string session_id;
    int64_t timestamp;
    std::string data;
};

} // namespace edgefocus

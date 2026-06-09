#pragma once
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
    int timestamp;
    std::string data;
};

} // namespace edgefocus

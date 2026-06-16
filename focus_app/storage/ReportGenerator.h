#pragma once
#include <string>
#include <cstdint>

namespace edgefocus {

class SQLiteStore;

struct SessionReport {
    std::string session_id;
    int64_t total_duration = 0;
    int64_t focus_duration = 0;
    int distraction_count = 0;
    int reminder_count = 0;
    int64_t longest_focus_streak_ms = 0;
    int phone_distraction_count = 0;
};

class ReportGenerator {
private:
    SQLiteStore& sqlite_store_;
public:
    explicit ReportGenerator(SQLiteStore& store);
    
    SessionReport generateReport(const std::string& session_id);
};

} // namespace edgefocus

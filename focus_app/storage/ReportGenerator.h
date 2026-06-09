#pragma once
#include <string>

namespace edgefocus {

struct SessionReport {
    std::string session_id;
    int total_duration = 0;
    int focus_duration = 0;
    int distraction_count = 0;
    int reminder_count = 0;
};

class ReportGenerator {
private:
    void* db_;
public:
    explicit ReportGenerator(const std::string& dbPath = "edgefocus.db");
    ~ReportGenerator();
    
    SessionReport generateReport(const std::string& session_id);
};

} // namespace edgefocus

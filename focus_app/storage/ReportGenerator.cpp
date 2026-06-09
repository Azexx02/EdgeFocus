#include "ReportGenerator.h"
#include "sqlite3.h"
#include <iostream>

namespace edgefocus {

ReportGenerator::ReportGenerator(const std::string& dbPath) : db_(nullptr) {
    sqlite3_open(dbPath.c_str(), reinterpret_cast<sqlite3**>(&db_));
}

ReportGenerator::~ReportGenerator() {
    if (db_) {
        sqlite3_close(reinterpret_cast<sqlite3*>(db_));
    }
}

SessionReport ReportGenerator::generateReport(const std::string& session_id) {
    SessionReport report;
    report.session_id = session_id;
    
    if (!db_) return report;
    
    std::string sql_reminders = "SELECT COUNT(*) FROM event WHERE session_id = '" + session_id + "' AND event_type = 'ReminderIssued';";
    
    auto callback = [](void* data, int argc, char** argv, char** /*azColName*/) -> int {
        int* count = static_cast<int*>(data);
        if (argc > 0 && argv[0]) {
            *count = std::stoi(argv[0]);
        }
        return 0;
    };
    
    sqlite3_exec(reinterpret_cast<sqlite3*>(db_), sql_reminders.c_str(), callback, &report.reminder_count, nullptr);
    
    std::string sql_distractions = "SELECT COUNT(*) FROM event WHERE session_id = '" + session_id + "' AND event_type = 'StateChange' AND data LIKE '%Distracted%';";
    sqlite3_exec(reinterpret_cast<sqlite3*>(db_), sql_distractions.c_str(), callback, &report.distraction_count, nullptr);
    
    std::cout << "Report for Session: " << session_id << "\n";
    std::cout << "Reminders: " << report.reminder_count << "\n";
    std::cout << "Distractions: " << report.distraction_count << "\n";
    
    return report;
}

} // namespace edgefocus

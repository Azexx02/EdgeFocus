#include "ReportGenerator.h"
#include "SQLiteStore.h"
#include "app/Logger.h"
#include <algorithm>

namespace edgefocus {

ReportGenerator::ReportGenerator(SQLiteStore& store) : sqlite_store_(store) {}

SessionReport ReportGenerator::generateReport(const std::string& session_id) {
    SessionReport report;
    report.session_id = session_id;

    auto events = sqlite_store_.queryEvents(session_id);
    if (events.empty()) return report;

    int64_t session_start = 0;
    int64_t session_end = 0;
    int64_t current_streak_start = 0;
    bool in_focus = false;

    for (const auto& ev : events) {
        switch (ev.type) {
            case StorageEventType::SessionStart:
                session_start = ev.timestamp;
                break;
            case StorageEventType::SessionEnd:
                session_end = ev.timestamp;
                break;
            case StorageEventType::ReminderIssued:
                report.reminder_count++;
                break;
            case StorageEventType::StateChange:
                if (ev.data.find("Phone") != std::string::npos ||
                    ev.data.find("phone") != std::string::npos) {
                    report.phone_distraction_count++;
                }
                // Match distraction states: numeric codes 1-5,8,9 or text patterns
                if (ev.data.find("Distracted") != std::string::npos ||
                    ev.data.find("PHONE_IN") != std::string::npos ||
                    ev.data.find("AWAY") != std::string::npos ||
                    ev.data.find("SLEEPY") != std::string::npos ||
                    ev.data.find("LOOKING_AWAY") != std::string::npos ||
                    ev.data.find("Changed to 1") != std::string::npos ||
                    ev.data.find("Changed to 2") != std::string::npos ||
                    ev.data.find("Changed to 3") != std::string::npos ||
                    ev.data.find("Changed to 4") != std::string::npos ||
                    ev.data.find("Changed to 5") != std::string::npos ||
                    ev.data.find("Changed to 8") != std::string::npos ||
                    ev.data.find("Changed to 9") != std::string::npos) {
                    report.distraction_count++;
                    if (in_focus && current_streak_start > 0) {
                        int64_t streak = ev.timestamp - current_streak_start;
                        report.longest_focus_streak_ms = std::max(report.longest_focus_streak_ms, streak);
                    }
                    in_focus = false;
                } else if (ev.data.find("FOCUSED") != std::string::npos ||
                           ev.data.find("Changed to 0") != std::string::npos) {
                    if (!in_focus) {
                        current_streak_start = ev.timestamp;
                        in_focus = true;
                    }
                }
                break;
        }
    }

    if (session_start > 0 && session_end > 0) {
        report.total_duration = (session_end - session_start) / 1000;
    }

    // Compute focus duration from state segments
    // Approximate: total - distraction time
    int64_t distraction_time_ms = 0;
    int64_t last_distraction_start = 0;
    bool in_distraction = false;
    for (const auto& ev : events) {
        if (ev.type != StorageEventType::StateChange) continue;
        bool is_distraction = ev.data.find("Distracted") != std::string::npos ||
                              ev.data.find("PHONE_IN") != std::string::npos ||
                              ev.data.find("AWAY") != std::string::npos ||
                              ev.data.find("SLEEPY") != std::string::npos ||
                              ev.data.find("LOOKING_AWAY") != std::string::npos ||
                              ev.data.find("Changed to 1") != std::string::npos ||
                              ev.data.find("Changed to 2") != std::string::npos ||
                              ev.data.find("Changed to 3") != std::string::npos ||
                              ev.data.find("Changed to 4") != std::string::npos ||
                              ev.data.find("Changed to 5") != std::string::npos ||
                              ev.data.find("Changed to 8") != std::string::npos ||
                              ev.data.find("Changed to 9") != std::string::npos;
        if (is_distraction && !in_distraction) {
            last_distraction_start = ev.timestamp;
            in_distraction = true;
        } else if (!is_distraction && in_distraction) {
            distraction_time_ms += ev.timestamp - last_distraction_start;
            in_distraction = false;
        }
    }
    report.focus_duration = report.total_duration - (distraction_time_ms / 1000);
    if (report.focus_duration < 0) report.focus_duration = 0;

    // Final streak check
    if (in_focus && current_streak_start > 0 && session_end > 0) {
        int64_t streak = session_end - current_streak_start;
        report.longest_focus_streak_ms = std::max(report.longest_focus_streak_ms, streak);
    }

    LOGI("ReportGenerator", "Report for %s: total=%llds focus=%llds dist=%d rem=%d streak=%lldms",
         session_id.c_str(), (long long)report.total_duration, (long long)report.focus_duration,
         report.distraction_count, report.reminder_count, (long long)report.longest_focus_streak_ms);

    return report;
}

} // namespace edgefocus

#include "ReportGenerator.h"
#include "SQLiteStore.h"
#include "app/Logger.h"

namespace edgefocus {

ReportGenerator::ReportGenerator(SQLiteStore& store) : sqlite_store_(store) {}

SessionReport ReportGenerator::generateReport(const std::string& session_id) {
    SessionReport report;
    report.session_id = session_id;

    auto session = sqlite_store_.querySessionById(session_id);
    if (!session.has_value()) {
        return report;
    }
    report.total_duration = session->total_duration;
    report.focus_duration = session->focus_duration;
    report.distraction_count = session->distraction_count;
    report.reminder_count = session->reminder_count;
    report.phone_distraction_count = session->phone_distraction_count;

    LOGI("ReportGenerator", "Report for %s: total=%llds focus=%llds dist=%d rem=%d phone=%d",
         session_id.c_str(), (long long)report.total_duration, (long long)report.focus_duration,
         report.distraction_count, report.reminder_count, report.phone_distraction_count);

    return report;
}

} // namespace edgefocus

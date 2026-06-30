#include "storage/SQLiteStore.h"
#include "storage/ReportGenerator.h"
#include "app/AppContext.h"
#include "sqlite3.h"
#include <iostream>
#include <cassert>

using namespace edgefocus;

int main() {
    std::cout << "Starting storage tests...\n";
    
    std::string dbPath = "test.db";
    std::remove(dbPath.c_str());
    
    SQLiteStore store(dbPath);
    AppContext dummyCtx;
    bool initOk = store.init(dummyCtx);
    assert(initOk);
    
    StorageEvent ev1{StorageEventType::SessionStart, "s1", 1000, ""};
    StorageEvent ev2{StorageEventType::StateChange, "s1", 1010, "Distracted"};
    StorageEvent ev3{StorageEventType::ReminderIssued, "s1", 1015, ""};
    StorageEvent ev4{StorageEventType::SessionEnd, "s1", 1020, ""};
    
    store.logEvent(ev1);
    store.logEvent(ev2);
    store.logEvent(ev3);
    store.logEvent(ev4);
    
    // Simulate engine tick
    for (int i=0; i<5; ++i) {
        store.runOnce(dummyCtx);
    }
    
    ReportGenerator reportGen(store);
    SessionReport rep = reportGen.generateReport("s1");
    
    assert(rep.session_id == "s1");
    assert(rep.distraction_count == 1);
    assert(rep.reminder_count == 1);
    assert(rep.total_duration == 0); // seconds: (1020-1000)/1000 = 0 since < 1s
    
    // Test queryEvents
    auto events = store.queryEvents("s1");
    assert(events.size() == 4);
    assert(events[0].type == StorageEventType::SessionStart);
    assert(events[1].type == StorageEventType::StateChange);
    assert(events[2].type == StorageEventType::ReminderIssued);
    assert(events[3].type == StorageEventType::SessionEnd);
    
    // Test extended report fields
    assert(rep.longest_focus_streak_ms >= 0);
    assert(rep.phone_distraction_count >= 0);
    
    // Test with phone-related state changes
    std::string dbPath2 = "test2.db";
    std::remove(dbPath2.c_str());
    SQLiteStore store2(dbPath2);
    store2.init(dummyCtx);
    
    store2.logEvent({StorageEventType::SessionStart, "s2", 5000, ""});
    store2.logEvent({StorageEventType::StateChange, "s2", 6000, "Changed to 0"}); // FOCUSED
    store2.logEvent({StorageEventType::StateChange, "s2", 10000, "Changed to 3"}); // PHONE_IN_USE
    store2.logEvent({StorageEventType::StateChange, "s2", 12000, "Changed to 0"}); // FOCUSED
    store2.logEvent({StorageEventType::SessionEnd, "s2", 20000, ""});
    
    for (int i=0; i<10; ++i) store2.runOnce(dummyCtx);
    
    ReportGenerator rg2(store2);
    SessionReport rep2 = rg2.generateReport("s2");
    assert(rep2.total_duration == 15); // (20000-5000)/1000 = 15
    assert(rep2.distraction_count == 1); // "Changed to 3" matches "PHONE" pattern

    // Test migration from an older deployed database schema.
    std::string legacyDbPath = "legacy_session.db";
    std::remove(legacyDbPath.c_str());
    sqlite3* legacyDb = nullptr;
    assert(sqlite3_open(legacyDbPath.c_str(), &legacyDb) == SQLITE_OK);
    assert(sqlite3_exec(legacyDb,
        "CREATE TABLE session ("
        "id TEXT PRIMARY KEY,"
        "start_time INTEGER,"
        "target_duration INTEGER,"
        "status TEXT"
        ");"
        "INSERT INTO session (id, start_time, target_duration, status) "
        "VALUES ('legacy_active', 1000, 60000, 'active');",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(legacyDb);

    SQLiteStore legacyStore(legacyDbPath);
    assert(legacyStore.init(dummyCtx));
    assert(legacyStore.closeOpenSessions(61000, "interrupted") == 1);
    auto legacyReport = legacyStore.queryLastCompletedSession();
    assert(legacyReport.session_id == "legacy_active");
    assert(legacyReport.total_duration == 60);

    std::string lastReportDbPath = "last_report_order.db";
    std::remove(lastReportDbPath.c_str());
    SQLiteStore lastReportStore(lastReportDbPath);
    assert(lastReportStore.init(dummyCtx));
    assert(lastReportStore.saveSession("old_future", 1000, 60000));
    assert(lastReportStore.updateSessionEnd("old_future", 999999, 60, 60, 0, 0, 0, "old"));
    assert(lastReportStore.saveSession("new_current", 2000, 60000));
    assert(lastReportStore.updateSessionEnd("new_current", 3000, 1, 1, 0, 0, 0, "new"));
    auto latestReport = lastReportStore.queryLastCompletedSession();
    assert(latestReport.session_id == "new_current");

    auto latestHistory = lastReportStore.querySessionHistory("", 1);
    assert(latestHistory.sessions.size() >= 1);
    assert(latestHistory.sessions.front().session_id == "new_current");

    // Pagination cursor should point at the last returned row, not the extra row
    // used to detect has_more, otherwise the next page skips one session.
    auto firstPage = lastReportStore.querySessionHistory("", 1);
    assert(firstPage.sessions.size() == 1);
    assert(firstPage.has_more);
    auto secondPage = lastReportStore.querySessionHistory(firstPage.next_cursor, 1);
    assert(secondPage.sessions.size() == 1);
    assert(secondPage.sessions.front().session_id == "old_future");

    // Verify migration for legacy feedback table missing updated_at column.
    std::string legacyFeedbackDbPath = "legacy_feedback.db";
    std::remove(legacyFeedbackDbPath.c_str());
    sqlite3* legacyFeedbackDb = nullptr;
    assert(sqlite3_open(legacyFeedbackDbPath.c_str(), &legacyFeedbackDb) == SQLITE_OK);
    assert(sqlite3_exec(legacyFeedbackDb,
        "CREATE TABLE session ("
        "id TEXT PRIMARY KEY,"
        "start_time INTEGER,"
        "target_duration INTEGER,"
        "status TEXT"
        ");"
        "INSERT INTO session (id, start_time, target_duration, status) "
        "VALUES ('legacy_feedback_session', 1000, 60000, 'active');"
        "CREATE TABLE feedback (session_id TEXT PRIMARY KEY, rating TEXT NOT NULL DEFAULT 'just_right');",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(legacyFeedbackDb);

    SQLiteStore legacyFeedbackStore(legacyFeedbackDbPath);
    assert(legacyFeedbackStore.init(dummyCtx));
    assert(legacyFeedbackStore.saveFeedback("legacy_feedback_session", "too_much"));
    sqlite3* migratedDb = nullptr;
    assert(sqlite3_open(legacyFeedbackDbPath.c_str(), &migratedDb) == SQLITE_OK);
    bool hasUpdatedAt = false;
    auto feedbackColumnCallback = [](void* data, int argc, char** argv, char**) -> int {
        if (argc >= 2 && argv[1] && std::string(argv[1]) == "updated_at") {
            *static_cast<bool*>(data) = true;
        }
        return 0;
    };
    assert(sqlite3_exec(migratedDb, "PRAGMA table_info(feedback);", feedbackColumnCallback, &hasUpdatedAt, nullptr) == SQLITE_OK);
    assert(hasUpdatedAt);
    sqlite3_close(migratedDb);
    
    std::cout << "Storage tests passed!\n";
    return 0;
}

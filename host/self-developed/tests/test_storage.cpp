#include "storage/SQLiteStore.h"
#include "storage/ReportGenerator.h"
#include "app/AppContext.h"
#include "sqlite3.h"
#include <iostream>
#include <cassert>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>
#ifdef _MSC_VER
#include <crtdbg.h>
#include <cstdlib>
#endif

using namespace edgefocus;

namespace {

std::string querySingleText(sqlite3* db, const char* sql, const std::string& param) {
    sqlite3_stmt* stmt = nullptr;
    assert(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_bind_text(stmt, 1, param.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK);

    std::string result;
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    const unsigned char* text = sqlite3_column_text(stmt, 0);
    if (text) {
        result = reinterpret_cast<const char*>(text);
    }

    sqlite3_finalize(stmt);
    return result;
}

} // namespace

int main() {
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    std::cout << "Starting storage tests...\n";
    
    std::string dbPath = "test.db";
    std::remove(dbPath.c_str());
    
    SQLiteStore store(dbPath);
    AppContext dummyCtx;
    bool initOk = store.init(dummyCtx);
    assert(initOk);
    assert(store.saveSession("s1", 1000, 60000));
    assert(store.updateSessionEnd("s1", 1020, 20, 15, 7, 5, 3, "test_end"));
    
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
    assert(rep.distraction_count == 7);
    assert(rep.reminder_count == 5);
    assert(rep.phone_distraction_count == 3);
    assert(rep.total_duration == 20);
    assert(rep.focus_duration == 15);
    
    // Test queryEvents
    auto events = store.queryEvents("s1");
    assert(events.size() == 4);
    assert(events[0].type == StorageEventType::SessionStart);
    assert(events[1].type == StorageEventType::StateChange);
    assert(events[2].type == StorageEventType::ReminderIssued);
    assert(events[3].type == StorageEventType::SessionEnd);
    
    // ReportGenerator must not infer counts from event text.
    assert(rep.longest_focus_streak_ms == 0);
    
    // Test with phone-related state changes
    std::string dbPath2 = "test2.db";
    std::remove(dbPath2.c_str());
    SQLiteStore store2(dbPath2);
    store2.init(dummyCtx);
    assert(store2.saveSession("s2", 5000, 60000));
    
    store2.logEvent({StorageEventType::SessionStart, "s2", 5000, ""});
    store2.logEvent({StorageEventType::StateChange, "s2", 6000, "Changed to 0"}); // FOCUSED
    store2.logEvent({StorageEventType::StateChange, "s2", 10000, "Changed to 3"}); // PHONE_IN_USE
    store2.logEvent({StorageEventType::StateChange, "s2", 12000, "Changed to 0"}); // FOCUSED
    store2.logEvent({StorageEventType::SessionEnd, "s2", 20000, ""});
    
    for (int i=0; i<10; ++i) store2.runOnce(dummyCtx);
    assert(store2.updateSessionEnd("s2", 20000, 15, 11, 4, 2, 1, "test_end"));
    
    ReportGenerator rg2(store2);
    SessionReport rep2 = rg2.generateReport("s2");
    assert(rep2.total_duration == 15); // (20000-5000)/1000 = 15
    assert(rep2.focus_duration == 11);
    assert(rep2.distraction_count == 4);
    assert(rep2.reminder_count == 2);
    assert(rep2.phone_distraction_count == 1);

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

    // Regression: session/feedback/preference values with quotes or special
    // characters should persist cleanly and read back intact.
    std::string quotedDbPath = "quoted_storage.db";
    std::remove(quotedDbPath.c_str());
    SQLiteStore quotedStore(quotedDbPath);
    assert(quotedStore.init(dummyCtx));

    const std::string quotedSessionId = "session_'\";[]{}";
    const std::string quotedEventData = "payload with 'quotes', \"double\", and ; punctuation";
    const std::string quotedEndReason = "ended because user's app said \"stop\"";
    const std::string quotedFeedback = "need'less_sound";
    const std::string quotedPrefKey = "channel'mask";
    const std::string quotedPrefValue = "screen+'band';sound=\"off\"";

    assert(quotedStore.saveSession(quotedSessionId, 7000, 90000));
    assert(quotedStore.logEvent({StorageEventType::StateChange, quotedSessionId, 7010, quotedEventData}));
    quotedStore.runOnce(dummyCtx);
    assert(quotedStore.updateSessionEnd(quotedSessionId, 9000, 2, 2, 1, 1, 0, quotedEndReason));
    assert(quotedStore.saveFeedback(quotedSessionId, quotedFeedback));
    assert(quotedStore.savePreference(quotedPrefKey, quotedPrefValue));
    assert(quotedStore.getPreference(quotedPrefKey, "") == quotedPrefValue);

    auto quotedEvents = quotedStore.queryEvents(quotedSessionId);
    assert(quotedEvents.size() == 1);
    assert(quotedEvents.front().session_id == quotedSessionId);
    assert(quotedEvents.front().data == quotedEventData);

    auto quotedSession = quotedStore.queryLastCompletedSession();
    assert(quotedSession.session_id == quotedSessionId);
    assert(quotedSession.feedback == quotedFeedback);

    sqlite3* quotedDb = nullptr;
    assert(sqlite3_open(quotedDbPath.c_str(), &quotedDb) == SQLITE_OK);
    assert(querySingleText(quotedDb, "SELECT end_reason FROM session WHERE id = ?;", quotedSessionId) == quotedEndReason);
    sqlite3_close(quotedDb);

    // Regression: shutdown should flush queued operations even if runOnce was
    // never called for the queued events.
    std::string shutdownDrainDbPath = "shutdown_drain.db";
    std::remove(shutdownDrainDbPath.c_str());
    {
        SQLiteStore shutdownDrainStore(shutdownDrainDbPath);
        assert(shutdownDrainStore.init(dummyCtx));
        for (int i = 0; i < 4; ++i) {
            assert(shutdownDrainStore.logEvent({
                StorageEventType::ReminderIssued,
                "drain_session",
                10000 + i,
                "queued event " + std::to_string(i)
            }));
        }
        assert(shutdownDrainStore.savePreference("shutdown'pref", "value before shutdown"));
        shutdownDrainStore.shutdown(dummyCtx);
    }

    SQLiteStore shutdownDrainVerifyStore(shutdownDrainDbPath);
    assert(shutdownDrainVerifyStore.init(dummyCtx));
    auto drainedEvents = shutdownDrainVerifyStore.queryEvents("drain_session");
    assert(drainedEvents.size() == 4);
    assert(drainedEvents.front().data == "queued event 0");
    assert(shutdownDrainVerifyStore.getPreference("shutdown'pref", "") == "value before shutdown");

    // Regression: runtime SQLite access can come from module ticks and App
    // command handlers at the same time, so DB handle access must be serialized.
    std::string concurrentDbPath = "concurrent_storage.db";
    std::remove(concurrentDbPath.c_str());
    SQLiteStore concurrentStore(concurrentDbPath);
    assert(concurrentStore.init(dummyCtx));
    assert(concurrentStore.saveSession("concurrent_session", 1000, 60000));
    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t) {
        workers.emplace_back([&concurrentStore, &dummyCtx, t]() {
            for (int i = 0; i < 50; ++i) {
                assert(concurrentStore.savePreference("k" + std::to_string(t) + "_" + std::to_string(i),
                                                      "v" + std::to_string(i)));
                assert(concurrentStore.updateSessionCounts("concurrent_session", i, i + 1, i + 2));
                concurrentStore.logEvent({StorageEventType::StateChange,
                                          "concurrent_session",
                                          2000 + t * 100 + i,
                                          "worker " + std::to_string(t)});
                concurrentStore.runOnce(dummyCtx);
                auto page = concurrentStore.querySessionHistory("", 2);
                (void)page;
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    concurrentStore.shutdown(dummyCtx);

    store.shutdown(dummyCtx);
    store2.shutdown(dummyCtx);
    legacyStore.shutdown(dummyCtx);
    lastReportStore.shutdown(dummyCtx);
    legacyFeedbackStore.shutdown(dummyCtx);
    quotedStore.shutdown(dummyCtx);
    shutdownDrainVerifyStore.shutdown(dummyCtx);

    std::remove(dbPath.c_str());
    std::remove(dbPath2.c_str());
    std::remove(legacyDbPath.c_str());
    std::remove(lastReportDbPath.c_str());
    std::remove(legacyFeedbackDbPath.c_str());
    std::remove(quotedDbPath.c_str());
    std::remove(shutdownDrainDbPath.c_str());
    std::remove(concurrentDbPath.c_str());
    
    std::cout << "Storage tests passed!\n";
    return 0;
}

#include "SQLiteStore.h"
#include "app/AppContext.h"
#include "app/Logger.h"
#include "schema.h"
#include "sqlite3.h"
#include <iostream>
#include <thread>
#include <chrono>

namespace edgefocus {

SQLiteStore::SQLiteStore(const std::string& dbPath) : Module("SQLiteStore"), db_(nullptr), dbPath_(dbPath), eventQueue_(100, true) {
}

SQLiteStore::~SQLiteStore() {
}

bool SQLiteStore::init(AppContext& /*ctx*/) {
    if (sqlite3_open(dbPath_.c_str(), reinterpret_cast<sqlite3**>(&db_)) != SQLITE_OK) {
        LOGE("SQLiteStore", "Can't open database: %s", sqlite3_errmsg(reinterpret_cast<sqlite3*>(db_)));
        return false;
    }
    
    char* errMsg = nullptr;
    if (sqlite3_exec(reinterpret_cast<sqlite3*>(db_), DB_SCHEMA, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        LOGE("SQLiteStore", "SQL error: %s", errMsg);
        sqlite3_free(errMsg);
        return false;
    }
    if (!migrateSchema()) {
        return false;
    }
    LOGI("SQLiteStore", "Database initialized.");
    return true;
}

bool SQLiteStore::sessionColumnExists(const std::string& column_name) const {
    return tableColumnExists("session", column_name);
}

bool SQLiteStore::tableColumnExists(const std::string& table_name, const std::string& column_name) const {
    if (!db_) return false;

    bool exists = false;
    auto callback = [](void* data, int argc, char** argv, char**) -> int {
        if (argc >= 2 && argv[1]) {
            auto* query = static_cast<std::pair<const std::string*, bool*>*>(data);
            if (*(query->first) == argv[1]) {
                *(query->second) = true;
            }
        }
        return 0;
    };

    std::pair<const std::string*, bool*> query{&column_name, &exists};
    char* errMsg = nullptr;
    const std::string pragma = "PRAGMA table_info(" + table_name + ");";
    if (sqlite3_exec(reinterpret_cast<sqlite3*>(db_), pragma.c_str(),
                     callback, &query, &errMsg) != SQLITE_OK) {
        LOGE("SQLiteStore", "schema inspection error: %s", errMsg);
        sqlite3_free(errMsg);
        return false;
    }
    return exists;
}

bool SQLiteStore::migrateSchema() {
    struct ColumnDef {
        const char* name;
        const char* definition;
    };

    static constexpr ColumnDef kSessionColumns[] = {
        {"start_time", "INTEGER"},
        {"end_time", "INTEGER"},
        {"target_duration", "INTEGER"},
        {"actual_duration", "INTEGER"},
        {"focus_duration", "INTEGER"},
        {"status", "TEXT"},
        {"end_reason", "TEXT"},
        {"distraction_count", "INTEGER DEFAULT 0"},
        {"reminder_count", "INTEGER DEFAULT 0"},
        {"phone_distraction_count", "INTEGER DEFAULT 0"},
        {"feedback", "TEXT DEFAULT 'just_right'"},
    };

    for (const auto& column : kSessionColumns) {
        if (sessionColumnExists(column.name)) {
            continue;
        }
        std::string sql = "ALTER TABLE session ADD COLUMN ";
        sql += column.name;
        sql += " ";
        sql += column.definition;
        sql += ";";

        char* errMsg = nullptr;
        if (sqlite3_exec(reinterpret_cast<sqlite3*>(db_), sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
            LOGE("SQLiteStore", "schema migration error for session.%s: %s", column.name, errMsg);
            sqlite3_free(errMsg);
            return false;
        }
        LOGI("SQLiteStore", "Migrated session table: added column %s", column.name);
    }

    if (!tableColumnExists("feedback", "updated_at")) {
        constexpr const char* kFeedbackMigrationSql = "ALTER TABLE feedback ADD COLUMN updated_at INTEGER;";
        char* errMsg = nullptr;
        if (sqlite3_exec(reinterpret_cast<sqlite3*>(db_), kFeedbackMigrationSql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
            LOGE("SQLiteStore", "schema migration error for feedback.updated_at: %s", errMsg);
            sqlite3_free(errMsg);
            return false;
        }
        LOGI("SQLiteStore", "Migrated feedback table: added column updated_at");
    }

    return true;
}

void SQLiteStore::shutdown(AppContext& /*ctx*/) {
    if (db_) {
        sqlite3_close(reinterpret_cast<sqlite3*>(db_));
        db_ = nullptr;
    }
    LOGI("SQLiteStore", "Shutdown completed.");
}

bool SQLiteStore::logEvent(const StorageEvent& event) {
    return eventQueue_.push(event);
}

std::vector<StorageEvent> SQLiteStore::queryEvents(const std::string& session_id) {
    std::vector<StorageEvent> results;
    if (!db_) return results;

    std::string sql = "SELECT event_type, session_id, timestamp, data FROM event WHERE session_id = '" + session_id + "' ORDER BY timestamp;";

    auto callback = [](void* data, int argc, char** argv, char** /*azColName*/) -> int {
        auto* vec = static_cast<std::vector<StorageEvent>*>(data);
        if (argc < 4) return 0;

        StorageEvent ev;
        std::string typeStr = argv[0] ? argv[0] : "";
        if (typeStr == "SessionStart") ev.type = StorageEventType::SessionStart;
        else if (typeStr == "SessionEnd") ev.type = StorageEventType::SessionEnd;
        else if (typeStr == "StateChange") ev.type = StorageEventType::StateChange;
        else if (typeStr == "ReminderIssued") ev.type = StorageEventType::ReminderIssued;
        else return 0;

        ev.session_id = argv[1] ? argv[1] : "";
        ev.timestamp = argv[2] ? std::stoll(argv[2]) : 0;
        ev.data = argv[3] ? argv[3] : "";
        vec->push_back(ev);
        return 0;
    };

    char* errMsg = nullptr;
    if (sqlite3_exec(reinterpret_cast<sqlite3*>(db_), sql.c_str(), callback, &results, &errMsg) != SQLITE_OK) {
        LOGE("SQLiteStore", "Query error: %s", errMsg);
        sqlite3_free(errMsg);
    }
    return results;
}

void SQLiteStore::runOnce(AppContext& /*ctx*/) {
    auto ev_opt = eventQueue_.tryPop();
    if (ev_opt.has_value()) {
        const auto& ev = *ev_opt;
        std::string typeStr;
        switch(ev.type) {
            case StorageEventType::SessionStart: typeStr = "SessionStart"; break;
            case StorageEventType::SessionEnd: typeStr = "SessionEnd"; break;
            case StorageEventType::StateChange: typeStr = "StateChange"; break;
            case StorageEventType::ReminderIssued: typeStr = "ReminderIssued"; break;
        }
        std::string sql = "INSERT INTO event (session_id, event_type, timestamp, data) VALUES ('" + 
                          ev.session_id + "', '" + typeStr + "', " + 
                          std::to_string(ev.timestamp) + ", '" + ev.data + "');";
        char* errMsg = nullptr;
        if (sqlite3_exec(reinterpret_cast<sqlite3*>(db_), sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
            LOGE("SQLiteStore", "Insert error: %s", errMsg);
            sqlite3_free(errMsg);
        }
    } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

// ---------------------------------------------------------------------------
// Session persistence
// ---------------------------------------------------------------------------

bool SQLiteStore::saveSession(const std::string& session_id, int64_t start_time, int64_t target_duration) {
    if (!db_) return false;
    std::string sql = "INSERT OR REPLACE INTO session (id, start_time, target_duration, status, feedback) VALUES ('" +
                      session_id + "', " + std::to_string(start_time) + ", " +
                      std::to_string(target_duration) + ", 'active', 'just_right');";
    char* errMsg = nullptr;
    if (sqlite3_exec(reinterpret_cast<sqlite3*>(db_), sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        LOGE("SQLiteStore", "saveSession error: %s", errMsg);
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool SQLiteStore::updateSessionEnd(const std::string& session_id, int64_t end_time,
                                    int64_t actual_duration, int64_t focus_duration,
                                    int distraction_count, int reminder_count,
                                    int phone_distraction_count, const std::string& end_reason) {
    if (!db_) return false;
    std::string sql = "UPDATE session SET end_time=" + std::to_string(end_time) +
                      ", actual_duration=" + std::to_string(actual_duration) +
                      ", focus_duration=" + std::to_string(focus_duration) +
                      ", distraction_count=" + std::to_string(distraction_count) +
                      ", reminder_count=" + std::to_string(reminder_count) +
                      ", phone_distraction_count=" + std::to_string(phone_distraction_count) +
                      ", end_reason='" + end_reason +
                      "', status='completed' WHERE id='" + session_id + "';";
    char* errMsg = nullptr;
    if (sqlite3_exec(reinterpret_cast<sqlite3*>(db_), sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        LOGE("SQLiteStore", "updateSessionEnd error: %s", errMsg);
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool SQLiteStore::updateSessionFeedback(const std::string& session_id, const std::string& feedback) {
    return saveFeedback(session_id, feedback);
}

bool SQLiteStore::updateSessionCounts(const std::string& session_id, int distraction_count,
                                       int reminder_count, int phone_distraction_count) {
    if (!db_) return false;
    std::string sql = "UPDATE session SET distraction_count=" + std::to_string(distraction_count) +
                      ", reminder_count=" + std::to_string(reminder_count) +
                      ", phone_distraction_count=" + std::to_string(phone_distraction_count) +
                      " WHERE id='" + session_id + "';";
    char* errMsg = nullptr;
    if (sqlite3_exec(reinterpret_cast<sqlite3*>(db_), sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        LOGE("SQLiteStore", "updateSessionCounts error: %s", errMsg);
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool SQLiteStore::sessionExists(const std::string& session_id) const {
    if (!db_) return false;
    if (session_id.empty()) return false;

    std::string sql = "SELECT 1 FROM session WHERE id='" + session_id + "' LIMIT 1;";
    bool exists = false;

    auto callback = [](void* data, int argc, char** argv, char**) -> int {
        if (argc > 0 && argv[0]) {
            *static_cast<bool*>(data) = true;
        }
        return 0;
    };

    char* errMsg = nullptr;
    if (sqlite3_exec(reinterpret_cast<sqlite3*>(db_), sql.c_str(), callback, &exists, &errMsg) != SQLITE_OK) {
        sqlite3_free(errMsg);
        return false;
    }

    return exists;
}

SQLiteStore::SessionHistoryPage SQLiteStore::querySessionHistory(const std::string& cursor, int limit) {
    SessionHistoryPage page;
    if (!db_) return page;

    int64_t effective_limit = limit > 0 ? limit : 1;

    std::string sql;
    if (cursor.empty()) {
        sql = "SELECT rowid, id, start_time, end_time, actual_duration, focus_duration, "
              "distraction_count, reminder_count, phone_distraction_count, feedback "
              "FROM session WHERE status='completed' ORDER BY rowid DESC LIMIT " +
              std::to_string(effective_limit + 1);
    } else {
        int64_t cursor_time = 0;
        try { cursor_time = std::stoll(cursor); } catch (...) { return page; }
        sql = "SELECT rowid, id, start_time, end_time, actual_duration, focus_duration, "
              "distraction_count, reminder_count, phone_distraction_count, feedback "
              "FROM session WHERE status='completed' AND rowid < " + std::to_string(cursor_time) +
              " ORDER BY rowid DESC LIMIT " + std::to_string(effective_limit + 1);
    }

    auto callback = [](void* data, int argc, char** argv, char**) -> int {
        auto* page = static_cast<SessionHistoryPage*>(data);
        if (argc < 10) return 0;
        SessionRecord rec;
        rec.rowid = argv[0] ? std::stoll(argv[0]) : 0;
        rec.session_id = argv[1] ? argv[1] : "";
        rec.start_time_ms = argv[2] ? std::stoll(argv[2]) : 0;
        rec.end_time_ms = argv[3] ? std::stoll(argv[3]) : 0;
        rec.total_duration = argv[4] ? std::stoll(argv[4]) : 0;
        rec.focus_duration = argv[5] ? std::stoll(argv[5]) : 0;
        rec.distraction_count = argv[6] ? std::stoi(argv[6]) : 0;
        rec.reminder_count = argv[7] ? std::stoi(argv[7]) : 0;
        rec.phone_distraction_count = argv[8] ? std::stoi(argv[8]) : 0;
        rec.feedback = argv[9] ? argv[9] : "just_right";
        page->sessions.push_back(rec);
        return 0;
    };

    char* errMsg = nullptr;
    if (sqlite3_exec(reinterpret_cast<sqlite3*>(db_), sql.c_str(), callback, &page, &errMsg) != SQLITE_OK) {
        LOGE("SQLiteStore", "querySessionHistory error: %s", errMsg);
        sqlite3_free(errMsg);
    }

    if (page.sessions.size() > static_cast<size_t>(effective_limit)) {
        page.has_more = true;
        page.sessions.pop_back();
        page.next_cursor = page.sessions.empty() ? "" : std::to_string(page.sessions.back().rowid);
    }
    return page;
}

SQLiteStore::SessionRecord SQLiteStore::queryLastCompletedSession() {
    SessionRecord rec;
    if (!db_) return rec;

    std::string sql = "SELECT id, start_time, end_time, actual_duration, focus_duration, "
                      "distraction_count, reminder_count, phone_distraction_count, feedback "
                      "FROM session WHERE status='completed' ORDER BY rowid DESC LIMIT 1;";

    auto callback = [](void* data, int argc, char** argv, char**) -> int {
        auto* rec = static_cast<SessionRecord*>(data);
        if (argc < 9) return 0;
        rec->session_id = argv[0] ? argv[0] : "";
        rec->start_time_ms = argv[1] ? std::stoll(argv[1]) : 0;
        rec->end_time_ms = argv[2] ? std::stoll(argv[2]) : 0;
        rec->total_duration = argv[3] ? std::stoll(argv[3]) : 0;
        rec->focus_duration = argv[4] ? std::stoll(argv[4]) : 0;
        rec->distraction_count = argv[5] ? std::stoi(argv[5]) : 0;
        rec->reminder_count = argv[6] ? std::stoi(argv[6]) : 0;
        rec->phone_distraction_count = argv[7] ? std::stoi(argv[7]) : 0;
        rec->feedback = argv[8] ? argv[8] : "just_right";
        return 0;
    };

    char* errMsg = nullptr;
    if (sqlite3_exec(reinterpret_cast<sqlite3*>(db_), sql.c_str(), callback, &rec, &errMsg) != SQLITE_OK) {
        LOGE("SQLiteStore", "queryLastCompletedSession error: %s", errMsg);
        sqlite3_free(errMsg);
    }
    return rec;
}

int SQLiteStore::closeOpenSessions(int64_t end_time_ms, const std::string& end_reason) {
    if (!db_) return 0;
    std::string sql = "UPDATE session SET end_time=" + std::to_string(end_time_ms) +
                      ", actual_duration=CASE WHEN start_time IS NOT NULL THEN (" +
                      std::to_string(end_time_ms) + " - start_time) / 1000 ELSE 0 END" +
                      ", focus_duration=0" +
                      ", end_reason='" + end_reason +
                      "', status='completed' WHERE status='active';";
    char* errMsg = nullptr;
    if (sqlite3_exec(reinterpret_cast<sqlite3*>(db_), sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        LOGE("SQLiteStore", "closeOpenSessions error: %s", errMsg);
        sqlite3_free(errMsg);
        return 0;
    }
    return sqlite3_changes(reinterpret_cast<sqlite3*>(db_));
}

// ---------------------------------------------------------------------------
// Feedback
// ---------------------------------------------------------------------------

bool SQLiteStore::saveFeedback(const std::string& session_id, const std::string& rating) {
    if (!sessionExists(session_id)) {
        return false;
    }
    if (!db_) return false;
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::string sql = "INSERT OR REPLACE INTO feedback (session_id, rating, updated_at) VALUES ('" +
                      session_id + "', '" + rating + "', " + std::to_string(now) + ");";
    char* errMsg = nullptr;
    if (sqlite3_exec(reinterpret_cast<sqlite3*>(db_), sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        LOGE("SQLiteStore", "saveFeedback error: %s", errMsg);
        sqlite3_free(errMsg);
        return false;
    }
    sql = "UPDATE session SET feedback='" + rating + "' WHERE id='" + session_id + "';";
    errMsg = nullptr;
    if (sqlite3_exec(reinterpret_cast<sqlite3*>(db_), sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        LOGE("SQLiteStore", "updateSessionFeedback error: %s", errMsg);
        sqlite3_free(errMsg);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Preferences
// ---------------------------------------------------------------------------

bool SQLiteStore::savePreference(const std::string& key, const std::string& value) {
    if (!db_) return false;
    std::string sql = "INSERT OR REPLACE INTO preferences (key, value) VALUES ('" +
                      key + "', '" + value + "');";
    char* errMsg = nullptr;
    if (sqlite3_exec(reinterpret_cast<sqlite3*>(db_), sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        LOGE("SQLiteStore", "savePreference error: %s", errMsg);
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

std::string SQLiteStore::getPreference(const std::string& key, const std::string& default_val) {
    if (!db_) return default_val;
    std::string result = default_val;
    std::string sql = "SELECT value FROM preferences WHERE key='" + key + "';";
    auto callback = [](void* data, int argc, char** argv, char**) -> int {
        if (argc > 0 && argv[0]) {
            *static_cast<std::string*>(data) = argv[0];
        }
        return 0;
    };
    char* errMsg = nullptr;
    if (sqlite3_exec(reinterpret_cast<sqlite3*>(db_), sql.c_str(), callback, &result, &errMsg) != SQLITE_OK) {
        sqlite3_free(errMsg);
    }
    return result;
}

std::map<std::string, std::string> SQLiteStore::getAllPreferences() {
    std::map<std::string, std::string> prefs;
    if (!db_) return prefs;
    std::string sql = "SELECT key, value FROM preferences;";
    auto callback = [](void* data, int argc, char** argv, char**) -> int {
        auto* map = static_cast<std::map<std::string, std::string>*>(data);
        if (argc >= 2 && argv[0] && argv[1]) {
            (*map)[argv[0]] = argv[1];
        }
        return 0;
    };
    char* errMsg = nullptr;
    if (sqlite3_exec(reinterpret_cast<sqlite3*>(db_), sql.c_str(), callback, &prefs, &errMsg) != SQLITE_OK) {
        sqlite3_free(errMsg);
    }
    return prefs;
}

} // namespace edgefocus

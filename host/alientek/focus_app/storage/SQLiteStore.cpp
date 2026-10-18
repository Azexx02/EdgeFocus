#include "SQLiteStore.h"
#include "app/AppContext.h"
#include "app/Logger.h"
#include "schema.h"
#include "sqlite3.h"
#include <iostream>
#include <thread>
#include <chrono>

namespace edgefocus {

namespace {

sqlite3* dbHandle(void* db) {
    return reinterpret_cast<sqlite3*>(db);
}

struct StatementGuard {
    sqlite3_stmt* stmt = nullptr;

    ~StatementGuard() {
        if (stmt) {
            sqlite3_finalize(stmt);
        }
    }
};

bool prepareStatement(sqlite3* db, const char* sql, StatementGuard& statement, const char* context) {
    if (sqlite3_prepare_v2(db, sql, -1, &statement.stmt, nullptr) != SQLITE_OK) {
        LOGE("SQLiteStore", "%s prepare error: %s", context, sqlite3_errmsg(db));
        return false;
    }
    return true;
}

bool bindText(sqlite3* db, sqlite3_stmt* stmt, int index, const std::string& value, const char* context) {
    if (sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        LOGE("SQLiteStore", "%s bind error: %s", context, sqlite3_errmsg(db));
        return false;
    }
    return true;
}

bool bindInt64(sqlite3* db, sqlite3_stmt* stmt, int index, int64_t value, const char* context) {
    if (sqlite3_bind_int64(stmt, index, value) != SQLITE_OK) {
        LOGE("SQLiteStore", "%s bind error: %s", context, sqlite3_errmsg(db));
        return false;
    }
    return true;
}

bool bindInt(sqlite3* db, sqlite3_stmt* stmt, int index, int value, const char* context) {
    if (sqlite3_bind_int(stmt, index, value) != SQLITE_OK) {
        LOGE("SQLiteStore", "%s bind error: %s", context, sqlite3_errmsg(db));
        return false;
    }
    return true;
}

bool stepExpectDone(sqlite3* db, sqlite3_stmt* stmt, const char* context) {
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        LOGE("SQLiteStore", "%s step error: %s", context, sqlite3_errmsg(db));
        return false;
    }
    return true;
}

const char* eventTypeToString(StorageEventType type) {
    switch (type) {
        case StorageEventType::SessionStart: return "SessionStart";
        case StorageEventType::SessionEnd: return "SessionEnd";
        case StorageEventType::StateChange: return "StateChange";
        case StorageEventType::ReminderIssued: return "ReminderIssued";
    }
    return "";
}

bool parseEventType(const char* type, StorageEventType& outType) {
    if (!type) {
        return false;
    }
    std::string typeStr = type;
    if (typeStr == "SessionStart") outType = StorageEventType::SessionStart;
    else if (typeStr == "SessionEnd") outType = StorageEventType::SessionEnd;
    else if (typeStr == "StateChange") outType = StorageEventType::StateChange;
    else if (typeStr == "ReminderIssued") outType = StorageEventType::ReminderIssued;
    else return false;
    return true;
}

} // namespace

SQLiteStore::SQLiteStore(const std::string& dbPath) : Module("SQLiteStore"), db_(nullptr), dbPath_(dbPath), eventQueue_(100, true) {
}

SQLiteStore::~SQLiteStore() {
}

bool SQLiteStore::init(AppContext& /*ctx*/) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    if (sqlite3_open(dbPath_.c_str(), reinterpret_cast<sqlite3**>(&db_)) != SQLITE_OK) {
        LOGE("SQLiteStore", "Can't open database: %s", sqlite3_errmsg(dbHandle(db_)));
        return false;
    }
    
    char* errMsg = nullptr;
    if (sqlite3_exec(dbHandle(db_), DB_SCHEMA, nullptr, nullptr, &errMsg) != SQLITE_OK) {
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
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
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
    if (sqlite3_exec(dbHandle(db_), pragma.c_str(),
                     callback, &query, &errMsg) != SQLITE_OK) {
        LOGE("SQLiteStore", "schema inspection error: %s", errMsg);
        sqlite3_free(errMsg);
        return false;
    }
    return exists;
}

bool SQLiteStore::migrateSchema() {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
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
        if (sqlite3_exec(dbHandle(db_), sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
            LOGE("SQLiteStore", "schema migration error for session.%s: %s", column.name, errMsg);
            sqlite3_free(errMsg);
            return false;
        }
        LOGI("SQLiteStore", "Migrated session table: added column %s", column.name);
    }

    if (!tableColumnExists("feedback", "updated_at")) {
        constexpr const char* kFeedbackMigrationSql = "ALTER TABLE feedback ADD COLUMN updated_at INTEGER;";
        char* errMsg = nullptr;
        if (sqlite3_exec(dbHandle(db_), kFeedbackMigrationSql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
            LOGE("SQLiteStore", "schema migration error for feedback.updated_at: %s", errMsg);
            sqlite3_free(errMsg);
            return false;
        }
        LOGI("SQLiteStore", "Migrated feedback table: added column updated_at");
    }

    return true;
}

void SQLiteStore::shutdown(AppContext& /*ctx*/) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    if (db_) {
        drainPendingEvents();
        sqlite3_close(dbHandle(db_));
        db_ = nullptr;
    }
    LOGI("SQLiteStore", "Shutdown completed.");
}

bool SQLiteStore::logEvent(const StorageEvent& event) {
    return eventQueue_.push(event);
}

bool SQLiteStore::persistEvent(const StorageEvent& event) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    if (!db_) return false;

    sqlite3* db = dbHandle(db_);
    StatementGuard statement;
    constexpr const char* kSql = "INSERT INTO event (session_id, event_type, timestamp, data) VALUES (?, ?, ?, ?);";
    if (!prepareStatement(db, kSql, statement, "persistEvent")) {
        return false;
    }
    if (!bindText(db, statement.stmt, 1, event.session_id, "persistEvent session_id") ||
        !bindText(db, statement.stmt, 2, eventTypeToString(event.type), "persistEvent event_type") ||
        !bindInt64(db, statement.stmt, 3, event.timestamp, "persistEvent timestamp") ||
        !bindText(db, statement.stmt, 4, event.data, "persistEvent data")) {
        return false;
    }
    return stepExpectDone(db, statement.stmt, "persistEvent");
}

void SQLiteStore::drainPendingEvents() {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    while (true) {
        auto event = eventQueue_.tryPop();
        if (!event.has_value()) {
            return;
        }
        persistEvent(*event);
    }
}

std::vector<StorageEvent> SQLiteStore::queryEvents(const std::string& session_id) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    std::vector<StorageEvent> results;
    if (!db_) return results;

    sqlite3* db = dbHandle(db_);
    StatementGuard statement;
    constexpr const char* kSql =
        "SELECT event_type, session_id, timestamp, data FROM event WHERE session_id = ? ORDER BY timestamp;";
    if (!prepareStatement(db, kSql, statement, "queryEvents") ||
        !bindText(db, statement.stmt, 1, session_id, "queryEvents session_id")) {
        return results;
    }

    while (true) {
        const int rc = sqlite3_step(statement.stmt);
        if (rc == SQLITE_DONE) {
            break;
        }
        if (rc != SQLITE_ROW) {
            LOGE("SQLiteStore", "queryEvents step error: %s", sqlite3_errmsg(db));
            results.clear();
            break;
        }

        StorageEvent ev;
        if (!parseEventType(reinterpret_cast<const char*>(sqlite3_column_text(statement.stmt, 0)), ev.type)) {
            continue;
        }

        const unsigned char* storedSessionId = sqlite3_column_text(statement.stmt, 1);
        const unsigned char* storedData = sqlite3_column_text(statement.stmt, 3);
        ev.session_id = storedSessionId ? reinterpret_cast<const char*>(storedSessionId) : "";
        ev.timestamp = sqlite3_column_int64(statement.stmt, 2);
        ev.data = storedData ? reinterpret_cast<const char*>(storedData) : "";
        results.push_back(std::move(ev));
    }
    return results;
}

void SQLiteStore::runOnce(AppContext& /*ctx*/) {
    auto ev_opt = eventQueue_.tryPop();
    if (ev_opt.has_value()) {
        persistEvent(*ev_opt);
    } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

// ---------------------------------------------------------------------------
// Session persistence
// ---------------------------------------------------------------------------

bool SQLiteStore::saveSession(const std::string& session_id, int64_t start_time, int64_t target_duration) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    if (!db_) return false;
    sqlite3* db = dbHandle(db_);
    StatementGuard statement;
    constexpr const char* kSql =
        "INSERT OR REPLACE INTO session (id, start_time, target_duration, status, feedback) "
        "VALUES (?, ?, ?, 'active', 'just_right');";
    if (!prepareStatement(db, kSql, statement, "saveSession")) {
        return false;
    }
    if (!bindText(db, statement.stmt, 1, session_id, "saveSession id") ||
        !bindInt64(db, statement.stmt, 2, start_time, "saveSession start_time") ||
        !bindInt64(db, statement.stmt, 3, target_duration, "saveSession target_duration")) {
        return false;
    }
    return stepExpectDone(db, statement.stmt, "saveSession");
}

bool SQLiteStore::updateSessionEnd(const std::string& session_id, int64_t end_time,
                                    int64_t actual_duration, int64_t focus_duration,
                                    int distraction_count, int reminder_count,
                                    int phone_distraction_count, const std::string& end_reason) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    if (!db_) return false;
    sqlite3* db = dbHandle(db_);
    StatementGuard statement;
    constexpr const char* kSql =
        "UPDATE session SET end_time=?, actual_duration=?, focus_duration=?, "
        "distraction_count=?, reminder_count=?, phone_distraction_count=?, "
        "end_reason=?, status='completed' WHERE id=?;";
    if (!prepareStatement(db, kSql, statement, "updateSessionEnd")) {
        return false;
    }
    if (!bindInt64(db, statement.stmt, 1, end_time, "updateSessionEnd end_time") ||
        !bindInt64(db, statement.stmt, 2, actual_duration, "updateSessionEnd actual_duration") ||
        !bindInt64(db, statement.stmt, 3, focus_duration, "updateSessionEnd focus_duration") ||
        !bindInt(db, statement.stmt, 4, distraction_count, "updateSessionEnd distraction_count") ||
        !bindInt(db, statement.stmt, 5, reminder_count, "updateSessionEnd reminder_count") ||
        !bindInt(db, statement.stmt, 6, phone_distraction_count, "updateSessionEnd phone_distraction_count") ||
        !bindText(db, statement.stmt, 7, end_reason, "updateSessionEnd end_reason") ||
        !bindText(db, statement.stmt, 8, session_id, "updateSessionEnd id")) {
        return false;
    }
    return stepExpectDone(db, statement.stmt, "updateSessionEnd");
}

bool SQLiteStore::updateSessionFeedback(const std::string& session_id, const std::string& feedback) {
    return saveFeedback(session_id, feedback);
}

bool SQLiteStore::updateSessionCounts(const std::string& session_id, int distraction_count,
                                       int reminder_count, int phone_distraction_count) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    if (!db_) return false;
    sqlite3* db = dbHandle(db_);
    StatementGuard statement;
    constexpr const char* kSql =
        "UPDATE session SET distraction_count=?, reminder_count=?, phone_distraction_count=? WHERE id=?;";
    if (!prepareStatement(db, kSql, statement, "updateSessionCounts")) {
        return false;
    }
    if (!bindInt(db, statement.stmt, 1, distraction_count, "updateSessionCounts distraction_count") ||
        !bindInt(db, statement.stmt, 2, reminder_count, "updateSessionCounts reminder_count") ||
        !bindInt(db, statement.stmt, 3, phone_distraction_count, "updateSessionCounts phone_distraction_count") ||
        !bindText(db, statement.stmt, 4, session_id, "updateSessionCounts id")) {
        return false;
    }
    return stepExpectDone(db, statement.stmt, "updateSessionCounts");
}

bool SQLiteStore::sessionExists(const std::string& session_id) const {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    if (!db_) return false;
    if (session_id.empty()) return false;

    sqlite3* db = dbHandle(db_);
    StatementGuard statement;
    constexpr const char* kSql = "SELECT 1 FROM session WHERE id = ? LIMIT 1;";
    if (!prepareStatement(db, kSql, statement, "sessionExists") ||
        !bindText(db, statement.stmt, 1, session_id, "sessionExists id")) {
        return false;
    }

    const int rc = sqlite3_step(statement.stmt);
    if (rc == SQLITE_ROW) {
        return true;
    }
    if (rc != SQLITE_DONE) {
        LOGE("SQLiteStore", "sessionExists step error: %s", sqlite3_errmsg(db));
        return false;
    }
    return false;
}

SQLiteStore::SessionHistoryPage SQLiteStore::querySessionHistory(const std::string& cursor, int limit) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
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

std::optional<SQLiteStore::SessionRecord> SQLiteStore::querySessionById(const std::string& session_id) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    if (!db_ || session_id.empty()) return std::nullopt;

    sqlite3* db = dbHandle(db_);
    StatementGuard statement;
    constexpr const char* kSql =
        "SELECT rowid, id, start_time, end_time, actual_duration, focus_duration, "
        "distraction_count, reminder_count, phone_distraction_count, feedback "
        "FROM session WHERE id = ? LIMIT 1;";
    if (!prepareStatement(db, kSql, statement, "querySessionById") ||
        !bindText(db, statement.stmt, 1, session_id, "querySessionById id")) {
        return std::nullopt;
    }

    const int rc = sqlite3_step(statement.stmt);
    if (rc == SQLITE_DONE) {
        return std::nullopt;
    }
    if (rc != SQLITE_ROW) {
        LOGE("SQLiteStore", "querySessionById step error: %s", sqlite3_errmsg(db));
        return std::nullopt;
    }

    SessionRecord rec;
    rec.rowid = sqlite3_column_int64(statement.stmt, 0);
    const unsigned char* stored_id = sqlite3_column_text(statement.stmt, 1);
    const unsigned char* feedback = sqlite3_column_text(statement.stmt, 9);
    rec.session_id = stored_id ? reinterpret_cast<const char*>(stored_id) : "";
    rec.start_time_ms = sqlite3_column_int64(statement.stmt, 2);
    rec.end_time_ms = sqlite3_column_int64(statement.stmt, 3);
    rec.total_duration = sqlite3_column_int64(statement.stmt, 4);
    rec.focus_duration = sqlite3_column_int64(statement.stmt, 5);
    rec.distraction_count = sqlite3_column_int(statement.stmt, 6);
    rec.reminder_count = sqlite3_column_int(statement.stmt, 7);
    rec.phone_distraction_count = sqlite3_column_int(statement.stmt, 8);
    rec.feedback = feedback ? reinterpret_cast<const char*>(feedback) : "just_right";
    return rec;
}

SQLiteStore::SessionRecord SQLiteStore::queryLastCompletedSession() {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
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
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    if (!db_) return 0;
    sqlite3* db = dbHandle(db_);
    StatementGuard statement;
    constexpr const char* kSql =
        "UPDATE session SET end_time=?, "
        "actual_duration=CASE WHEN start_time IS NOT NULL THEN (? - start_time) / 1000 ELSE 0 END, "
        "focus_duration=0, end_reason=?, status='completed' WHERE status='active';";
    if (!prepareStatement(db, kSql, statement, "closeOpenSessions")) {
        return 0;
    }
    if (!bindInt64(db, statement.stmt, 1, end_time_ms, "closeOpenSessions end_time") ||
        !bindInt64(db, statement.stmt, 2, end_time_ms, "closeOpenSessions duration_end_time") ||
        !bindText(db, statement.stmt, 3, end_reason, "closeOpenSessions end_reason") ||
        !stepExpectDone(db, statement.stmt, "closeOpenSessions")) {
        return 0;
    }
    return sqlite3_changes(db);
}

// ---------------------------------------------------------------------------
// Feedback
// ---------------------------------------------------------------------------

bool SQLiteStore::saveFeedback(const std::string& session_id, const std::string& rating) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    if (!sessionExists(session_id)) {
        return false;
    }
    if (!db_) return false;
    sqlite3* db = dbHandle(db_);
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    StatementGuard saveStatement;
    constexpr const char* kSaveSql =
        "INSERT OR REPLACE INTO feedback (session_id, rating, updated_at) VALUES (?, ?, ?);";
    if (!prepareStatement(db, kSaveSql, saveStatement, "saveFeedback") ||
        !bindText(db, saveStatement.stmt, 1, session_id, "saveFeedback session_id") ||
        !bindText(db, saveStatement.stmt, 2, rating, "saveFeedback rating") ||
        !bindInt64(db, saveStatement.stmt, 3, now, "saveFeedback updated_at") ||
        !stepExpectDone(db, saveStatement.stmt, "saveFeedback")) {
        return false;
    }

    StatementGuard updateStatement;
    constexpr const char* kUpdateSql = "UPDATE session SET feedback=? WHERE id=?;";
    if (!prepareStatement(db, kUpdateSql, updateStatement, "updateSessionFeedback") ||
        !bindText(db, updateStatement.stmt, 1, rating, "updateSessionFeedback feedback") ||
        !bindText(db, updateStatement.stmt, 2, session_id, "updateSessionFeedback session_id")) {
        return false;
    }
    return stepExpectDone(db, updateStatement.stmt, "updateSessionFeedback");
}

// ---------------------------------------------------------------------------
// Preferences
// ---------------------------------------------------------------------------

bool SQLiteStore::savePreference(const std::string& key, const std::string& value) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    if (!db_) return false;
    sqlite3* db = dbHandle(db_);
    StatementGuard statement;
    constexpr const char* kSql = "INSERT OR REPLACE INTO preferences (key, value) VALUES (?, ?);";
    if (!prepareStatement(db, kSql, statement, "savePreference")) {
        return false;
    }
    if (!bindText(db, statement.stmt, 1, key, "savePreference key") ||
        !bindText(db, statement.stmt, 2, value, "savePreference value")) {
        return false;
    }
    return stepExpectDone(db, statement.stmt, "savePreference");
}

std::string SQLiteStore::getPreference(const std::string& key, const std::string& default_val) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    if (!db_) return default_val;
    std::string result = default_val;
    sqlite3* db = dbHandle(db_);
    StatementGuard statement;
    constexpr const char* kSql = "SELECT value FROM preferences WHERE key = ?;";
    if (!prepareStatement(db, kSql, statement, "getPreference") ||
        !bindText(db, statement.stmt, 1, key, "getPreference key")) {
        return default_val;
    }

    const int rc = sqlite3_step(statement.stmt);
    if (rc == SQLITE_ROW) {
        const unsigned char* value = sqlite3_column_text(statement.stmt, 0);
        if (value) {
            result = reinterpret_cast<const char*>(value);
        }
        return result;
    }
    if (rc != SQLITE_DONE) {
        LOGE("SQLiteStore", "getPreference step error: %s", sqlite3_errmsg(db));
    }
    return result;
}

std::map<std::string, std::string> SQLiteStore::getAllPreferences() {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
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
    if (sqlite3_exec(dbHandle(db_), sql.c_str(), callback, &prefs, &errMsg) != SQLITE_OK) {
        sqlite3_free(errMsg);
    }
    return prefs;
}

} // namespace edgefocus

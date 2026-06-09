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
    LOGI("SQLiteStore", "Database initialized.");
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

} // namespace edgefocus

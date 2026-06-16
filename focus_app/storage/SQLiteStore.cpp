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

} // namespace edgefocus

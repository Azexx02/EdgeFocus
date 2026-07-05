#pragma once
#include "app/Module.h"
#include "app/BoundedQueue.h"
#include "StorageEvent.h"
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <optional>

namespace edgefocus {

class AppContext;

class SQLiteStore : public Module {
public:
    explicit SQLiteStore(const std::string& dbPath = "edgefocus.db");
    ~SQLiteStore() override;
    
    bool init(AppContext& ctx) override;
    void runOnce(AppContext& ctx) override;
    void shutdown(AppContext& ctx) override;
    
    bool logEvent(const StorageEvent& event);
    std::vector<StorageEvent> queryEvents(const std::string& session_id);

    // Session persistence
    bool saveSession(const std::string& session_id, int64_t start_time, int64_t target_duration);
    bool updateSessionEnd(const std::string& session_id, int64_t end_time, int64_t actual_duration,
                          int64_t focus_duration, int distraction_count, int reminder_count,
                          int phone_distraction_count, const std::string& end_reason);
    bool updateSessionFeedback(const std::string& session_id, const std::string& feedback);
    bool updateSessionCounts(const std::string& session_id, int distraction_count,
                             int reminder_count, int phone_distraction_count);

    // Session history (cursor-based pagination)
    struct SessionRecord {
        int64_t rowid = 0;
        std::string session_id;
        int64_t start_time_ms = 0;
        int64_t end_time_ms = 0;
        int64_t total_duration = 0;
        int64_t focus_duration = 0;
        int distraction_count = 0;
        int reminder_count = 0;
        int phone_distraction_count = 0;
        std::string feedback;
    };
    struct SessionHistoryPage {
        std::vector<SessionRecord> sessions;
        std::string next_cursor;
        bool has_more = false;
    };
    SessionHistoryPage querySessionHistory(const std::string& cursor, int limit);
    std::optional<SessionRecord> querySessionById(const std::string& session_id);
    SessionRecord queryLastCompletedSession();
    int closeOpenSessions(int64_t end_time_ms, const std::string& end_reason);

    // Feedback
    bool saveFeedback(const std::string& session_id, const std::string& rating);
    bool sessionExists(const std::string& session_id) const;

    // Preferences
    bool savePreference(const std::string& key, const std::string& value);
    std::string getPreference(const std::string& key, const std::string& default_val = "");
    std::map<std::string, std::string> getAllPreferences();

private:
    bool migrateSchema();
    bool sessionColumnExists(const std::string& column_name) const;
    bool tableColumnExists(const std::string& table_name, const std::string& column_name) const;
    bool persistEvent(const StorageEvent& event);
    void drainPendingEvents();

    void* db_; // sqlite3* pointer
    mutable std::recursive_mutex db_mutex_;
    std::string dbPath_;
    BoundedQueue<StorageEvent> eventQueue_;
};

} // namespace edgefocus

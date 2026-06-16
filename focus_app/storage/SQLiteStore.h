#pragma once
#include "app/Module.h"
#include "app/BoundedQueue.h"
#include "StorageEvent.h"
#include <string>
#include <vector>

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

private:
    void* db_; // sqlite3* pointer
    std::string dbPath_;
    BoundedQueue<StorageEvent> eventQueue_;
};

} // namespace edgefocus

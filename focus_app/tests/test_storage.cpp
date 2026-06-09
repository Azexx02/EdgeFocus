#include "storage/SQLiteStore.h"
#include "storage/ReportGenerator.h"
#include "app/AppContext.h"
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
    
    ReportGenerator reportGen(dbPath);
    SessionReport rep = reportGen.generateReport("s1");
    
    assert(rep.session_id == "s1");
    assert(rep.distraction_count == 1);
    assert(rep.reminder_count == 1);
    
    std::cout << "Storage tests passed!\n";
    return 0;
}

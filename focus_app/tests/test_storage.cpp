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
    
    std::cout << "Storage tests passed!\n";
    return 0;
}

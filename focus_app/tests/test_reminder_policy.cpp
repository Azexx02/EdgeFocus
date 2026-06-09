#include "policy/ReminderPolicy.h"
#include "app/AppContext.h"
#include "app/SystemStateStore.h"
#include <iostream>
#include <cassert>

using namespace edgefocus;

void test_policy_escalation() {
    SystemStateStore store;
    SQLiteStore sqlite_store;
    ReminderPolicy policy(store, sqlite_store);
    AppContext ctx;
    policy.init(ctx);
    
    // Test LOOKING_AWAY -> LEVEL_1
    auto cmd1 = policy.processState(FocusState::LOOKING_AWAY, 1000);
    assert(cmd1.has_value());
    assert(cmd1->level == CommandLevel::LEVEL_1);
    assert(cmd1->message == "视线好像离开学习区了，回来看看这一页吧。");

    // Test cooling (within cooling time, should be nullopt)
    auto cmd2 = policy.processState(FocusState::LOOKING_AWAY, 2000);
    assert(!cmd2.has_value());

    // Test cooling passed, LEVEL_2 for PHONE_IN_USE
    auto cmd3 = policy.processState(FocusState::PHONE_IN_USE, 7000);
    assert(cmd3.has_value());
    assert(cmd3->level == CommandLevel::LEVEL_2);
    assert(cmd3->message == "学习期间手机正在使用，先放到一边吧。");

    std::cout << "test_policy_escalation passed\n";
}

void test_fatigue() {
    SystemStateStore store;
    SQLiteStore sqlite_store;
    ReminderPolicy policy(store, sqlite_store);
    auto cmd = policy.processState(FocusState::SLEEPY_OR_SLUMPED, 1000);
    assert(cmd.has_value());
    assert(cmd->level == CommandLevel::REST_SUGGESTION);
    assert(cmd->message == "看起来有点累，可以坐直一点，或者休息一下。");
    std::cout << "test_fatigue passed\n";
}

void test_silent_mode() {
    SystemStateStore store;
    SQLiteStore sqlite_store;
    ReminderPolicy policy(store, sqlite_store);
    policy.getModeManager().setMode(SystemMode::SILENT);
    
    // PHONE_IN_USE is LEVEL_2 normally, SILENT should downgrade to LEVEL_1
    auto cmd = policy.processState(FocusState::PHONE_IN_USE, 1000);
    assert(cmd.has_value());
    assert(cmd->level == CommandLevel::LEVEL_1);
    assert(cmd->message.find("[Silent]") != std::string::npos);
    std::cout << "test_silent_mode passed\n";
}

void test_user_ack() {
    SystemStateStore store;
    SQLiteStore sqlite_store;
    ReminderPolicy policy(store, sqlite_store);
    auto cmd1 = policy.processState(FocusState::LOOKING_AWAY, 1000);
    assert(cmd1.has_value());
    
    // User acks
    policy.onUserAck(2000);
    
    // Now cooling is base * 3 = 15000. So at 7000 it should still be nullopt
    auto cmd2 = policy.processState(FocusState::LOOKING_AWAY, 7000);
    assert(!cmd2.has_value());
    
    // At 18000 it should trigger again
    auto cmd3 = policy.processState(FocusState::LOOKING_AWAY, 18000);
    assert(cmd3.has_value());
    
    std::cout << "test_user_ack passed\n";
}

int main() {
    test_policy_escalation();
    test_fatigue();
    test_silent_mode();
    test_user_ack();
    std::cout << "All reminder policy tests passed!\n";
    return 0;
}

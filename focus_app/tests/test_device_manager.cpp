#include "device/DeviceManager.h"
#include "app/AppContext.h"
#include "app/SystemStateStore.h"
#include "policy/InterventionCommand.h"
#include <cassert>
#include <iostream>
#include <string>

using namespace edgefocus;

void test_device_manager_queue() {
    AppContext ctx;
    SystemStateStore store;
    DeviceManager dev_mgr(store);
    dev_mgr.init(ctx);

    InterventionCommand cmd;
    cmd.level = CommandLevel::LEVEL_3;
    cmd.message = "Test Alert";

    assert(dev_mgr.issueCommand(cmd) == true);

    // Process the command
    dev_mgr.runOnce(ctx);
    assert(dev_mgr.executedCommandCount() == 2);
    assert(dev_mgr.lastExecutedLevel() == CommandLevel::LEVEL_3);
    assert(dev_mgr.screenFlashCount() == 1);
    assert(dev_mgr.lastBandAlertPattern() == 0x03);
    assert(dev_mgr.lastBandAlertLevel() == 3);
    assert(dev_mgr.lastBandAlertFrame()[0] == 0x01);
    assert(dev_mgr.lastBandAlertFrame()[1] == 0x03);
    assert(dev_mgr.lastBandAlertFrame()[2] == 0x03);

    dev_mgr.shutdown(ctx);
}

void test_level2_uses_medium_band_alert() {
    AppContext ctx;
    SystemStateStore store;
    DeviceManager dev_mgr(store);
    dev_mgr.init(ctx);

    InterventionCommand cmd;
    cmd.level = CommandLevel::LEVEL_2;
    cmd.message = "Medium Alert";

    assert(dev_mgr.issueCommand(cmd) == true);
    dev_mgr.runOnce(ctx);

    assert(dev_mgr.executedCommandCount() == 2);
    assert(dev_mgr.lastExecutedLevel() == CommandLevel::LEVEL_2);
    assert(dev_mgr.screenFlashCount() == 1);
    assert(dev_mgr.lastBandAlertPattern() == 0x02);
    assert(dev_mgr.lastBandAlertLevel() == 2);
    assert(dev_mgr.lastBandAlertFrame()[0] == 0x01);
    assert(dev_mgr.lastBandAlertFrame()[1] == 0x02);
    assert(dev_mgr.lastBandAlertFrame()[2] == 0x02);

    dev_mgr.shutdown(ctx);
}

void test_action_queue_issue_band_updates_device_status() {
    AppContext ctx;
    SystemStateStore store;
    DeviceManager dev_mgr(store);
    dev_mgr.init(ctx);

    DeviceStatus initial = store.snapshot().device;
    initial.ble_enabled = true;
    store.updateDeviceStatus(initial);

    DeviceAction action{NotificationChannel::BAND, CommandLevel::LEVEL_1};
    assert(dev_mgr.issueAction(action) == true);
    dev_mgr.runOnce(ctx);

    assert(dev_mgr.executedCommandCount() == 1);
    assert(dev_mgr.lastExecutedLevel() == CommandLevel::LEVEL_1);
    auto snap = store.snapshot().device;
    assert(snap.band_last_alert_level == static_cast<int>(CommandLevel::LEVEL_1));
    assert(snap.band_last_alert_pattern == 0x01);
    auto frame = dev_mgr.lastBandAlertFrame();
    assert(frame[0] == 0x01);
    assert(frame[1] == 0x01);
    assert(frame[2] == 0x01);
    assert(snap.band_last_write_status.find("disconnected") != std::string::npos ||
           snap.band_last_write_status.find("diagnostic-only") != std::string::npos ||
           snap.band_last_write_status == "");

    dev_mgr.shutdown(ctx);
}

void test_rest_suggestion_is_clamped_for_band_alert() {
    AppContext ctx;
    SystemStateStore store;
    DeviceManager dev_mgr(store);
    dev_mgr.init(ctx);

    DeviceStatus initial = store.snapshot().device;
    initial.ble_enabled = true;
    store.updateDeviceStatus(initial);

    DeviceAction action{NotificationChannel::BAND, CommandLevel::REST_SUGGESTION};
    assert(dev_mgr.issueAction(action) == true);
    dev_mgr.runOnce(ctx);

    assert(dev_mgr.executedCommandCount() == 1);
    assert(dev_mgr.lastExecutedLevel() == CommandLevel::REST_SUGGESTION);
    auto snap = store.snapshot().device;
    assert(snap.band_last_alert_level == 3);
    assert(snap.band_last_alert_pattern == 0x03);
    auto frame = dev_mgr.lastBandAlertFrame();
    assert(frame[2] == 0x03);
    assert(frame[1] == 0x03);
    assert(frame[0] == 0x01);

    dev_mgr.shutdown(ctx);
}

int main() {
    test_device_manager_queue();
    test_level2_uses_medium_band_alert();
    test_action_queue_issue_band_updates_device_status();
    test_rest_suggestion_is_clamped_for_band_alert();
    std::cout << "All DeviceManager tests passed!\n";
    return 0;
}

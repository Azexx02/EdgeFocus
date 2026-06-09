#include "device/DeviceManager.h"
#include "app/AppContext.h"
#include "app/SystemStateStore.h"
#include "policy/InterventionCommand.h"
#include <cassert>
#include <iostream>

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

    dev_mgr.shutdown(ctx);
}

int main() {
    test_device_manager_queue();
    std::cout << "All DeviceManager tests passed!\n";
    return 0;
}

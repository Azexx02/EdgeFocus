#include "device/GimbalFaceTracker.h"

#include <cassert>
#include <iostream>

using namespace edgefocus;

GimbalTrackingInput baseInput() {
    GimbalTrackingInput input;
    input.enabled = true;
    input.gimbal_ready = true;
    input.target_valid = true;
    input.target_confidence = 0.8f;
    input.current_angle_x = 90.0f;
    input.current_angle_y = 180.0f;
    input.target_timestamp_ms = 1000;
    input.now_ms = 1000;
    return input;
}

void test_centered_target_stays_idle() {
    GimbalFaceTracker tracker;
    auto input = baseInput();
    input.target_center_x = 0.50f;
    input.target_center_y = 0.50f;
    auto command = tracker.update(input);
    assert(!command.has_value());
}

void test_off_center_target_generates_set_xy() {
    GimbalFaceTracker tracker;
    auto input = baseInput();
    input.target_center_x = 0.80f;
    input.target_center_y = 0.50f;
    auto command = tracker.update(input);
    assert(command.has_value());
    assert(command->x_deg == 90);
    assert(command->y_deg > 180);
}

void test_disabled_or_gimbal_not_ready_blocks_tracking() {
    GimbalFaceTracker tracker;
    auto input = baseInput();
    input.target_center_x = 0.80f;
    input.enabled = false;
    assert(!tracker.update(input).has_value());

    input = baseInput();
    input.gimbal_ready = false;
    assert(!tracker.update(input).has_value());
}

void test_vertical_error_changes_x_axis() {
    GimbalFaceTracker tracker;
    auto input = baseInput();
    input.target_center_x = 0.50f;
    input.target_center_y = 0.85f;
    auto command = tracker.update(input);
    assert(command.has_value());
    assert(command->x_deg > 90);
    assert(command->y_deg == 180);
}

int main() {
    test_centered_target_stays_idle();
    test_off_center_target_generates_set_xy();
    test_disabled_or_gimbal_not_ready_blocks_tracking();
    test_vertical_error_changes_x_axis();
    std::cout << "All GimbalFaceTracker tests passed!\n";
    return 0;
}

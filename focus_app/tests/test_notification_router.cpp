#include "policy/NotificationRouter.h"
#include <cassert>
#include <iostream>
#include <vector>

using namespace edgefocus;

void test_normal_route_to_multiple_channels() {
    NotificationRouter router;
    ReminderIntent intent{CommandLevel::LEVEL_2, "work", 12345};
    NotificationPreferences prefs{
        {NotificationChannel::SCREEN, NotificationChannel::BAND, NotificationChannel::SOUND},
        {NotificationChannel::SOUND}
    };

    auto actions = router.route(intent, SystemMode::NORMAL, prefs);
    assert(actions.size() == 3);
    assert(actions[0].channel == NotificationChannel::SCREEN);
    assert(actions[1].channel == NotificationChannel::BAND);
    assert(actions[2].channel == NotificationChannel::SOUND);
    assert(actions[0].level == CommandLevel::LEVEL_2);
    assert(actions[1].level == CommandLevel::LEVEL_2);
    assert(actions[2].level == CommandLevel::LEVEL_2);
}

void test_silent_route_to_single_quiet_channel_and_no_downgrade() {
    NotificationRouter router;
    ReminderIntent intent{CommandLevel::LEVEL_3, "focus", 20000};
    NotificationPreferences prefs{
        {NotificationChannel::SCREEN, NotificationChannel::BAND},
        {NotificationChannel::SOUND}
    };

    auto actions = router.route(intent, SystemMode::SILENT, prefs);
    assert(actions.size() == 1);
    assert(actions[0].channel == NotificationChannel::SOUND);
    assert(actions[0].level == CommandLevel::LEVEL_3);
}

void test_none_route() {
    NotificationRouter router;
    ReminderIntent intent{CommandLevel::NONE, "none", 30000};
    NotificationPreferences prefs{
        {NotificationChannel::SCREEN},
        {NotificationChannel::BAND}
    };

    auto actions = router.route(intent, SystemMode::NORMAL, prefs);
    assert(actions.empty());
}

void test_mask_decode_for_default_sets() {
    auto normal = notificationMaskToChannels(kNotificationChannelScreenMask | kNotificationChannelBandMask);
    auto silent = notificationMaskToChannels(kNotificationChannelBandMask);

    assert(normal.size() == 2);
    assert(normal[0] == NotificationChannel::SCREEN);
    assert(normal[1] == NotificationChannel::BAND);
    assert(silent.size() == 1);
    assert(silent[0] == NotificationChannel::BAND);
}

void test_mask_encode() {
    int mask = notificationChannelsToMask({NotificationChannel::SCREEN, NotificationChannel::LIGHT});
    assert(mask == (kNotificationChannelScreenMask | kNotificationChannelLightMask));

    auto names = notificationMaskToNames(kNotificationChannelBandMask | kNotificationChannelSoundMask);
    assert(names.size() == 2);
    assert(names[0] == "BAND");
    assert(names[1] == "SOUND");
}

int main() {
    test_normal_route_to_multiple_channels();
    test_silent_route_to_single_quiet_channel_and_no_downgrade();
    test_none_route();
    test_mask_decode_for_default_sets();
    test_mask_encode();
    std::cout << "All notification router tests passed!\n";
    return 0;
}

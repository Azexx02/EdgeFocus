#pragma once

#include "InterventionCommand.h"
#include "policy/ModeManager.h"
#include <string>
#include <vector>
#include <cstdint>

namespace edgefocus {

enum class NotificationChannel {
    SCREEN,
    BAND,
    SOUND,
    // Deprecated compatibility alias; kept for backward input mapping in migration code.
    LIGHT
};

using NotificationChannelMask = int32_t;

constexpr NotificationChannelMask kNotificationChannelScreenMask = 0x1;
constexpr NotificationChannelMask kNotificationChannelBandMask = 0x2;
constexpr NotificationChannelMask kNotificationChannelSoundMask = 0x4;
constexpr NotificationChannelMask kNotificationChannelLightMask = 0x8;

struct ReminderIntent {
    CommandLevel level{CommandLevel::NONE};
    std::string message;
    int64_t timestamp_ms{0};
};

struct NotificationPreferences {
    std::vector<NotificationChannel> normal_channels;
    std::vector<NotificationChannel> silent_channels;
};

NotificationChannelMask notificationChannelsToMask(
    const std::vector<NotificationChannel>& channels);

std::vector<NotificationChannel> notificationMaskToChannels(int32_t mask);

std::vector<std::string> notificationMaskToNames(int32_t mask);

struct DeviceAction {
    NotificationChannel channel{NotificationChannel::SCREEN};
    CommandLevel level{CommandLevel::NONE};
};

class NotificationRouter {
public:
    std::vector<DeviceAction> route(const ReminderIntent& intent,
                                    SystemMode mode,
                                    const NotificationPreferences& prefs) const;
};

} // namespace edgefocus

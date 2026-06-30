#include "NotificationRouter.h"

namespace edgefocus {

NotificationChannelMask notificationChannelsToMask(
    const std::vector<NotificationChannel>& channels) {
    NotificationChannelMask mask = 0;
    for (NotificationChannel channel : channels) {
        switch (channel) {
            case NotificationChannel::SCREEN:
                mask |= kNotificationChannelScreenMask;
                break;
            case NotificationChannel::BAND:
                mask |= kNotificationChannelBandMask;
                break;
            case NotificationChannel::SOUND:
                mask |= kNotificationChannelSoundMask;
                break;
            case NotificationChannel::LIGHT:
                mask |= kNotificationChannelLightMask;
                break;
            default:
                break;
        }
    }
    return mask;
}

std::vector<NotificationChannel> notificationMaskToChannels(int32_t mask) {
    std::vector<NotificationChannel> channels;
    if (mask & kNotificationChannelScreenMask) channels.push_back(NotificationChannel::SCREEN);
    if (mask & kNotificationChannelBandMask) channels.push_back(NotificationChannel::BAND);
    if (mask & kNotificationChannelSoundMask) channels.push_back(NotificationChannel::SOUND);
    if (mask & kNotificationChannelLightMask) channels.push_back(NotificationChannel::LIGHT);
    return channels;
}

std::vector<std::string> notificationMaskToNames(int32_t mask) {
    std::vector<std::string> names;
    if (mask & kNotificationChannelScreenMask) names.push_back("SCREEN");
    if (mask & kNotificationChannelBandMask) names.push_back("BAND");
    if (mask & kNotificationChannelSoundMask) names.push_back("SOUND");
    if (mask & kNotificationChannelLightMask) names.push_back("LIGHT");
    return names;
}

std::vector<DeviceAction> NotificationRouter::route(const ReminderIntent& intent,
                                                   SystemMode mode,
                                                   const NotificationPreferences& prefs) const {
    if (intent.level == CommandLevel::NONE) {
        return {};
    }

    const auto& channels = (mode == SystemMode::SILENT) ? prefs.silent_channels : prefs.normal_channels;
    std::vector<DeviceAction> actions;
    actions.reserve(channels.size());

    for (NotificationChannel channel : channels) {
        actions.push_back({channel, intent.level});
    }

    return actions;
}

} // namespace edgefocus

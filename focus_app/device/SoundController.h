#pragma once
#include "app/Logger.h"
#include <atomic>
#include <string>

namespace edgefocus {

class SoundController {
public:
    SoundController() = default;
    ~SoundController() = default;

    // Test hook/environment guard: disable real playback in unit tests or CI.
    static bool playbackEnabledForTests();
    static void setPlaybackEnabledForTests(bool enabled);

    /// Trigger a short reminder sound asynchronously.
    void playReminderSound();

    int playCount() const { return play_count_.load(); }

private:
    static bool isPlaybackAllowed();
    static bool isEnvPlaybackDisabled();
    static bool hasCommand(const std::string& command);
    static bool launchCommand(const std::string& command);
    static void playReminderSoundAsync();

    static std::atomic<bool> playback_enabled_for_tests_;
    static std::atomic<bool> in_flight_;
    std::atomic<int> play_count_{0};
};

} // namespace edgefocus

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

    /// Ensure a short non-speech WAV reminder tone exists at path.
    static bool ensureReminderToneFile(const std::string& path);

    /// Trigger a short reminder sound asynchronously.
    void playReminderSound(float volume);

    int playCount() const { return play_count_.load(); }

private:
    static bool isPlaybackAllowed();
    static bool isEnvPlaybackDisabled();
    static bool hasAlsaPlaybackDevice();
    static bool hasCommand(const std::string& command);
    static bool launchCommand(const std::string& command);
    static int volumeToPercent(float volume);
    static std::string mixerPrefix(int volume_percent);
    static void playReminderSoundAsync(int volume_percent);

    static std::atomic<bool> playback_enabled_for_tests_;
    static std::atomic<bool> in_flight_;
    std::atomic<int> play_count_{0};
};

} // namespace edgefocus

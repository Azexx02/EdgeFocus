#include "SoundController.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <cctype>
#include <string>
#include <system_error>
#include <thread>

namespace edgefocus {
namespace {

bool isRegularFile(const std::string& path) {
    std::ifstream fs(path);
    return fs.is_open();
}

bool envVarMeansDisabled(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    const std::string lower = [&value]() {
        std::string out = value;
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return out;
    }();
    return lower == "1" || lower == "true" || lower == "yes" || lower == "on" || lower == "disable";
}

} // namespace

std::atomic<bool> SoundController::playback_enabled_for_tests_{true};
std::atomic<bool> SoundController::in_flight_{false};

bool SoundController::playbackEnabledForTests() {
    return playback_enabled_for_tests_.load();
}

void SoundController::setPlaybackEnabledForTests(bool enabled) {
    playback_enabled_for_tests_ = enabled;
}

bool SoundController::isEnvPlaybackDisabled() {
    const char* value = std::getenv("EDGEFOCUS_DISABLE_SOUND");
    if (!value) {
        return false;
    }
    return envVarMeansDisabled(value);
}

bool SoundController::isPlaybackAllowed() {
    return playbackEnabledForTests() && !isEnvPlaybackDisabled();
}

bool SoundController::hasCommand(const std::string& command) {
#if defined(__linux__)
    const std::string probe = "command -v " + command + " >/dev/null 2>&1";
    return std::system(probe.c_str()) == 0;
#else
    return false;
#endif
}

bool SoundController::launchCommand(const std::string& command) {
#if defined(__linux__)
    const int rc = std::system(command.c_str());
    return rc == 0;
#else
    return false;
#endif
}

void SoundController::playReminderSoundAsync() {
#if defined(__linux__)
    const std::string sample = "/usr/share/sounds/alsa/Front_Center.wav";
    if (isRegularFile(sample) && hasCommand("aplay")) {
        if (launchCommand("aplay -q " + sample + " >/dev/null 2>&1 &")) {
            LOGI("SoundController", "Played reminder sound via aplay");
            return;
        }
    }

    if (hasCommand("speaker-test")) {
        if (launchCommand("speaker-test -t sine -f 1000 -l 1 >/dev/null 2>&1 &")) {
            LOGI("SoundController", "Played reminder sound via speaker-test");
            return;
        }
    }

    if (hasCommand("beep")) {
        if (launchCommand("beep -f 1200 -l 150 >/dev/null 2>&1 &")) {
            LOGI("SoundController", "Played reminder sound via beep");
            return;
        }
    }

    LOGW("SoundController", "No available ALSA-like command for reminder sound");
#else
    LOGI("SoundController", "Sound reminders are Linux-only in this build");
#endif
}

void SoundController::playReminderSound() {
    ++play_count_;
    if (!isPlaybackAllowed()) {
        LOGW("SoundController", "Sound playback disabled; skip reminder sound");
        return;
    }
    if (in_flight_.exchange(true)) {
        LOGW("SoundController", "Reminder sound already in flight; skip duplicate request");
        return;
    }

    try {
        std::thread([]() {
            try {
                playReminderSoundAsync();
            } catch (const std::exception& ex) {
                LOGE("SoundController", "Reminder sound task failed: %s", ex.what());
            } catch (...) {
                LOGE("SoundController", "Reminder sound task failed with unknown exception");
            }
            in_flight_ = false;
        }).detach();
    } catch (const std::system_error& e) {
        in_flight_ = false;
        LOGE("SoundController", "Failed to spawn reminder sound thread: %s", e.what());
    } catch (const std::exception& e) {
        in_flight_ = false;
        LOGE("SoundController", "Failed to spawn reminder sound thread: %s", e.what());
    } catch (...) {
        in_flight_ = false;
        LOGE("SoundController", "Failed to spawn reminder sound thread with unknown error");
    }
}

} // namespace edgefocus

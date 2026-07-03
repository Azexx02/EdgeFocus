#include "SoundController.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <filesystem>
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

void writeLe16(std::ofstream& out, std::uint16_t value) {
    out.put(static_cast<char>(value & 0xff));
    out.put(static_cast<char>((value >> 8) & 0xff));
}

void writeLe32(std::ofstream& out, std::uint32_t value) {
    writeLe16(out, static_cast<std::uint16_t>(value & 0xffff));
    writeLe16(out, static_cast<std::uint16_t>((value >> 16) & 0xffff));
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

bool SoundController::isAvailable() {
    return isPlaybackAllowed() && hasAlsaPlaybackDevice();
}

bool SoundController::hasCommand(const std::string& command) {
#if defined(__linux__)
    const std::string probe = "command -v " + command + " >/dev/null 2>&1";
    return std::system(probe.c_str()) == 0;
#else
    return false;
#endif
}

bool SoundController::hasAlsaPlaybackDevice() {
#if defined(__linux__)
    std::error_code ec;
    if (std::filesystem::exists("/proc/asound/cards", ec) && !ec) {
        std::ifstream cards("/proc/asound/cards");
        if (cards.is_open()) {
            return true;
        }
    }

    ec.clear();
    if (std::filesystem::exists("/dev/snd", ec) && !ec && std::filesystem::is_directory("/dev/snd", ec) && !ec) {
        return true;
    }

    return false;
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

int SoundController::volumeToPercent(float volume) {
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    return static_cast<int>(volume * 100.0f + 0.5f);
}

std::string SoundController::mixerPrefix(int volume_percent) {
#if defined(__linux__)
    if (!hasCommand("amixer")) {
        LOGW("SoundController", "amixer not available; sound volume control skipped");
        return "";
    }
    return "amixer -q sset Master " + std::to_string(volume_percent) + "% >/dev/null 2>&1 || true; ";
#else
    (void)volume_percent;
    return "";
#endif
}

bool SoundController::ensureReminderToneFile(const std::string& path) {
#if defined(__linux__)
    if (isRegularFile(path)) {
        return true;
    }

    constexpr int kSampleRate = 16000;
    constexpr int kDurationMs = 180;
    constexpr int kFrequencyHz = 1200;
    constexpr double kAmplitude = 0.35;
    constexpr int kSamples = kSampleRate * kDurationMs / 1000;
    constexpr int kChannels = 1;
    constexpr int kBitsPerSample = 16;
    constexpr int kBlockAlign = kChannels * kBitsPerSample / 8;
    constexpr int kByteRate = kSampleRate * kBlockAlign;
    constexpr std::uint32_t kDataSize = kSamples * kBlockAlign;
    constexpr std::uint32_t kRiffSize = 36 + kDataSize;

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        LOGW("SoundController", "Failed to create reminder tone file: %s", path.c_str());
        return false;
    }

    out.write("RIFF", 4);
    writeLe32(out, kRiffSize);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    writeLe32(out, 16);
    writeLe16(out, 1);
    writeLe16(out, kChannels);
    writeLe32(out, kSampleRate);
    writeLe32(out, kByteRate);
    writeLe16(out, kBlockAlign);
    writeLe16(out, kBitsPerSample);
    out.write("data", 4);
    writeLe32(out, kDataSize);

    constexpr double kTwoPi = 6.28318530717958647692;
    for (int i = 0; i < kSamples; ++i) {
        const double phase = kTwoPi * static_cast<double>(kFrequencyHz) *
                             static_cast<double>(i) / static_cast<double>(kSampleRate);
        const double envelope = std::min(1.0, std::min(i / 240.0, (kSamples - i) / 240.0));
        const auto sample = static_cast<std::int16_t>(
            std::sin(phase) * kAmplitude * envelope * 32767.0);
        writeLe16(out, static_cast<std::uint16_t>(sample));
    }

    return out.good();
#else
    (void)path;
    return false;
#endif
}

void SoundController::playReminderSoundAsync(int volume_percent) {
#if defined(__linux__)
    if (!hasAlsaPlaybackDevice()) {
        LOGW("SoundController", "No ALSA playback device found; skip reminder sound");
        return;
    }
    const std::string volume_prefix = mixerPrefix(volume_percent);
    const std::string sample = "/tmp/edgefocus_reminder_tone.wav";
    if (ensureReminderToneFile(sample) && hasCommand("aplay")) {
        if (launchCommand(volume_prefix + "aplay -q " + sample + " >/dev/null 2>&1 &")) {
            LOGI("SoundController", "Played reminder sound via aplay (volume=%d%%)", volume_percent);
            return;
        }
    }

    if (hasCommand("speaker-test")) {
        if (launchCommand(volume_prefix + "speaker-test -t sine -f 1000 -l 1 >/dev/null 2>&1 &")) {
            LOGI("SoundController", "Played reminder sound via speaker-test (volume=%d%%)", volume_percent);
            return;
        }
    }

    if (hasCommand("beep")) {
        if (launchCommand(volume_prefix + "beep -f 1200 -l 150 >/dev/null 2>&1 &")) {
            LOGI("SoundController", "Played reminder sound via beep (volume=%d%%)", volume_percent);
            return;
        }
    }

    LOGW("SoundController", "No available ALSA-like command for reminder sound");
#else
    LOGI("SoundController", "Sound reminders are Linux-only in this build");
#endif
}

void SoundController::playReminderSound(float volume) {
    ++play_count_;
    if (!isPlaybackAllowed()) {
        LOGW("SoundController", "Sound playback disabled; skip reminder sound");
        return;
    }
    if (in_flight_.exchange(true)) {
        LOGW("SoundController", "Reminder sound already in flight; skip duplicate request");
        return;
    }

    const int volume_percent = volumeToPercent(volume);
    try {
        std::thread([volume_percent]() {
            try {
                playReminderSoundAsync(volume_percent);
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

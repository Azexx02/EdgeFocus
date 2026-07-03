#pragma once

#include "behavior/EventDetector.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <nlohmann/json.hpp>

namespace edgefocus {

namespace {
std::string lowerAsciiCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}
} // namespace

struct ParsedPhoneSync {
    bool active = false;
    int distraction_level = 1;
    PhoneUsageState usage_state = PhoneUsageState::IN_USE;
};

inline int normalizePhoneDistractionLevel(int level) {
    if (level < 0) return 0;
    if (level > 3) return 3;
    return level;
}

inline PhoneUsageState parsePhoneUsageState(const nlohmann::json& data, int distraction_level = 1) {
    std::string raw;
    if (data.contains("phone_state")) {
        raw = data.value("phone_state", "");
    } else if (data.contains("usage_state")) {
        raw = data.value("usage_state", "");
    } else if (data.contains("state")) {
        raw = data.value("state", "");
    } else if (data.contains("app_category")) {
        raw = data.value("app_category", "");
    }

    raw = lowerAsciiCopy(raw);
    if (raw == "learning" || raw == "study" || raw == "education" ||
        raw == "phone_in_learning") {
        return PhoneUsageState::LEARNING;
    }
    if (raw == "playing" || raw == "game" || raw == "games" ||
        raw == "video" || raw == "short_video" || raw == "short-video" ||
        raw == "entertainment" || raw == "phone_in_playing") {
        return PhoneUsageState::PLAYING;
    }

    if (raw.empty() && distraction_level == 0) {
        return PhoneUsageState::LEARNING;
    }

    return PhoneUsageState::IN_USE;
}

inline ParsedPhoneSync parsePhoneSyncPayload(const nlohmann::json& data,
                                            bool active_fallback = false) {
    ParsedPhoneSync parsed;
    if (data.contains("is_in_use")) {
        parsed.active = data.value("is_in_use", active_fallback);
    } else if (data.contains("active")) {
        parsed.active = data.value("active", active_fallback);
    } else {
        parsed.active = active_fallback;
    }
    parsed.distraction_level = normalizePhoneDistractionLevel(data.value("distraction_level", 1));
    parsed.usage_state = parsePhoneUsageState(data, parsed.distraction_level);
    return parsed;
}

} // namespace edgefocus

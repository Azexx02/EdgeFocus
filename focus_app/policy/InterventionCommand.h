#pragma once
#include <cstdint>
#include <string>

namespace edgefocus {

enum class CommandLevel : uint8_t {
    NONE = 0,
    LEVEL_1 = 1,
    LEVEL_2 = 2,
    LEVEL_3 = 3,
    REST_SUGGESTION = 4
};

struct InterventionCommand {
    int64_t timestamp_ms{0};
    CommandLevel level{CommandLevel::NONE};
    std::string message;
};

} // namespace edgefocus

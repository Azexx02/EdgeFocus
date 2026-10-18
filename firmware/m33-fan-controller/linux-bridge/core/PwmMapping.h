#pragma once

#include <cstdint>

namespace edgefocus::fan {

// FanControlCore keeps the Linux prototype's logical duty convention: a
// smaller duty means more cooling. TIM17 drives the board through CH1N, so the
// complementary output requires an inverted compare value.
constexpr bool logicalDutyToComplementaryCompareTicks(
    std::uint32_t duty_ns,
    std::uint32_t period_ns,
    std::uint32_t tick_ns,
    std::uint32_t& compare_ticks) {
    if (tick_ns == 0 || period_ns == 0 || duty_ns > period_ns ||
        (period_ns % tick_ns) != 0 || (duty_ns % tick_ns) != 0) {
        return false;
    }

    const std::uint32_t period_ticks = period_ns / tick_ns;
    const std::uint32_t logical_ticks = duty_ns / tick_ns;
    compare_ticks = period_ticks - logical_ticks;
    return true;
}

}  // namespace edgefocus::fan

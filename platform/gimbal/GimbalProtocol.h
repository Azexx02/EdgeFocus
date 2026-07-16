#pragma once

#include "platform/gimbal/GimbalTypes.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace edgefocus::gimbal {

class LineFramer {
public:
    explicit LineFramer(std::size_t maximum_line_bytes = 256);

    std::vector<std::string> feed(std::string_view bytes);
    void reset();
    std::size_t droppedLines() const noexcept { return dropped_lines_; }

private:
    std::size_t maximum_line_bytes_;
    std::string buffer_;
    bool dropping_line_{};
    std::size_t dropped_lines_{};
};

Event parseEvent(std::string_view line);
bool isHealthyPingEvent(const Event& event) noexcept;
bool isHealthyFaultEvent(const Event& event) noexcept;
bool isHealthyStatusEvent(const Event& event) noexcept;

std::string diagnosticCommand(EventKind expected);
std::string armCommand();
std::string stopCommand();
std::string holdCommand();
std::string moveCommand(double yaw_deg, double pitch_deg);
std::string trackCommand(double yaw_deg, double pitch_deg);
double normalizeAngle(double angle_deg) noexcept;
double shortestAngleDelta(double from_deg, double to_deg) noexcept;
double offsetAngle(double reference_deg, double offset_deg) noexcept;

}  // namespace edgefocus::gimbal

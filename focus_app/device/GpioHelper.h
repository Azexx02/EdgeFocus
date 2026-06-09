#pragma once

#include <string>

namespace edgefocus {
namespace device {

struct GpioLine {
    int chip_fd = -1;
    int handle_fd = -1;
    std::string name;

    bool isValid() const {
        return chip_fd >= 0 && handle_fd >= 0;
    }
};

class GpioHelper {
public:
    // Opens a GPIO line by its name (e.g., "PB4"). 
    // If output is true, it is set as an output pin with default_value.
    static GpioLine openLine(const std::string& name, bool output, int default_value);

    // Sets the value of an opened output line
    static void setValue(const GpioLine& line, int value);

    // Closes and releases the GPIO line
    static void closeLine(GpioLine& line);

private:
    static int findLine(const std::string& name, char* chip_path, size_t chip_len, unsigned int& offset);
};

} // namespace device
} // namespace edgefocus

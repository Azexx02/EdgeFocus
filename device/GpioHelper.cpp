#include "GpioHelper.h"
#include "app/Logger.h"

#include <cstring>
#include <cstdint>

#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>
#endif

namespace edgefocus {
namespace device {

int GpioHelper::findLine(const std::string& name, char* chip_path, size_t chip_len, unsigned int& offset) {
#ifdef __linux__
    for (int chip = 0; chip < 16; chip++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/gpiochip%d", chip);
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;

        struct gpiochip_info chip_info;
        if (ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &chip_info) < 0) {
            close(fd);
            continue;
        }

        for (uint32_t i = 0; i < chip_info.lines; i++) {
            struct gpioline_info info;
            memset(&info, 0, sizeof(info));
            info.line_offset = i;
            if (ioctl(fd, GPIO_GET_LINEINFO_IOCTL, &info) < 0) continue;

            if (name == info.name || strncmp(info.name, name.c_str(), name.length()) == 0) {
                snprintf(chip_path, chip_len, "%s", path);
                offset = i;
                close(fd);
                return 0;
            }
        }
        close(fd);
    }
#else
    (void)name;
    (void)chip_path;
    (void)chip_len;
    (void)offset;
#endif
    return -1;
}

GpioLine GpioHelper::openLine(const std::string& name, bool output, int default_value) {
    GpioLine line;
    line.name = name;

#ifdef __linux__
    char chip_path[64];
    unsigned int offset = 0;
    
    if (findLine(name, chip_path, sizeof(chip_path), offset) < 0) {
        LOGE("GpioHelper", "Could not find GPIO line: %s", name.c_str());
        return line;
    }

    line.chip_fd = open(chip_path, O_RDONLY | O_CLOEXEC);
    if (line.chip_fd < 0) {
        LOGE("GpioHelper", "Failed to open %s", chip_path);
        return line;
    }

    struct gpiohandle_request req;
    memset(&req, 0, sizeof(req));
    req.lineoffsets[0] = offset;
    req.lines = 1;
    req.flags = output ? GPIOHANDLE_REQUEST_OUTPUT : GPIOHANDLE_REQUEST_INPUT;
    req.default_values[0] = default_value ? 1 : 0;
    snprintf(req.consumer_label, sizeof(req.consumer_label), "focus-app-%s", name.c_str());

    if (ioctl(line.chip_fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        LOGE("GpioHelper", "Failed to get handle for GPIO %s", name.c_str());
        close(line.chip_fd);
        line.chip_fd = -1;
        return line;
    }
    
    line.handle_fd = req.fd;
    LOGI("GpioHelper", "Opened GPIO %s on %s offset %u", name.c_str(), chip_path, offset);
#else
    (void)output;
    (void)default_value;
#endif
    return line;
}

void GpioHelper::setValue(const GpioLine& line, int value) {
    (void)trySetValue(line, value, nullptr);
}

bool GpioHelper::trySetValue(const GpioLine& line, int value, std::string* error) {
#ifdef __linux__
    if (line.handle_fd < 0) {
        if (error) *error = "invalid GPIO handle: " + line.name;
        return false;
    }

    struct gpiohandle_data data;
    memset(&data, 0, sizeof(data));
    data.values[0] = value ? 1 : 0;
    if (ioctl(line.handle_fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data) < 0) {
        LOGE("GpioHelper", "Failed to set GPIO value for %s", line.name.c_str());
        if (error) *error = "failed to set GPIO: " + line.name;
        return false;
    }
    if (error) error->clear();
    return true;
#else
    (void)line;
    (void)value;
    if (error) *error = "GPIO unavailable on this platform";
    return false;
#endif
}

bool GpioHelper::readValue(const GpioLine& line, int& value, std::string* error) {
#ifdef __linux__
    if (line.handle_fd < 0) {
        if (error) *error = "invalid GPIO handle: " + line.name;
        return false;
    }
    struct gpiohandle_data data;
    memset(&data, 0, sizeof(data));
    if (ioctl(line.handle_fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) < 0) {
        if (error) *error = "failed to read GPIO: " + line.name;
        return false;
    }
    value = data.values[0] ? 1 : 0;
    if (error) error->clear();
    return true;
#else
    (void)line;
    value = 0;
    if (error) *error = "GPIO unavailable on this platform";
    return false;
#endif
}

void GpioHelper::closeLine(GpioLine& line) {
#ifdef __linux__
    if (line.handle_fd >= 0) {
        close(line.handle_fd);
        line.handle_fd = -1;
    }
    if (line.chip_fd >= 0) {
        close(line.chip_fd);
        line.chip_fd = -1;
    }
#else
    (void)line;
#endif
}

} // namespace device
} // namespace edgefocus

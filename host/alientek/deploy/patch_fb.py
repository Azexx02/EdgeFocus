import sys
import re

with open(r'C:\Users\Aze\Desktop\edgefocus(1)\开发板部分代码\focus_app\ui\FrameBuffer.cpp', 'r', encoding='utf-8') as f:
    code = f.read()

spi_includes = """
#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <linux/gpio.h>
#include <linux/spi/spidev.h>
#include <time.h>

struct GpioLine {
    int chip_fd = -1;
    int handle_fd = -1;
    char name[32] = {0};
};

namespace edgefocus {
struct SpiLcdContext {
    int spi_fd = -1;
    GpioLine cs;
    GpioLine dc;
    GpioLine rst;
    GpioLine bl;
};
} // namespace edgefocus

static void msleep_fb(unsigned ms) {
    struct timespec ts = { .tv_sec = (time_t)(ms / 1000), .tv_nsec = (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, nullptr);
}

static int starts_with_fb(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static int gpio_find_line_fb(const char *name, char *chip_path, size_t chip_len, unsigned int *offset) {
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
            if (strcmp(info.name, name) == 0 || starts_with_fb(info.name, name)) {
                snprintf(chip_path, chip_len, "%s", path);
                *offset = i;
                close(fd);
                return 0;
            }
        }
        close(fd);
    }
    return -1;
}

static GpioLine gpio_open_line_fb(const char *name, int output, int value) {
    char chip_path[64];
    unsigned int offset;
    GpioLine line;
    snprintf(line.name, sizeof(line.name), "%s", name);

    if (gpio_find_line_fb(name, chip_path, sizeof(chip_path), &offset) < 0) return line;

    line.chip_fd = open(chip_path, O_RDONLY | O_CLOEXEC);
    if (line.chip_fd < 0) return line;

    struct gpiohandle_request req;
    memset(&req, 0, sizeof(req));
    req.lineoffsets[0] = offset;
    req.lines = 1;
    req.flags = output ? GPIOHANDLE_REQUEST_OUTPUT : GPIOHANDLE_REQUEST_INPUT;
    req.default_values[0] = value ? 1 : 0;
    snprintf(req.consumer_label, sizeof(req.consumer_label), "focus-ui-%s", name);

    if (ioctl(line.chip_fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) return line;
    line.handle_fd = req.fd;
    return line;
}

static void gpio_set_fb(GpioLine *line, int value) {
    if (line->handle_fd < 0) return;
    struct gpiohandle_data data;
    memset(&data, 0, sizeof(data));
    data.values[0] = value ? 1 : 0;
    ioctl(line->handle_fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);
}

static void gpio_close_fb(GpioLine *line) {
    if (line->handle_fd >= 0) close(line->handle_fd);
    if (line->chip_fd >= 0) close(line->chip_fd);
    line->handle_fd = -1;
    line->chip_fd = -1;
}

static int spi_open_config_fb(const char *path, int speed_hz, int spi_mode) {
    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) return -1;
    uint8_t mode = (uint8_t)spi_mode;
    uint8_t bits = 8;
    ioctl(fd, SPI_IOC_WR_MODE, &mode);
    ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed_hz);
    return fd;
}

static void spi_write_all_fb(int fd, const uint8_t *buf, size_t len) {
    while (len > 0) {
        size_t chunk = len > 4096 ? 4096 : len;
        ssize_t n = write(fd, buf, chunk);
        if (n <= 0) break;
        buf += n;
        len -= (size_t)n;
    }
}

static void lcd_cmd_fb(int spi_fd, GpioLine *cs, GpioLine *dc, uint8_t cmd) {
    gpio_set_fb(cs, 0); gpio_set_fb(dc, 0);
    spi_write_all_fb(spi_fd, &cmd, 1);
    gpio_set_fb(cs, 1);
}

static void lcd_data_fb(int spi_fd, GpioLine *cs, GpioLine *dc, const uint8_t *data, size_t len) {
    gpio_set_fb(cs, 0); gpio_set_fb(dc, 1);
    spi_write_all_fb(spi_fd, data, len);
    gpio_set_fb(cs, 1);
}

static void lcd_data8_fb(int spi_fd, GpioLine *cs, GpioLine *dc, uint8_t data) {
    lcd_data_fb(spi_fd, cs, dc, &data, 1);
}

static void lcd_init_ili9341_fb(int spi_fd, GpioLine *cs, GpioLine *dc, GpioLine *rst) {
    gpio_set_fb(rst, 1); msleep_fb(20);
    gpio_set_fb(rst, 0); msleep_fb(80);
    gpio_set_fb(rst, 1); msleep_fb(120);

    lcd_cmd_fb(spi_fd, cs, dc, 0x01); msleep_fb(120);
    lcd_cmd_fb(spi_fd, cs, dc, 0xCF);
    { uint8_t d[] = { 0x00, 0xC1, 0x30 }; lcd_data_fb(spi_fd, cs, dc, d, sizeof(d)); }
    lcd_cmd_fb(spi_fd, cs, dc, 0xED);
    { uint8_t d[] = { 0x64, 0x03, 0x12, 0x81 }; lcd_data_fb(spi_fd, cs, dc, d, sizeof(d)); }
    lcd_cmd_fb(spi_fd, cs, dc, 0xE8);
    { uint8_t d[] = { 0x85, 0x00, 0x78 }; lcd_data_fb(spi_fd, cs, dc, d, sizeof(d)); }
    lcd_cmd_fb(spi_fd, cs, dc, 0xCB);
    { uint8_t d[] = { 0x39, 0x2C, 0x00, 0x34, 0x02 }; lcd_data_fb(spi_fd, cs, dc, d, sizeof(d)); }
    lcd_cmd_fb(spi_fd, cs, dc, 0xF7); lcd_data8_fb(spi_fd, cs, dc, 0x20);
    lcd_cmd_fb(spi_fd, cs, dc, 0xEA);
    { uint8_t d[] = { 0x00, 0x00 }; lcd_data_fb(spi_fd, cs, dc, d, sizeof(d)); }
    lcd_cmd_fb(spi_fd, cs, dc, 0xC0); lcd_data8_fb(spi_fd, cs, dc, 0x23);
    lcd_cmd_fb(spi_fd, cs, dc, 0xC1); lcd_data8_fb(spi_fd, cs, dc, 0x10);
    lcd_cmd_fb(spi_fd, cs, dc, 0xC5);
    { uint8_t d[] = { 0x3E, 0x28 }; lcd_data_fb(spi_fd, cs, dc, d, sizeof(d)); }
    lcd_cmd_fb(spi_fd, cs, dc, 0xC7); lcd_data8_fb(spi_fd, cs, dc, 0x86);
    lcd_cmd_fb(spi_fd, cs, dc, 0x3A); lcd_data8_fb(spi_fd, cs, dc, 0x55);
    lcd_cmd_fb(spi_fd, cs, dc, 0x36); lcd_data8_fb(spi_fd, cs, dc, 0x48); // MADCTL
    lcd_cmd_fb(spi_fd, cs, dc, 0xB1);
    { uint8_t d[] = { 0x00, 0x18 }; lcd_data_fb(spi_fd, cs, dc, d, sizeof(d)); }
    lcd_cmd_fb(spi_fd, cs, dc, 0xB6);
    { uint8_t d[] = { 0x08, 0x82, 0x27 }; lcd_data_fb(spi_fd, cs, dc, d, sizeof(d)); }
    lcd_cmd_fb(spi_fd, cs, dc, 0xF2); lcd_data8_fb(spi_fd, cs, dc, 0x00);
    lcd_cmd_fb(spi_fd, cs, dc, 0x26); lcd_data8_fb(spi_fd, cs, dc, 0x01);
    lcd_cmd_fb(spi_fd, cs, dc, 0xE0);
    { uint8_t d[] = { 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00 }; lcd_data_fb(spi_fd, cs, dc, d, sizeof(d)); }
    lcd_cmd_fb(spi_fd, cs, dc, 0xE1);
    { uint8_t d[] = { 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F }; lcd_data_fb(spi_fd, cs, dc, d, sizeof(d)); }
    lcd_cmd_fb(spi_fd, cs, dc, 0x11); msleep_fb(120);
    lcd_cmd_fb(spi_fd, cs, dc, 0x29); msleep_fb(20);
}

static void lcd_set_window_fb(int spi_fd, GpioLine *cs, GpioLine *dc, int x0, int y0, int x1, int y1) {
    uint8_t buf[4];
    lcd_cmd_fb(spi_fd, cs, dc, 0x2A);
    buf[0] = (uint8_t)(x0 >> 8); buf[1] = (uint8_t)x0;
    buf[2] = (uint8_t)(x1 >> 8); buf[3] = (uint8_t)x1;
    lcd_data_fb(spi_fd, cs, dc, buf, sizeof(buf));

    lcd_cmd_fb(spi_fd, cs, dc, 0x2B);
    buf[0] = (uint8_t)(y0 >> 8); buf[1] = (uint8_t)y0;
    buf[2] = (uint8_t)(y1 >> 8); buf[3] = (uint8_t)y1;
    lcd_data_fb(spi_fd, cs, dc, buf, sizeof(buf));
    lcd_cmd_fb(spi_fd, cs, dc, 0x2C);
}

#else
namespace edgefocus {
struct SpiLcdContext {
};
} // namespace edgefocus
#endif
"""

# Replace the linux includes section
code = re.sub(r'#ifdef __linux__\s*#include <fcntl\.h>.*?#endif', spi_includes, code, flags=re.DOTALL)

# Add initSpiLcd and flushSpiLcd
spi_methods = """
bool FrameBuffer::initSpiLcd() {
#ifdef __linux__
    int fd = spi_open_config_fb("/dev/spidev0.0", 24000000, 0);
    if (fd < 0) return false;

    spi_ctx_ = new SpiLcdContext();
    spi_ctx_->spi_fd = fd;
    spi_ctx_->cs = gpio_open_line_fb("PB0", 1, 1);
    spi_ctx_->rst = gpio_open_line_fb("PB1", 1, 1);
    spi_ctx_->dc = gpio_open_line_fb("PF10", 1, 0);
    spi_ctx_->bl = gpio_open_line_fb("PD8", 1, 1);

    lcd_init_ili9341_fb(spi_ctx_->spi_fd, &spi_ctx_->cs, &spi_ctx_->dc, &spi_ctx_->rst);
    return true;
#else
    return false;
#endif
}

void FrameBuffer::flushSpiLcd() {
#ifdef __linux__
    if (!spi_ctx_ || spi_ctx_->spi_fd < 0 || width_ != 720 || height_ != 1280) return;

    // Downscale 720x1280 to 240x320 and convert to RGB565
    int out_w = 240;
    int out_h = 320;
    uint8_t* line_buf = new uint8_t[out_w * 2];

    lcd_set_window_fb(spi_ctx_->spi_fd, &spi_ctx_->cs, &spi_ctx_->dc, 0, 0, out_w - 1, out_h - 1);
    gpio_set_fb(&spi_ctx_->cs, 0);
    gpio_set_fb(&spi_ctx_->dc, 1);

    for (int y = 0; y < out_h; y++) {
        for (int x = 0; x < out_w; x++) {
            uint32_t argb = buffer_[(y * 4) * width_ + (x * 3)];
            uint8_t r = (argb >> 16) & 0xFF;
            uint8_t g = (argb >> 8) & 0xFF;
            uint8_t b = argb & 0xFF;
            uint16_t rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
            line_buf[x * 2] = (uint8_t)(rgb565 >> 8);
            line_buf[x * 2 + 1] = (uint8_t)(rgb565 & 0xFF);
        }
        spi_write_all_fb(spi_ctx_->spi_fd, line_buf, out_w * 2);
    }

    gpio_set_fb(&spi_ctx_->cs, 1);
    delete[] line_buf;
#endif
}
"""

# Insert methods into FrameBuffer implementation
code = code.replace('FrameBuffer::FrameBuffer() = default;', spi_methods + '\nFrameBuffer::FrameBuffer() = default;')

# Modify init to call initSpiLcd
init_hook = """    // Fallback: offscreen buffer (default 720x1280 for MIPI LCD)
    width_ = 720;
    height_ = 1280;
    buffer_.resize(width_ * height_, 0);
    real_fb_ = false;
    LOGI("FrameBuffer", "Using offscreen buffer: %dx%d", width_, height_);
    
#ifdef __linux__
    if (initSpiLcd()) {
        LOGI("FrameBuffer", "Initialized userspace SPI LCD driver (240x320 screen downscaling)");
    }
#endif
    return true;"""
code = code.replace('    // Fallback: offscreen buffer (default 720x1280 for MIPI LCD)\n    width_ = 720;\n    height_ = 1280;\n    buffer_.resize(width_ * height_, 0);\n    real_fb_ = false;\n    LOGI("FrameBuffer", "Using offscreen buffer: %dx%d", width_, height_);\n    return true;', init_hook)

# Modify close to free SPI
close_hook = """
    if (spi_ctx_) {
#ifdef __linux__
        if (spi_ctx_->spi_fd >= 0) ::close(spi_ctx_->spi_fd);
        gpio_close_fb(&spi_ctx_->cs);
        gpio_close_fb(&spi_ctx_->dc);
        gpio_close_fb(&spi_ctx_->rst);
        gpio_close_fb(&spi_ctx_->bl);
#endif
        delete spi_ctx_;
        spi_ctx_ = nullptr;
    }
    buffer_.clear();"""
code = code.replace('    buffer_.clear();', close_hook)

# Modify flush to call flushSpiLcd
flush_hook = """void FrameBuffer::flush() {
    if (real_fb_) {
#ifdef __linux__
        // fbdev is memory-mapped, already written directly
#endif
    } else if (spi_ctx_) {
        flushSpiLcd();
    }
}"""
code = re.sub(r'void FrameBuffer::flush\(\) \{.*?\n\}', flush_hook, code, flags=re.DOTALL)

with open(r'C:\Users\Aze\Desktop\edgefocus(1)\开发板部分代码\focus_app\ui\FrameBuffer.cpp', 'w', encoding='utf-8') as f:
    f.write(code)

print("Patching FrameBuffer.cpp complete.")

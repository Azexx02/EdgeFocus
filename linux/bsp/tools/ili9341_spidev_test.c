#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <linux/spi/spidev.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

struct gpio_line {
    int chip_fd;
    int line_fd;
    const char *chip;
    unsigned int offset;
    const char *name;
};

static void msleep(unsigned int ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) < 0 && errno == EINTR) {
    }
}

static int gpio_open_output(struct gpio_line *line, int initial)
{
    struct gpiohandle_request req;

    memset(&req, 0, sizeof(req));
    line->chip_fd = open(line->chip, O_RDONLY | O_CLOEXEC);
    if (line->chip_fd < 0) {
        perror(line->chip);
        return -1;
    }

    req.lineoffsets[0] = line->offset;
    req.flags = GPIOHANDLE_REQUEST_OUTPUT;
    req.lines = 1;
    req.default_values[0] = initial ? 1 : 0;
    snprintf(req.consumer_label, sizeof(req.consumer_label), "lcd-test-%s", line->name);

    if (ioctl(line->chip_fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        fprintf(stderr, "request GPIO %s offset %u failed: %s\n", line->chip, line->offset, strerror(errno));
        close(line->chip_fd);
        line->chip_fd = -1;
        return -1;
    }

    line->line_fd = req.fd;
    return 0;
}

static int gpio_set(struct gpio_line *line, int value)
{
    struct gpiohandle_data data;

    memset(&data, 0, sizeof(data));
    data.values[0] = value ? 1 : 0;
    if (ioctl(line->line_fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data) < 0) {
        fprintf(stderr, "set GPIO %s failed: %s\n", line->name, strerror(errno));
        return -1;
    }
    return 0;
}

static void gpio_close(struct gpio_line *line)
{
    if (line->line_fd >= 0) {
        close(line->line_fd);
    }
    if (line->chip_fd >= 0) {
        close(line->chip_fd);
    }
}

static void bb_delay(void)
{
    for (volatile int i = 0; i < 120; i++) {
    }
}

static int bb_write_byte(struct gpio_line *sck, struct gpio_line *mosi, uint8_t value)
{
    for (int bit = 7; bit >= 0; bit--) {
        if (gpio_set(mosi, (value >> bit) & 1) < 0) return -1;
        bb_delay();
        if (gpio_set(sck, 1) < 0) return -1;
        bb_delay();
        if (gpio_set(sck, 0) < 0) return -1;
        bb_delay();
    }
    return 0;
}

static int bb_cmd(struct gpio_line *cs, struct gpio_line *dc, struct gpio_line *sck, struct gpio_line *mosi, uint8_t cmd)
{
    printf("bb cmd 0x%02x\n", cmd);
    fflush(stdout);
    if (gpio_set(dc, 0) < 0) return -1;
    if (gpio_set(cs, 0) < 0) return -1;
    if (bb_write_byte(sck, mosi, cmd) < 0) return -1;
    if (gpio_set(cs, 1) < 0) return -1;
    return 0;
}

static int bb_data(struct gpio_line *cs, struct gpio_line *dc, struct gpio_line *sck, struct gpio_line *mosi,
                   const uint8_t *data, size_t len)
{
    if (gpio_set(dc, 1) < 0) return -1;
    if (gpio_set(cs, 0) < 0) return -1;
    for (size_t i = 0; i < len; i++) {
        if (bb_write_byte(sck, mosi, data[i]) < 0) return -1;
    }
    if (gpio_set(cs, 1) < 0) return -1;
    return 0;
}

static int bb_cmd1(struct gpio_line *cs, struct gpio_line *dc, struct gpio_line *sck, struct gpio_line *mosi,
                   uint8_t cmd, uint8_t arg)
{
    if (bb_cmd(cs, dc, sck, mosi, cmd) < 0) return -1;
    return bb_data(cs, dc, sck, mosi, &arg, 1);
}

static int bb_cmdn(struct gpio_line *cs, struct gpio_line *dc, struct gpio_line *sck, struct gpio_line *mosi,
                   uint8_t cmd, const uint8_t *args, size_t len)
{
    if (bb_cmd(cs, dc, sck, mosi, cmd) < 0) return -1;
    return bb_data(cs, dc, sck, mosi, args, len);
}

static int bb_init_st7789(struct gpio_line *cs, struct gpio_line *dc, struct gpio_line *sck, struct gpio_line *mosi)
{
    if (bb_cmd(cs, dc, sck, mosi, 0x11) < 0) return -1;
    msleep(120);
    if (bb_cmd1(cs, dc, sck, mosi, 0x36, 0x00) < 0) return -1;
    if (bb_cmd1(cs, dc, sck, mosi, 0x3a, 0x55) < 0) return -1;
    if (bb_cmd(cs, dc, sck, mosi, 0x21) < 0) return -1;
    if (bb_cmd(cs, dc, sck, mosi, 0x13) < 0) return -1;
    if (bb_cmd(cs, dc, sck, mosi, 0x29) < 0) return -1;
    msleep(20);
    return 0;
}

static int bb_init_ili9341_full(struct gpio_line *cs, struct gpio_line *dc, struct gpio_line *sck,
                                struct gpio_line *mosi, uint8_t madctl)
{
    if (bb_cmd(cs, dc, sck, mosi, 0x01) < 0) return -1;
    msleep(120);

    { uint8_t d[] = { 0x00, 0xc1, 0x30 }; if (bb_cmdn(cs, dc, sck, mosi, 0xcf, d, sizeof(d)) < 0) return -1; }
    { uint8_t d[] = { 0x64, 0x03, 0x12, 0x81 }; if (bb_cmdn(cs, dc, sck, mosi, 0xed, d, sizeof(d)) < 0) return -1; }
    { uint8_t d[] = { 0x85, 0x00, 0x78 }; if (bb_cmdn(cs, dc, sck, mosi, 0xe8, d, sizeof(d)) < 0) return -1; }
    { uint8_t d[] = { 0x39, 0x2c, 0x00, 0x34, 0x02 }; if (bb_cmdn(cs, dc, sck, mosi, 0xcb, d, sizeof(d)) < 0) return -1; }
    if (bb_cmd1(cs, dc, sck, mosi, 0xf7, 0x20) < 0) return -1;
    { uint8_t d[] = { 0x00, 0x00 }; if (bb_cmdn(cs, dc, sck, mosi, 0xea, d, sizeof(d)) < 0) return -1; }

    if (bb_cmd1(cs, dc, sck, mosi, 0xc0, 0x23) < 0) return -1;
    if (bb_cmd1(cs, dc, sck, mosi, 0xc1, 0x10) < 0) return -1;
    { uint8_t d[] = { 0x3e, 0x28 }; if (bb_cmdn(cs, dc, sck, mosi, 0xc5, d, sizeof(d)) < 0) return -1; }
    if (bb_cmd1(cs, dc, sck, mosi, 0xc7, 0x86) < 0) return -1;
    if (bb_cmd1(cs, dc, sck, mosi, 0x3a, 0x55) < 0) return -1;
    if (bb_cmd1(cs, dc, sck, mosi, 0x36, madctl) < 0) return -1;
    { uint8_t d[] = { 0x00, 0x18 }; if (bb_cmdn(cs, dc, sck, mosi, 0xb1, d, sizeof(d)) < 0) return -1; }
    { uint8_t d[] = { 0x08, 0x82, 0x27 }; if (bb_cmdn(cs, dc, sck, mosi, 0xb6, d, sizeof(d)) < 0) return -1; }
    if (bb_cmd1(cs, dc, sck, mosi, 0xf2, 0x00) < 0) return -1;
    if (bb_cmd1(cs, dc, sck, mosi, 0x26, 0x01) < 0) return -1;
    { uint8_t d[] = { 0x0f, 0x31, 0x2b, 0x0c, 0x0e, 0x08, 0x4e, 0xf1, 0x37, 0x07, 0x10, 0x03, 0x0e, 0x09, 0x00 }; if (bb_cmdn(cs, dc, sck, mosi, 0xe0, d, sizeof(d)) < 0) return -1; }
    { uint8_t d[] = { 0x00, 0x0e, 0x14, 0x03, 0x11, 0x07, 0x31, 0xc1, 0x48, 0x08, 0x0f, 0x0c, 0x31, 0x36, 0x0f }; if (bb_cmdn(cs, dc, sck, mosi, 0xe1, d, sizeof(d)) < 0) return -1; }

    if (bb_cmd(cs, dc, sck, mosi, 0x11) < 0) return -1;
    msleep(120);
    if (bb_cmd(cs, dc, sck, mosi, 0x29) < 0) return -1;
    msleep(20);
    return 0;
}

static int bb_set_window(struct gpio_line *cs, struct gpio_line *dc, struct gpio_line *sck, struct gpio_line *mosi,
                         uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t buf[4];

    buf[0] = x0 >> 8; buf[1] = x0 & 0xff; buf[2] = x1 >> 8; buf[3] = x1 & 0xff;
    if (bb_cmd(cs, dc, sck, mosi, 0x2a) < 0 || bb_data(cs, dc, sck, mosi, buf, sizeof(buf)) < 0) return -1;
    buf[0] = y0 >> 8; buf[1] = y0 & 0xff; buf[2] = y1 >> 8; buf[3] = y1 & 0xff;
    if (bb_cmd(cs, dc, sck, mosi, 0x2b) < 0 || bb_data(cs, dc, sck, mosi, buf, sizeof(buf)) < 0) return -1;
    return bb_cmd(cs, dc, sck, mosi, 0x2c);
}

static int bb_fill(struct gpio_line *cs, struct gpio_line *dc, struct gpio_line *sck, struct gpio_line *mosi, uint16_t color)
{
    uint8_t px[2] = { color >> 8, color & 0xff };

    if (bb_set_window(cs, dc, sck, mosi, 0, 0, 239, 319) < 0) return -1;
    if (gpio_set(dc, 1) < 0) return -1;
    if (gpio_set(cs, 0) < 0) return -1;
    for (int i = 0; i < 240 * 320; i++) {
        if (bb_write_byte(sck, mosi, px[0]) < 0 || bb_write_byte(sck, mosi, px[1]) < 0) return -1;
    }
    if (gpio_set(cs, 1) < 0) return -1;
    return 0;
}

static int run_bitbang(const char *stage)
{
    int rc = 1;
    struct gpio_line cs = { -1, -1, "/dev/gpiochip1", 0, "PB0-cs" };
    struct gpio_line reset = { -1, -1, "/dev/gpiochip1", 1, "PB1-reset" };
    struct gpio_line power = { -1, -1, "/dev/gpiochip1", 4, "PB4-power" };
    struct gpio_line dc = { -1, -1, "/dev/gpiochip5", 10, "PF10-dc" };
    struct gpio_line backlight = { -1, -1, "/dev/gpiochip3", 8, "PD8-backlight" };
    struct gpio_line mosi = { -1, -1, "/dev/gpiochip9", 0, "PZ0-mosi" };
    struct gpio_line sck = { -1, -1, "/dev/gpiochip9", 2, "PZ2-sck" };

    printf("bitbang stage=%s\n", stage);
    fflush(stdout);
    if (gpio_open_output(&cs, 1) < 0) goto out;
    printf("bb requested PB0 cs\n"); fflush(stdout);
    if (gpio_open_output(&reset, 1) < 0) goto out;
    printf("bb requested PB1 reset\n"); fflush(stdout);
    if (gpio_open_output(&power, 1) < 0) goto out;
    printf("bb requested PB4 power\n"); fflush(stdout);
    if (gpio_open_output(&dc, 1) < 0) goto out;
    printf("bb requested PF10 dc\n"); fflush(stdout);
    if (gpio_open_output(&backlight, 1) < 0) goto out;
    printf("bb requested PD8 backlight\n"); fflush(stdout);
    if (gpio_open_output(&mosi, 0) < 0) goto out;
    printf("bb requested PZ0 mosi\n"); fflush(stdout);
    if (gpio_open_output(&sck, 0) < 0) goto out;
    printf("bb requested PZ2 sck\n"); fflush(stdout);

    gpio_set(&power, 1);
    gpio_set(&backlight, 1);
    gpio_set(&cs, 1);
    gpio_set(&sck, 0);
    gpio_set(&reset, 1);
    msleep(20);
    gpio_set(&reset, 0);
    msleep(80);
    gpio_set(&reset, 1);
    msleep(120);
    printf("bb reset done\n");
    fflush(stdout);
    if (strcmp(stage, "bb-reset") == 0) {
        rc = 0;
        goto out;
    }

    if (strcmp(stage, "bb-st7789") == 0 || strcmp(stage, "bb-st7789-fill") == 0) {
        if (bb_init_st7789(&cs, &dc, &sck, &mosi) < 0) goto out;
    } else if (strcmp(stage, "bb-full") == 0 || strcmp(stage, "bb-full-fill") == 0) {
        if (bb_init_ili9341_full(&cs, &dc, &sck, &mosi, 0x48) < 0) goto out;
    } else {
        if (bb_cmd(&cs, &dc, &sck, &mosi, 0x01) < 0) goto out;
        msleep(120);
        if (bb_cmd1(&cs, &dc, &sck, &mosi, 0x3a, 0x55) < 0) goto out;
        if (bb_cmd1(&cs, &dc, &sck, &mosi, 0x36, 0x48) < 0) goto out;
        if (bb_cmd(&cs, &dc, &sck, &mosi, 0x11) < 0) goto out;
        msleep(120);
        if (bb_cmd(&cs, &dc, &sck, &mosi, 0x29) < 0) goto out;
        msleep(20);
    }
    printf("bb init done\n");
    fflush(stdout);
    if (strcmp(stage, "bb-init") == 0 || strcmp(stage, "bb-full") == 0 || strcmp(stage, "bb-st7789") == 0) {
        rc = 0;
        goto out;
    }

    printf("bb fill red\n");
    fflush(stdout);
    if (bb_fill(&cs, &dc, &sck, &mosi, 0xf800) < 0) goto out;
    msleep(1000);
    printf("bb fill green\n");
    fflush(stdout);
    if (bb_fill(&cs, &dc, &sck, &mosi, 0x07e0) < 0) goto out;
    msleep(1000);
    printf("bb fill blue\n");
    fflush(stdout);
    if (bb_fill(&cs, &dc, &sck, &mosi, 0x001f) < 0) goto out;
    rc = 0;

out:
    gpio_close(&sck);
    gpio_close(&mosi);
    gpio_close(&backlight);
    gpio_close(&dc);
    gpio_close(&power);
    gpio_close(&reset);
    gpio_close(&cs);
    return rc;
}

static int spi_write_all(int spi_fd, const uint8_t *buf, size_t len, uint32_t speed)
{
    uint8_t rx[4096];

    while (len > 0) {
        size_t chunk = len > 4096 ? 4096 : len;
        struct spi_ioc_transfer tr;

        memset(&tr, 0, sizeof(tr));
        tr.tx_buf = (unsigned long)buf;
        tr.rx_buf = (unsigned long)rx;
        tr.len = (uint32_t)chunk;
        tr.speed_hz = speed;
        tr.bits_per_word = 8;

        if (ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
            perror("SPI_IOC_MESSAGE");
            return -1;
        }
        buf += chunk;
        len -= chunk;
    }
    return 0;
}

static int lcd_cmd(int spi_fd, struct gpio_line *dc, uint8_t cmd, uint32_t speed)
{
    printf("send cmd 0x%02x\n", cmd);
    fflush(stdout);
    if (gpio_set(dc, 0) < 0) {
        return -1;
    }
    if (spi_write_all(spi_fd, &cmd, 1, speed) < 0) {
        return -1;
    }
    printf("done cmd 0x%02x\n", cmd);
    fflush(stdout);
    return 0;
}

static int lcd_data(int spi_fd, struct gpio_line *dc, const uint8_t *data, size_t len, uint32_t speed)
{
    printf("send data len=%zu\n", len);
    fflush(stdout);
    if (gpio_set(dc, 1) < 0) {
        return -1;
    }
    if (spi_write_all(spi_fd, data, len, speed) < 0) {
        return -1;
    }
    printf("done data len=%zu\n", len);
    fflush(stdout);
    return 0;
}

static int lcd_cmd1(int spi_fd, struct gpio_line *dc, uint8_t cmd, uint8_t arg, uint32_t speed)
{
    if (lcd_cmd(spi_fd, dc, cmd, speed) < 0) {
        return -1;
    }
    return lcd_data(spi_fd, dc, &arg, 1, speed);
}

static int lcd_set_window(int spi_fd, struct gpio_line *dc, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint32_t speed)
{
    uint8_t buf[4];

    buf[0] = x0 >> 8;
    buf[1] = x0 & 0xff;
    buf[2] = x1 >> 8;
    buf[3] = x1 & 0xff;
    if (lcd_cmd(spi_fd, dc, 0x2a, speed) < 0 || lcd_data(spi_fd, dc, buf, sizeof(buf), speed) < 0) {
        return -1;
    }

    buf[0] = y0 >> 8;
    buf[1] = y0 & 0xff;
    buf[2] = y1 >> 8;
    buf[3] = y1 & 0xff;
    if (lcd_cmd(spi_fd, dc, 0x2b, speed) < 0 || lcd_data(spi_fd, dc, buf, sizeof(buf), speed) < 0) {
        return -1;
    }

    return lcd_cmd(spi_fd, dc, 0x2c, speed);
}

static int lcd_fill(int spi_fd, struct gpio_line *dc, uint16_t color, uint32_t speed)
{
    uint8_t line[240 * 2];
    int y;

    for (size_t i = 0; i < sizeof(line); i += 2) {
        line[i] = color >> 8;
        line[i + 1] = color & 0xff;
    }

    if (lcd_set_window(spi_fd, dc, 0, 0, 239, 319, speed) < 0) {
        return -1;
    }
    if (gpio_set(dc, 1) < 0) {
        return -1;
    }
    for (y = 0; y < 320; y++) {
        if (spi_write_all(spi_fd, line, sizeof(line), speed) < 0) {
            return -1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *spi_path = argc > 1 ? argv[1] : "/dev/spidev0.0";
    uint32_t speed = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 0) : 4000000;
    const char *stage = argc > 3 ? argv[3] : "fill";
    uint8_t mode = 0;
    uint8_t bits = 8;
    int spi_fd = -1;
    int rc = 1;
    struct gpio_line power = { -1, -1, "/dev/gpiochip1", 4, "PB4-power" };
    struct gpio_line reset = { -1, -1, "/dev/gpiochip1", 1, "PB1-reset" };
    struct gpio_line dc = { -1, -1, "/dev/gpiochip5", 10, "PF10-dc" };
    struct gpio_line backlight = { -1, -1, "/dev/gpiochip3", 8, "PD8-backlight" };
    const uint16_t colors[] = { 0xf800, 0x07e0, 0x001f, 0xffff, 0x0000 };
    const char *names[] = { "red", "green", "blue", "white", "black" };

    if (strncmp(stage, "bb", 2) == 0) {
        return run_bitbang(stage);
    }

    printf("SPI LCD test: spi=%s speed=%u stage=%s\n", spi_path, speed, stage);
    fflush(stdout);
    if (gpio_open_output(&power, 1) < 0 ||
        gpio_open_output(&reset, 1) < 0 ||
        gpio_open_output(&dc, 1) < 0 ||
        gpio_open_output(&backlight, 1) < 0) {
        goto out;
    }
    printf("GPIO requested: PB4 power=1, PB1 reset=1, PF10 dc=1, PD8 backlight=1\n");
    fflush(stdout);
    if (strcmp(stage, "gpio") == 0) {
        rc = 0;
        goto out;
    }

    spi_fd = open(spi_path, O_RDWR | O_CLOEXEC);
    if (spi_fd < 0) {
        perror(spi_path);
        goto out;
    }
    printf("SPI opened\n");
    fflush(stdout);
    if (ioctl(spi_fd, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        perror("configure spidev");
        goto out;
    }
    printf("SPI configured\n");
    fflush(stdout);
    if (strcmp(stage, "open") == 0) {
        rc = 0;
        goto out;
    }

    gpio_set(&power, 1);
    gpio_set(&backlight, 1);
    gpio_set(&reset, 1);
    msleep(20);
    gpio_set(&reset, 0);
    msleep(80);
    gpio_set(&reset, 1);
    msleep(120);
    printf("LCD reset sequence done\n");
    fflush(stdout);
    if (strcmp(stage, "reset") == 0) {
        rc = 0;
        goto out;
    }
    if (strcmp(stage, "only3a") == 0) {
        if (lcd_cmd(spi_fd, &dc, 0x3a, speed) < 0) goto out;
        rc = 0;
        goto out;
    }
    if (strcmp(stage, "twonop") == 0) {
        if (lcd_cmd(spi_fd, &dc, 0x00, speed) < 0) goto out;
        if (lcd_cmd(spi_fd, &dc, 0x00, speed) < 0) goto out;
        rc = 0;
        goto out;
    }
    if (strncmp(stage, "byte", 4) == 0) {
        unsigned long value = strtoul(stage + 4, NULL, 16);
        if (lcd_cmd(spi_fd, &dc, (uint8_t)value, speed) < 0) goto out;
        rc = 0;
        goto out;
    }

    if (lcd_cmd(spi_fd, &dc, 0x01, speed) < 0) goto out;
    printf("cmd 0x01 software reset sent\n");
    fflush(stdout);
    msleep(120);
    if (lcd_cmd1(spi_fd, &dc, 0x3a, 0x55, speed) < 0) goto out;
    printf("cmd 0x3a pixel format sent\n");
    fflush(stdout);
    if (lcd_cmd1(spi_fd, &dc, 0x36, 0x48, speed) < 0) goto out;
    printf("cmd 0x36 madctl sent\n");
    fflush(stdout);
    if (lcd_cmd(spi_fd, &dc, 0x11, speed) < 0) goto out;
    printf("cmd 0x11 sleep out sent\n");
    fflush(stdout);
    msleep(120);
    if (lcd_cmd(spi_fd, &dc, 0x29, speed) < 0) goto out;
    printf("cmd 0x29 display on sent\n");
    fflush(stdout);
    msleep(20);
    if (strcmp(stage, "init") == 0) {
        rc = 0;
        goto out;
    }

    for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); i++) {
        printf("fill %s\n", names[i]);
        fflush(stdout);
        if (lcd_fill(spi_fd, &dc, colors[i], speed) < 0) {
            goto out;
        }
        msleep(900);
    }

    printf("done; leaving backlight on\n");
    rc = 0;

out:
    if (spi_fd >= 0) {
        close(spi_fd);
    }
    gpio_close(&backlight);
    gpio_close(&dc);
    gpio_close(&reset);
    gpio_close(&power);
    return rc;
}

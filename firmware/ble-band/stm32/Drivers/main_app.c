#include "main_app.h"

#include "battery.h"
#include "ble_band.h"
#include "ble_protocol.h"
#include "button.h"
#include "main.h"
#include "motor.h"
#include "power.h"
#include "ssd1306.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define BAND_DEVICE_NAME "EdgefocusBand"
#define BLE_POLL_MS 100U
#define CHARGE_CHECK_MS 500U
#define BATTERY_UPDATE_MS 30000U
#define CHARGE_SLEEP_LOOP_MS 50U
#define BUTTON_DEBOUNCE_MS 30U
#define BUTTON_LONG_PRESS_MS 2000U
#define EXT_POWER_DEBOUNCE_MS 1000U
#define AUTO_CHARGE_SHUTDOWN_ENABLED 0U

extern UART_HandleTypeDef huart1;
extern I2C_HandleTypeDef hi2c1;
extern ADC_HandleTypeDef hadc1;

static ble_band_t band;
static uint32_t countdown_seconds;
static uint32_t countdown_last_tick;
static uint8_t countdown_active;
static uint8_t charge_sleeping;
static uint32_t charge_last_check_tick;
static uint32_t battery_last_update_tick;
static uint16_t battery_cached_mv;
static uint8_t battery_cached_percent;
static uint8_t battery_cache_valid;
static uint8_t setup_failed;
static uint8_t setup_anim_frame;
static uint8_t display_enabled = 1U;
static uint8_t button_last_raw;
static uint8_t button_stable;
static uint32_t button_last_change_tick;
static uint8_t button_long_handled;
static uint32_t button_press_start_tick;
static uint8_t ext_power_raw;
static uint8_t ext_power_stable;
static uint32_t ext_power_last_change_tick;

static void show4(const char *a, const char *b, const char *c, const char *d);
static void show_main_page(void);
static void show_countdown_page(void);

static void init_ext_power_state(void)
{
    ext_power_raw = power_ext_present();
    ext_power_stable = ext_power_raw;
    ext_power_last_change_tick = HAL_GetTick();
}

static uint8_t update_ext_power_state(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t raw = power_ext_present();

    if (raw != ext_power_raw) {
        ext_power_raw = raw;
        ext_power_last_change_tick = now;
    }

    if (ext_power_stable != ext_power_raw &&
        (uint32_t)(now - ext_power_last_change_tick) >= EXT_POWER_DEBOUNCE_MS) {
        ext_power_stable = ext_power_raw;
    }

    return ext_power_stable;
}

static uint8_t refresh_battery_cache(uint8_t force)
{
    uint32_t now = HAL_GetTick();

    if (!force && battery_cache_valid &&
        (uint32_t)(now - battery_last_update_tick) < BATTERY_UPDATE_MS) {
        return 0U;
    }

    battery_cached_mv = battery_read_mv();
    battery_cached_percent = battery_percent_from_mv(battery_cached_mv);
    battery_last_update_tick = now;
    battery_cache_valid = 1U;
    return 1U;
}

static void make_battery_line(char *out, size_t out_size)
{
    if (out == NULL || out_size < 16U) {
        return;
    }
    (void)refresh_battery_cache(0U);

    out[0] = 'B';
    out[1] = 'A';
    out[2] = 'T';
    out[3] = ' ';
    out[4] = (char)('0' + (battery_cached_mv / 1000U) % 10U);
    out[5] = '.';
    out[6] = (char)('0' + (battery_cached_mv / 100U) % 10U);
    out[7] = (char)('0' + (battery_cached_mv / 10U) % 10U);
    out[8] = 'V';
    out[9] = ' ';
    out[10] = (battery_cached_percent >= 100U) ? '1' :
              (battery_cached_percent >= 10U ? (char)('0' + battery_cached_percent / 10U) : ' ');
    out[11] = (battery_cached_percent >= 100U) ? '0' : (char)('0' + battery_cached_percent % 10U);
    out[12] = (battery_cached_percent >= 100U) ? '0' : '%';
    out[13] = (battery_cached_percent >= 100U) ? '%' : '\0';
    out[14] = '\0';
}

static const char *ble_status_text(void)
{
    switch (ble_band_state(&band)) {
        case BLE_BAND_STATE_CONNECTED:
            return "BLE Connected";
        case BLE_BAND_STATE_ADVERTISING:
            return "BLE Advertising";
        default:
            return "BLE Offline";
    }
}

static void show_main_page(void)
{
    char battery_line[16] = {0};
    make_battery_line(battery_line, sizeof(battery_line));

    switch (ble_band_state(&band)) {
        case BLE_BAND_STATE_CONNECTED:
            show4(BAND_DEVICE_NAME, "BLE Connected", battery_line, "Waiting signal");
            break;
        case BLE_BAND_STATE_ADVERTISING:
            show4(BAND_DEVICE_NAME, "BLE Advertising", battery_line, "Waiting link");
            break;
        default:
            show4(BAND_DEVICE_NAME, "BLE Offline", battery_line, "E104 not ready");
            break;
    }
}

static void show_countdown_page(void)
{
    char battery_line[16] = {0};
    make_battery_line(battery_line, sizeof(battery_line));
    ssd1306_show_countdown_status(countdown_seconds, ble_status_text(), battery_line);
}

static void show_line(uint8_t row, const char *text)
{
    char buf[22];
    size_t n = strlen(text);

    if (n > 21U) {
        n = 21U;
    }
    memcpy(buf, text, n);
    buf[n] = '\0';
    ssd1306_draw_string(row, 0, buf);
}

static void show4(const char *a, const char *b, const char *c, const char *d)
{
    if (!display_enabled) {
        return;
    }

    ssd1306_clear();
    show_line(0, a);
    show_line(1, b);
    show_line(2, c);
    show_line(3, d);
    ssd1306_update();
}

static void set_display_enabled(uint8_t enabled)
{
    if (enabled) {
        display_enabled = 1U;
        ssd1306_display_on();
        if (countdown_active) {
            show_countdown_page();
        } else {
            show_main_page();
        }
    } else {
        display_enabled = 0U;
        ssd1306_display_off();
    }
}

static void bytes_to_ascii(const uint8_t *data, size_t len, char *out, size_t out_size)
{
    size_t n;

    if (out == NULL || out_size == 0U) {
        return;
    }

    n = len;
    if (n > out_size - 1U) {
        n = out_size - 1U;
    }

    for (size_t i = 0U; i < n; i++) {
        uint8_t b = data[i];
        out[i] = (b >= 0x20U && b <= 0x7EU) ? (char)b : '.';
    }
    out[n] = '\0';
}

static void show_setup_progress(void)
{
    static const char *const dots[] = {
        ".",
        "..",
        "...",
        "....",
    };

    show4(BAND_DEVICE_NAME, "Linking host", dots[setup_anim_frame], "Please wait");
    setup_anim_frame = (uint8_t)((setup_anim_frame + 1U) % 4U);
}

static void on_config_step(const char *cmd,
                           ble_band_result_t result,
                           const uint8_t *response,
                           size_t response_len,
                           void *user)
{
    char resp[22];
    const char *status;
    (void)user;

    if (result == BLE_BAND_OK) {
        show_setup_progress();
        return;
    }

    setup_failed = 1U;
    bytes_to_ascii(response, response_len, resp, sizeof(resp));
    if (result == BLE_BAND_E104_ERROR) {
        status = "uart/timeout";
    } else {
        status = "bad reply";
    }
    show4("BLE setup failed", cmd, status, resp);
}

static void on_state_changed(ble_band_state_t state, void *user)
{
    (void)state;
    (void)user;

    if (!countdown_active && !charge_sleeping) {
        show_main_page();
    }
}

static void buzz_alert(uint8_t pattern, uint8_t level)
{
    uint32_t pulse_ms = (level >= 3U) ? 500U : 300U;

    motor_init();

    switch (pattern) {
        case BLE_PROTOCOL_ALERT_NONE:
            break;
        case BLE_PROTOCOL_ALERT_LEGACY:
            motor_vibrate(500U);
            break;
        case BLE_PROTOCOL_ALERT_LONG:
            motor_vibrate((level >= 3U) ? 1000U : 800U);
            break;
        case BLE_PROTOCOL_ALERT_FAST:
            for (uint8_t i = 0U; i < 3U; i++) {
                motor_vibrate(pulse_ms);
                HAL_Delay(100U);
            }
            break;
        case BLE_PROTOCOL_ALERT_SLOW:
        default:
            for (uint8_t i = 0U; i < 3U; i++) {
                motor_vibrate(pulse_ms);
                HAL_Delay(500U);
            }
            break;
    }
}

static void on_alert(uint8_t pattern, uint8_t level, void *user)
{
    char line[8];
    const char *message;
    uint8_t was_counting = countdown_active;
    (void)user;

    if (pattern == BLE_PROTOCOL_ALERT_LEGACY) {
        buzz_alert(pattern, level);
        return;
    }

    line[0] = 'P';
    line[1] = '=';
    line[2] = (char)('0' + pattern);
    line[3] = ' ';
    line[4] = 'L';
    line[5] = '=';
    line[6] = (char)('0' + level);
    line[7] = '\0';

    if (level >= 4U) {
        message = "Take a rest";
    } else if (level >= 3U) {
        message = "Focus warning";
    } else if (level >= 2U) {
        message = "Attention";
    } else {
        message = "Gentle remind";
    }

    show4("BLE Alert", line, message, "motor active");
    buzz_alert(pattern, level);

    if (was_counting) {
        show_countdown_page();
    } else {
        HAL_Delay(800U);
        show_main_page();
    }
}

static void on_time_sync(uint16_t minutes, void *user)
{
    (void)user;
    if (minutes == 0U) {
        countdown_seconds = 0U;
        countdown_active = 0U;
        show_main_page();
        return;
    }

    countdown_seconds = (uint32_t)minutes * 60U;
    countdown_last_tick = HAL_GetTick();
    countdown_active = 1U;
    show_countdown_page();
}

static void on_text(const uint8_t *data, size_t len, void *user)
{
    char line[22];
    size_t n = len;
    uint8_t was_counting = countdown_active;
    (void)user;

    if (n > 21U) {
        n = 21U;
    }
    memcpy(line, data, n);
    line[n] = '\0';

    show4("BLE Text", line, "", "");
    HAL_Delay(1500U);

    if (was_counting) {
        show_countdown_page();
    } else {
        show_main_page();
    }
}

static void on_raw_rx(const uint8_t *data, size_t len, void *user)
{
    (void)data;
    (void)len;
    (void)user;
}

static void on_error(ble_band_result_t error, void *user)
{
    (void)error;
    (void)user;
    if (setup_failed) {
        return;
    }
    show4("BLE Error", "setup failed", "check E104", "");
}

static void run_manual_ble_setup(void)
{
    if (charge_sleeping) {
        return;
    }

    if (!display_enabled) {
        display_enabled = 1U;
        ssd1306_display_on();
    }

    countdown_active = 0U;
    setup_failed = 0U;
    setup_anim_frame = 0U;
    show4("BLE manual setup", "Restarting", "E104 config", "");
    HAL_Delay(400U);
    show_setup_progress();

    if (ble_band_configure(&band) != BLE_BAND_OK) {
        return;
    }
    show_main_page();
}

static void handle_button_single_click(void)
{
    set_display_enabled(display_enabled ? 0U : 1U);
}

static void handle_button_long_press(void)
{
    run_manual_ble_setup();
}

static void update_button_actions(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t raw = button_read();

    if (raw != button_last_raw) {
        button_last_raw = raw;
        button_last_change_tick = now;
    }

    if ((uint32_t)(now - button_last_change_tick) >= BUTTON_DEBOUNCE_MS &&
        raw != button_stable) {
        uint8_t old = button_stable;
        button_stable = raw;
        if (!old && button_stable) {
            button_press_start_tick = now;
            button_long_handled = 0U;
        } else if (old && !button_stable) {
            if (!button_long_handled) {
                handle_button_single_click();
            }
        }
    }

    if (button_stable && !button_long_handled &&
        (uint32_t)(now - button_press_start_tick) >= BUTTON_LONG_PRESS_MS) {
        button_long_handled = 1U;
        handle_button_long_press();
    }
}

static void update_countdown(void)
{
    uint32_t now;
    uint32_t elapsed;

    if (!countdown_active) {
        return;
    }

    now = HAL_GetTick();
    elapsed = (now - countdown_last_tick) / 1000U;
    if (elapsed == 0U) {
        return;
    }

    countdown_last_tick += elapsed * 1000U;
    if (elapsed >= countdown_seconds) {
        countdown_seconds = 0U;
        countdown_active = 0U;
        show_main_page();
        return;
    }

    countdown_seconds -= elapsed;
    show_countdown_page();
}

static void enter_charge_shutdown(void)
{
    countdown_active = 0U;
    charge_sleeping = 1U;
    motor_off();
    show4(BAND_DEVICE_NAME, "Charging", "System sleeping", "");
    HAL_Delay(800U);
    ssd1306_display_off();
    e104_bt52_sleep(&band.e104);
}

static void leave_charge_shutdown(void)
{
    charge_sleeping = 0U;
    ssd1306_display_on();
    show4(BAND_DEVICE_NAME, "Waking", "BLE resume", "");
    e104_bt52_wake(&band.e104);
    if (!ble_band_is_configured(&band)) {
        setup_failed = 0U;
        setup_anim_frame = 0U;
        show_setup_progress();
        (void)ble_band_configure(&band);
    }
    (void)refresh_battery_cache(1U);
    show_main_page();
}

static void update_charge_state(void)
{
    uint32_t now = HAL_GetTick();

    if (!AUTO_CHARGE_SHUTDOWN_ENABLED) {
        (void)update_ext_power_state();
        return;
    }

    if ((uint32_t)(now - charge_last_check_tick) < CHARGE_CHECK_MS) {
        return;
    }
    charge_last_check_tick = now;

    if (update_ext_power_state()) {
        if (!charge_sleeping) {
            enter_charge_shutdown();
        }
        return;
    }

    if (charge_sleeping) {
        leave_charge_shutdown();
    }
}

static void update_battery_display(void)
{
    if (!refresh_battery_cache(0U)) {
        return;
    }

    if (!countdown_active && !charge_sleeping) {
        show_main_page();
    }
}

void main_app(void)
{
    ssd1306_config_t cfg = {
        .hi2c = &hi2c1,
        .i2c_addr = 0x3C,
    };
    ble_band_callbacks_t callbacks = {
        .state_changed = on_state_changed,
        .config_step = on_config_step,
        .alert = on_alert,
        .time_sync = on_time_sync,
        .text = on_text,
        .raw_rx = on_raw_rx,
        .error = on_error,
        .user = NULL,
    };

    motor_init();
    battery_init(&hadc1);
    power_init();
    button_init();
    (void)refresh_battery_cache(1U);

    if (ssd1306_init(&cfg) != HAL_OK) {
        while (1) {
            motor_vibrate(50U);
            HAL_Delay(200U);
        }
    }

    ble_band_init(&band,
                  &huart1,
                  Bluetooth_wake_up_GPIO_Port,
                  Bluetooth_wake_up_Pin,
                  Bluetooth_reset_GPIO_Port,
                  Bluetooth_reset_Pin,
                  Bluetooth_connection_status_GPIO_Port,
                  Bluetooth_connection_status_Pin,
                  &callbacks);
    button_last_raw = button_read();
    button_stable = button_last_raw;
    button_last_change_tick = HAL_GetTick();
    init_ext_power_state();

    if (AUTO_CHARGE_SHUTDOWN_ENABLED && ext_power_stable) {
        charge_last_check_tick = HAL_GetTick();
        enter_charge_shutdown();
    } else {
        setup_failed = 0U;
        setup_anim_frame = 0U;
        show_setup_progress();
        if (ble_band_configure(&band) != BLE_BAND_OK) {
            while (1) {
                motor_vibrate(100U);
                HAL_Delay(1500U);
            }
        }
        show_main_page();
    }

    while (1) {
        update_charge_state();
        if (AUTO_CHARGE_SHUTDOWN_ENABLED && charge_sleeping) {
            HAL_Delay(CHARGE_SLEEP_LOOP_MS);
            continue;
        }
        update_button_actions();
        ble_band_poll(&band, BLE_POLL_MS);
        update_countdown();
        update_battery_display();
    }
}

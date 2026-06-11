#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "ssd1306.h"

static const char *TAG = "main";

// Pin configuration
#define SDA_PIN     9
#define SCL_PIN     8
#define OLED_ADDR   0x3C
#define MOTOR_PIN   4

// BLE UUIDs (custom 16-bit)
// Service: 0x180C, Characteristic: 0x2A56
static const ble_uuid16_t motor_svc_uuid = BLE_UUID16_INIT(0x180C);
static const ble_uuid16_t motor_chr_uuid = BLE_UUID16_INIT(0x2A56);

static uint16_t motor_chr_handle;
static bool ble_connected = false;

// ─── Motor ────────────────────────────────────────────
static void motor_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << MOTOR_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(MOTOR_PIN, 0);
}

static void motor_vibrate(int duration_ms)
{
    gpio_set_level(MOTOR_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    gpio_set_level(MOTOR_PIN, 0);
}

// ─── BLE GATT callbacks ──────────────────────────────
static int motor_chr_write(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    ESP_LOGI(TAG, "BLE write received! Triggering motor...");

    // Update OLED
    ssd1306_clear();
    ssd1306_draw_string(0, 0, "BLE Signal!");
    ssd1306_draw_string(1, 0, "Motor ON...");
    ssd1306_draw_string(3, 0, "ESP32-Band");
    ssd1306_update();

    // Vibrate for 500ms
    motor_vibrate(500);

    // Restore display
    ssd1306_clear();
    ssd1306_draw_string(0, 0, "BLE Connected");
    ssd1306_draw_string(1, 0, "Waiting signal");
    ssd1306_draw_string(3, 0, "ESP32-Band");
    ssd1306_update();

    return 0;
}

// GATT service definition
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &motor_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &motor_chr_uuid.u,
                .access_cb = motor_chr_write,
                .flags = BLE_GATT_CHR_F_WRITE,
                .val_handle = &motor_chr_handle,
            },
            { 0 }, // End
        },
    },
    { 0 }, // End
};

// ─── BLE GAP event handler ───────────────────────────
static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "BLE %sconnected (handle=%d)",
                 event->connect.status == 0 ? "" : "fail to ",
                 event->connect.conn_handle);
        if (event->connect.status == 0) {
            ble_connected = true;
            ssd1306_clear();
            ssd1306_draw_string(0, 0, "BLE Connected");
            ssd1306_draw_string(1, 0, "Waiting signal");
            ssd1306_draw_string(3, 0, "ESP32-Band");
            ssd1306_update();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "BLE disconnected");
        ble_connected = false;
        ssd1306_clear();
        ssd1306_draw_string(0, 0, "BLE Advertising");
        ssd1306_draw_string(1, 0, "Connect me...");
        ssd1306_draw_string(3, 0, "ESP32-Band");
        ssd1306_update();
        // Restart advertising
        struct ble_gap_adv_params adv_params = {
            .conn_mode = BLE_GAP_CONN_MODE_UND,
            .disc_mode = BLE_GAP_DISC_MODE_GEN,
        };
        ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                          &adv_params, ble_gap_event, NULL);
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        // Restart advertising if not connected
        if (!ble_connected) {
            struct ble_gap_adv_params adv_params2 = {
                .conn_mode = BLE_GAP_CONN_MODE_UND,
                .disc_mode = BLE_GAP_DISC_MODE_GEN,
            };
            ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                              &adv_params2, ble_gap_event, NULL);
        }
        break;
    }
    return 0;
}

// ─── BLE advertising ─────────────────────────────────
static void ble_advertise(void)
{
    struct ble_gap_adv_params adv_params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
        .itvl_min = 0x20,  // 20ms
        .itvl_max = 0x40,  // 40ms
    };

    const char *name = "ESP32-Band";

    // Put name + flags directly in advertising data
    // bluetoothctl needs name in adv data to show it during scan
    struct ble_hs_adv_fields adv_fields = { 0 };
    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    adv_fields.name = (uint8_t *)name;
    adv_fields.name_len = strlen(name);
    adv_fields.name_is_complete = 1;
    ble_gap_adv_set_fields(&adv_fields);

    int rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                               &adv_params, ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "BLE advertise failed: %d", rc);
        return;
    }
    ESP_LOGI(TAG, "BLE advertising as '%s'", name);
}

// ─── BLE sync callback (called when host is ready) ───
static void ble_on_sync(void)
{
    ESP_LOGI(TAG, "BLE host synced");
    ble_advertise();
}

// ─── BLE host task ────────────────────────────────────
static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// ─── App main ─────────────────────────────────────────
void app_main(void)
{
    ESP_LOGI(TAG, "Starting ESP32-Band...");

    // Init motor
    motor_init();

    // Init NVS (required by BLE)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Init OLED
    vTaskDelay(pdMS_TO_TICKS(1000));
    ssd1306_config_t oled_config = {
        .sda_pin = SDA_PIN,
        .scl_pin = SCL_PIN,
        .i2c_addr = OLED_ADDR,
    };
    ret = ssd1306_init(&oled_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SSD1306 init failed: %s", esp_err_to_name(ret));
        return;
    }

    // Show startup
    ssd1306_clear();
    ssd1306_draw_string(0, 0, "ESP32-Band");
    ssd1306_draw_string(1, 0, "BLE starting...");
    ssd1306_draw_string(3, 0, "Please wait");
    ssd1306_update();

    // Startup vibration
    motor_vibrate(100);

    // Init NimBLE
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE init failed: %s", esp_err_to_name(ret));
        return;
    }

    // Configure BLE host
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.reset_cb = NULL;

    // Register GATT services
    ret = ble_gatts_count_cfg(gatt_svcs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATT count failed: %d", ret);
        return;
    }
    ret = ble_gatts_add_svcs(gatt_svcs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATT add svcs failed: %d", ret);
        return;
    }

    // Set device name
    ble_svc_gap_device_name_set("ESP32-Band");

    // Start BLE host task
    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "BLE initialized, waiting for connections...");

    // Update OLED
    ssd1306_clear();
    ssd1306_draw_string(0, 0, "ESP32-Band");
    ssd1306_draw_string(1, 0, "BLE Advertising");
    ssd1306_draw_string(3, 0, "Connect me!");
    ssd1306_update();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

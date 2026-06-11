# ESP32-Band BLE 接口文档

## 设备信息

| 项目 | 值 |
|------|-----|
| 设备名称 | `ESP32-Band` |
| 协议 | BLE (Bluetooth Low Energy) |
| 角色 | Peripheral (从设备/广播者) |
| 芯片 | ESP32-S3 |

## BLE 服务与特征值

### Service（服务）

| 项目 | 值 |
|------|-----|
| UUID | `0x180C` |
| 类型 | Primary Service |

### Characteristic（特征值）

| 项目 | 值 |
|------|-----|
| UUID | `0x2A56` |
| 权限 | Write（可写） |
| 说明 | 写入任意数据触发马达震动 500ms |

## 交互流程

```
发送端 (ESP32-A)                     接收端 (ESP32-Band)
     |                                      |
     |  1. BLE 扫描                         |
     |  --------------------------------->  |  广播名称: "ESP32-Band"
     |                                      |
     |  2. 建立连接                         |
     |  --------------------------------->  |  OLED 显示 "BLE Connected"
     |                                      |
     |  3. 写入特征值 (UUID: 0x2A56)        |
     |  --------------------------------->  |  马达震动 500ms
     |       写入任意 1 字节数据             |  OLED 显示 "BLE Signal!"
     |                                      |
     |  4. 断开连接                         |
     |  --------------------------------->  |  OLED 显示 "BLE Advertising"
     |                                      |  重新开始广播
```

## 发送端代码示例 (Arduino + BLE Client)

```cpp
#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLEUtils.h>
#include <BLEScan.h>

// 与 ESP32-Band 匹配的 UUID
#define SERVICE_UUID        "180C"
#define CHARACTERISTIC_UUID "2A56"

BLEClient* pClient = nullptr;
BLERemoteCharacteristic* pCharacteristic = nullptr;
bool connected = false;

// 扫描回调 - 发现 ESP32-Band 时自动连接
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        if (advertisedDevice.getName() == "ESP32-Band") {
            Serial.println("找到 ESP32-Band，正在连接...");
            BLEDevice::getScan()->stop();
            
            pClient = BLEDevice::createClient();
            pClient->connect(&advertisedDevice);
            
            BLERemoteService* pService = pClient->getService(SERVICE_UUID);
            if (pService) {
                pCharacteristic = pService->getCharacteristic(CHARACTERISTIC_UUID);
                if (pCharacteristic) {
                    connected = true;
                    Serial.println("已连接，可以发送信号");
                }
            }
        }
    }
};

void setup() {
    Serial.begin(115200);
    BLEDevice::init("ESP32-Sender");
    
    BLEScan* pScan = BLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pScan->setActiveScan(true);
    pScan->start(5);  // 扫描 5 秒
}

void loop() {
    if (connected && pCharacteristic) {
        // 发送信号：写入 1 字节数据（值任意）
        uint8_t cmd = 0x01;
        pCharacteristic->writeValue(cmd, 1);
        Serial.println("信号已发送！");

        delay(3000);  // 每 3 秒发一次
    }
}
```

## 发送端代码示例 (ESP-IDF + NimBLE Client)

```c
#include "nimble/nimble_port.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"

// 扫描到 ESP32-Band 后写入特征值
static void trigger_motor(uint16_t conn_handle) {
    // 查找服务
    uint16_t svc_start_handle = 0;
    ble_gattc_disc_svc_by_uuid(conn_handle,
        BLE_UUID16_DECLARE(0x180C), NULL, NULL);

    // 查找特征值
    uint16_t chr_val_handle = 0;
    ble_gattc_disc_chrs_by_uuid(conn_handle,
        svc_start_handle, 0xFFFF,
        BLE_UUID16_DECLARE(0x2A56), NULL, NULL);

    // 写入数据触发马达
    uint8_t cmd = 0x01;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(&cmd, 1);
    ble_gattc_write_flat(conn_handle, chr_val_handle, &cmd, 1, NULL, NULL);
}
```

## 手机测试 (nRF Connect App)

1. 手机安装 **nRF Connect**（iOS/Android）
2. 打开 app，扫描蓝牙设备
3. 找到 **ESP32-Band**，点击连接
4. 展开服务 `0x180C`
5. 找到特征值 `0x2A56`（带写入图标）
6. 点击写入按钮，选择 **Byte Array**，输入任意值（如 `01`），发送
7. ESP32-Band 马达震动 500ms，OLED 显示 "BLE Signal!"

## 硬件接线 (ESP32-Band 接收端)

| 功能 | ESP32-S3 引脚 | 外设 |
|------|--------------|------|
| OLED SDA | GPIO9 | SSD1306 SDA |
| OLED SCL | GPIO8 | SSD1306 SCL |
| 马达 | GPIO4 | 马达驱动信号 |
| 马达 | 3.3V / GND | 马达电源 |

## 注意事项

- BLE 连接范围约 10-30 米（视环境而定）
- 写入的字节数据内容不影响触发，任何值都会触发马达
- 马达固定震动 500ms，如需修改时长请改 `main.c` 中 `motor_vibrate(500)` 的参数
- OLED 会实时显示连接状态和信号接收状态

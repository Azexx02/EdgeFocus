# ESP32-S3 G431 纯串口测试主机

## 接线

| ESP32-S3 示例脚 | G431 | 说明 |
|---|---|---|
| GPIO4 / TX | U2.2 / PC11 RX | 交叉连接 |
| GPIO5 / RX | U2.1 / PC10 TX | 交叉连接 |
| GND | U2.6、U2.7 或 U2.8 | 必须共地 |

GPIO4/5 只是示例，应避开实际开发板的 Flash、PSRAM、USB 和启动脚。不要连接 5 V/VBUS 到 G431 信号脚。

## Arduino 示例

电脑 USB 串口和 G431 串口均使用 115200。电脑输入的每一行会转发给 G431；G431 返回会显示在串口监视器。`@start` 等价于 `ARM`，`@stop` 等价于 `STOP`。

```cpp
#include <Arduino.h>

static constexpr int GIMBAL_TX = 4;
static constexpr int GIMBAL_RX = 5;
static constexpr uint32_t BAUD = 115200;
static constexpr size_t CAP = 192;

HardwareSerial Gimbal(1);
char pcLine[CAP];
size_t pcLength = 0;

static void sendCommand(const char *command)
{
  Gimbal.print(command);
  Gimbal.print("\r\n");
  Serial.print("[HOST->G431] ");
  Serial.println(command);
}

static void handleLine(char *line)
{
  if (!strcmp(line, "@start")) {
    sendCommand("ARM");
    Serial.println("[HOST] 等待 BOOT_ALIGN,OK 和 OK,ARM");
  } else if (!strcmp(line, "@stop")) {
    sendCommand("STOP");
  } else if (!strcmp(line, "@test")) {
    sendCommand("PING");
    sendCommand("VER");
    sendCommand("STAT");
    sendCommand("ANGLE");
  } else {
    sendCommand(line);
  }
}

void setup()
{
  Serial.begin(BAUD);
  Gimbal.begin(BAUD, SERIAL_8N1, GIMBAL_RX, GIMBAL_TX);
  delay(500);
  Serial.println("G431 纯串口主机：@test、@start、@stop");
}

void loop()
{
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r' || c == '\n') {
      if (pcLength) {
        pcLine[pcLength] = '\0';
        handleLine(pcLine);
        pcLength = 0;
      }
    } else if (pcLength < CAP - 1) {
      pcLine[pcLength++] = c;
    } else {
      pcLength = 0;
      Serial.println("[HOST] 输入过长，已丢弃");
    }
  }

  while (Gimbal.available()) {
    Serial.write(Gimbal.read());
  }
}
```

## 使用步骤

1. 断电接线并确认共地、TX/RX 交叉。
2. 打开 115200 串口监视器，输入 `@test`，确认返回 `OK,G431`。
3. 使用限流电源给云台供电，清空周围障碍。
4. 输入 `@start`，等待 `BOOT_ALIGN,OK` 和 `OK,ARM`。
5. 依次测试 `PITCH 10`、`YAW 10`、`ANGLE`。
6. 输入 `@stop`，再用 `STAT` 确认功率关闭。

本示例只覆盖当前推荐的纯串口模式。需要 Enable/Fault/BOOT0/NRST 时，应单独实现硬件联锁状态机，不要把升级控制混入正常运动流程。

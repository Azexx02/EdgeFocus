# G431 接线、板级信号与主控适配

## 1. 推荐方案

| 场景 | 推荐模式 | 必需连接 |
|---|---|---|
| 一体板、正常产品控制 | 纯串口模式 | TX、RX、GND |
| 需要独立硬件停机和故障线 | 硬件联锁模式 | UART + Enable + Fault + GND |
| 需要 ROM Bootloader 升级 | 完整控制模式 | UART + Enable + Fault + BOOT0 + NRST + GND |

当前固件默认 `GIMBAL_UART_ONLY_MODE=1`。不需要远程升级时，优先使用纯串口模式，减少接线和上电时序问题。

## 2. G431 云台板接口

| U2 | 信号 | G431 引脚 | 方向 | 电气说明 |
|---:|---|---|---|---|
| 1 | `G431_UART_TX` | PC10 | G431 → 主控 | 3.3 V UART |
| 2 | `G431_UART_RX` | PC11 | 主控 → G431 | 3.3 V UART |
| 3 | `G431_ENABLE_IN` | PD2 | 主控 → G431 | 高有效，R3 默认下拉 |
| 4 | `G431_FAULT_OUT` | PC12 | G431 → 主控 | 开漏、低有效，R4 上拉 |
| 5 | `BOOT0_CTRL` | PB8/BOOT0 | 主控 → G431 | 复位采样，R1 默认下拉 |
| 6–8 | GND | — | — | 信号参考地 |

禁止向信号脚输入 5 V。TX/RX 必须交叉连接，主控和云台必须共地。

## 3. 纯串口模式

接线：

```text
主控 TX  ──> U2.2 / G431 PC11 RX
主控 RX  <── U2.1 / G431 PC10 TX
主控 GND ─── U2.6/7/8 GND
```

`G431_ENABLE_IN`、`G431_FAULT_OUT`、`BOOT0_CTRL` 和 NRST 可不连接，但 G431 板上的 BOOT0 下拉必须保留。标准启动为：

```text
PING
ARM
等待 BOOT_ALIGN,OK 和 OK,ARM
发送运动命令
STOP
```

## 4. 可选硬件信号

### G431_ENABLE_IN

仅硬件联锁模式使用。低电平禁止功率；低到高边沿触发对齐。运动中拉低会关闭保持、PWM 和 Boost。重新拉高后必须重新等待对齐完成。

### G431_FAULT_OUT

开漏低有效。主控端只能配置为输入，不能推挽驱动。检测到低电平时应停止发送运动命令并撤销 Enable。纯串口模式不接该脚时，通过 `STAT` 和 `FAULT` 查询软件故障。

### BOOT0_CTRL 与 NRST

BOOT0 只在复位时采样：低电平从用户 Flash 启动，高电平进入系统 ROM Bootloader。正常控制不需要 NRST；只有远程切换 Bootloader 才需要 BOOT0 与 NRST 配合。NRST 应使用开漏方式只主动拉低，不要用推挽高电平与目标板复位网络对抗。

## 5. STM32MP257 正点原子核心板（已验证）

自制板连接：

| MP257 | 功能 | 连接 |
|---|---|---|
| PB7 / AF4 | UART4_TX | → G431 U2.2 / RX |
| PB6 / AF4 | UART4_RX | ← G431 U2.1 / TX |
| GND | 共地 | ↔ G431 GND |
| PD4 | `BOOT0_CTRL_G431` | 纯串口模式可不使用 |
| PD7 | `G431_ENABLE_IN` | 纯串口模式可不使用 |
| PD6 | `G431_FAULT_OUT` | 纯串口模式可不使用 |

当前 Linux 设备树已启用 UART4，设备节点为 `/dev/ttySTM3`。配置和测试：

```sh
stty -F /dev/ttySTM3 115200 cs8 -cstopb -parenb raw -echo
printf 'PING\r\n' > /dev/ttySTM3
printf 'ARM\r\n' > /dev/ttySTM3
printf 'PITCH 10\r\n' > /dev/ttySTM3
printf 'YAW 10\r\n' > /dev/ttySTM3
printf 'STOP\r\n' > /dev/ttySTM3
```

若应用需要持续收发，应由一个进程独占 `/dev/ttySTM3`，使用 `termios` 配置 115200 8N1，并按行解析返回；不要让多个 shell 和服务同时打开同一串口。

### STM32MP257 实机联调脚本

下面脚本是本次 COM31 联调流程的可重复版本。它会依次执行通信检查、ARM 对齐、P/Y 各 10° 小角度运动、停止和最终状态查询。

> 警告：执行脚本会让两个电机转动。运行前必须使用限流电源、清空机构周围障碍、确认 P/Y 电机和编码器没有接反，并准备物理断电。

```sh
#!/bin/sh
set -eu

PORT=/dev/ttySTM3

if [ ! -c "$PORT" ]; then
    echo "ERROR: $PORT 不存在，请检查 UART4 设备树和驱动"
    exit 1
fi

# UART4：115200、8N1、raw、无回显和软件流控。
stty -F "$PORT" 115200 cs8 -cstopb -parenb raw -echo -ixon -ixoff

# 同时打开读写端，后台持续显示 G431 返回。
exec 3<>"$PORT"
cat <&3 &
READER_PID=$!

send()
{
    printf '%s\r\n' "$1" >&3
    echo "[MP257->G431] $1" >&2
}

safe_exit()
{
    # 无论正常结束还是 Ctrl+C，都尽力关闭云台功率。
    printf 'STOP\r\n' >&3 2>/dev/null || true
    sleep 1
    kill "$READER_PID" 2>/dev/null || true
    wait "$READER_PID" 2>/dev/null || true
    exec 3>&-
    exec 3<&-
}
trap safe_exit EXIT INT TERM

echo "[1/5] 只读通信检查"
send PING
send VER
send STAT
send ANGLE
sleep 2

echo "[2/5] ARM 与电角度对齐"
send ARM
echo "必须观察到 BOOT_ALIGN,OK 和 OK,ARM"
sleep 6

echo "[3/5] P 轴 10 度测试"
send "PITCH 10"
sleep 2
send ANGLE
sleep 1

echo "[4/5] Y 轴 10 度测试"
send "YAW 10"
sleep 2
send ANGLE
sleep 1

echo "[5/5] 停止并检查安全状态"
send STOP
sleep 1
send STAT
send FAULT
sleep 2

echo "检查最终返回应包含 BOOST=0,MOTOR=0,HOLD=0,FAULT=0"
```

保存为 `g431_uart_test.sh` 后运行：

```sh
chmod +x g431_uart_test.sh
./g431_uart_test.sh
```

本次实测的关键返回如下，角度允许存在约 1–2° 的跟踪误差：

```text
OK,G431
VER,PROTO=2.0,FW=G431-GIMBAL,CAPS=UART_ONLY|ARM|...
BOOT_ALIGN,START
BOOT_ALIGN,OK
OK,ARM
ANGLE,...,P_DEG=9,Y_DEG=0,...
ANGLE,...,P_DEG=9,Y_DEG=9,...
OK,STOP
STAT,...,BOOST=0,MOTOR=0,HOLD=0,...,FAULT=0,...
```

脚本适合人工验收，不应直接作为产品服务。产品程序应逐行解析返回，只有明确收到 `OK,ARM` 才进入运动状态；超时、`ERR`、串口断开或故障时立即发送 `STOP` 并切断运动许可。

设备树恢复文件和生成工具见项目根目录：

- `mp257-current.dtb`：修改前导出。
- `mp257-uart4-pb6-pb7.dtb`：已验证版本。
- `tools/patch_mp257_uart4_dtb.py`：可复用生成脚本。

## 6. ESP32-S3 调试主机

ESP32-S3 为 3.3 V GPIO，可直接连接。Arduino Uno 为 5 V GPIO，必须对 Uno TX 及所有输出信号做可靠电平转换，因此不作为首选。完整示例见 [ESP32-S3 测试主机](examples/esp32_s3_gimbal_host.md)。

## 7. 必须遵守的注意事项

1. 所有插拔和改线都在电机电源断开时完成。
2. 首次运行使用限流电源，只测试 P/Y 各 10°；确认轴映射后再扩大范围。
3. `ARM` 对齐期间电机会动作，机构周围不得有人手、线束或硬物。
4. 两轴没有软件机械限位；不使用滑环时禁止连续多圈缠绕线束。
5. `STOP` 是软件停止，不替代物理断电或急停开关。
6. 收到 `BOOT_ALIGN,OK` 和 `OK,ARM` 前禁止发送运动命令。
7. 故障后先执行 `STOP` 并断开电机电源，再检查编码器、电机相线和机构卡滞。
8. 不需要升级时保持 BOOT0 硬件下拉，避免上电进入 ROM Bootloader。

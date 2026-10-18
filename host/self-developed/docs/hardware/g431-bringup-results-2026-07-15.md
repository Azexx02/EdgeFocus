# G431 Bring-up 实测记录（2026-07-15）

## 当前结论

- 独立 `gimbal_bringup` 已完成 host 测试、AArch64 交叉编译和板端部署。
- 板端 6 个新增 gimbal/power 测试全部通过。
- 无运动 probe **未通过 UART 回包门槛**，因此没有执行 Enable、对齐、STOP、Fault 注入或机械运动。
- 正式 `focus_app.service` 始终保留 `EDGEFOCUS_DISABLE_GIMBAL=1`，M33 风扇及 bridge 未受影响。

## 产物

- 当前板端目录：`/opt/edgefocus/tools/gimbal-recheck-20260715-2320`
- 当前 `gimbal_bringup` SHA-256：`0081205325341cf9d07d9c48cd93b3066f6dd597c3284bb051e6b92ef284df54`
- 历史首次工具目录在返板后已不存在；当前工具由最新源码 clean AArch64 全目标构建，并经编译机、本机、板端三端哈希校验。
- 板级配置仍为 `motion_verified=false`，机械参数全部保持未验证。

## 安全电平与服务状态

探测完成后使用 libgpiod 以 `--as-is` 读取：

```text
PD4 BOOT0   = 0 (inactive)
PD6 FAULT   = 1 (inactive, 外部上拉有效)
PD7 ENABLE  = 0 (inactive)
```

`/dev/ttySTM1` 无残留 owner；以下服务均保持 `active`：

```text
focus_app.service
edgefocus-m33-fan.service
edgefocus-fan-bridge.service
```

## UART probe 原始结果

工具在 PD4=0、PD7=0 下依次发送 `VER/PING/STAT/FAULT/ANGLE`。五步均超时，未收到任何完整 UART 行：

```json
{
  "error": "timeout",
  "failed_steps": ["VER", "PING", "STAT", "FAULT", "ANGLE"],
  "fault_active": false,
  "operation": "probe",
  "state": "DISABLED",
  "success": false,
  "timeout": true,
  "uart_transcript": []
}
```

内核串口计数：

```text
ttySTM1 / 0x40330000: tx=36, rx=0
```

设备树状态为 `okay`，live pinmux 确认 UART4 为 `PG14/PG15 AF6`；当前 DTB 默认组中 PG14 是推挽 TX，PG15 是 RX 输入。最新网表显示 `UART4_TX/U1.D51 -> R77 -> G431_UART_RX`、`G431_UART_TX -> R78 -> UART4_RX/U1.D52`，软件侧设备节点、复用和方向未发现颠倒；仍需示波器或逻辑分析仪确认板上实际波形。

## G431 开关修复返板复验

返板、联网和校时后，先确认 ttySTM1 无 owner、正式服务仍紧急禁用云台、PD4/PD6/PD7=`0/1/0`，再用新工具执行一轮同样的禁能 probe。结果仍为五条命令全部超时、transcript 为空；内核计数为：

```text
ttySTM1 / 0x40330000: tx=31, rx=0
```

复验退出后 PD4/PD6/PD7 仍为 `0/1/0`，ttySTM1 无 owner，`focus_app`、M33 fan、fan bridge 均保持 active。G431 Fault 线已释放，但 UART 回包门槛仍未通过，因此没有执行 Enable、对齐、STOP 或机械运动。

用户随后确认机械净空并允许转动，硬件侧补充诊断回复可能要求 Enable=1。修正 `enableAndAlign()` 为 PD7 低 100 ms 后直接拉高、等待异步 `BOOT_ALIGN,OK`、成功后才执行五项诊断；对应 host 与 ARM 板端 fake-I/O 测试通过。使用 SHA-256 为 `a3c88a985f0ac49617b6b4f7da203308f6285518dd9d11775c6404db2ee5f37d` 的新独立工具执行首次高沿，结果为：

```json
{
  "error": "timeout",
  "fault_latched_reason": "align_timeout",
  "operation": "align",
  "state": "FAULT",
  "success": false,
  "timeout": true,
  "uart_transcript": []
}
```

3 秒超时后工具已立即禁能；未进入后置诊断，也没有发送目标角度命令。退出后的物理读值仍为 PD4/PD6/PD7=`0/1/0`，ttySTM1 tx=31/rx=0。JSON 中 `fault_active=true` 是主机状态机因超时锁存的内部 Fault；退出后 PD6 实测仍为高。

用户现场确认该高沿期间云台确实发生动作。随后部署仅用于 bring-up 的 `enabled-probe`（SHA-256 `4b930bb86101560761919fd3605561e4ab441bfec8063d791d05e38c234cb1c5`）：Enable 高后被动监听约 1 秒，再发送 `VER/PING/STAT/FAULT/ANGLE`，禁止所有目标运动命令。五条仍全部超时，UART transcript 为空；内核计数从 tx=31/rx=0 增加为：

```text
ttySTM1 / 0x40330000: tx=62, rx=0
```

这证明 Linux 端在使能窗口内确实发送了第二轮 31 字节，但没有收到任何字节。结合电机发生动作，当前优先检查 G431 是否烧录了带 UART 协议的固件，以及 `G431_UART_TX -> R78 -> UART4_RX/PG15` 的实际波形/焊接；在 RX 恢复前继续禁止目标运动。

## 最终正式协议门槛复验（2026-07-16）

按最终协议修正为：Enable=0 稳定 500 ms，最多 3 次 `PING\r\n`，只接受 `OK,G431`/`PONG`/`OK,PING`，成功后才允许 Enable 高沿；对齐超时 10 秒。Host 3/3、ARM 板端 3/3 测试通过后，最终工具在安全态执行严格启动。

三次 PING 均无返回，UART transcript 为空；内核计数从 tx=62/rx=0 变为 tx=80/rx=0，精确对应三帧各 6 bytes 的 `PING\r\n`。PD7 全程未拉高，云台没有进入对齐；退出后 PD4/PD6/PD7=`0/1/0`、ttySTM1 无 owner，三项服务 active。

软件启动协议和“无回包绝不使能”安全门已完成实测。后续仅在 G431 PC10/U17.52、R78 两端及 MP257 PG15 恢复可见 UART 波形后继续机械调试。

## G431 重刷后独立复验（2026-07-16）

用户重新刷写 G431 后，保持正式主程序不变并通过 COM3 板端控制台运行同一独立工具。重启后的基线为 UART4 tx=0/rx=0、PD4/PD6/PD7=`0/1/0`、ttySTM1 无 owner。禁能 `probe` 的 `VER/PING/STAT/FAULT/ANGLE` 仍全部超时，transcript 为空；结束后 UART4 tx=31/rx=0，GPIO 保持 `0/1/0`。

因此重刷动作没有恢复主控接收链。按照正式协议，缺少 `OK,G431` 时禁止 Enable 高沿，故没有执行对齐或运动；主程序未修改、未部署。下一步仍需确认刷入镜像确实把 PC10 配置为 115200 UART TX，并测量 PC10/U17.52、R78 两端和 MP257 PG15。

## 后续硬件门槛

在以下任一证据取得前，禁止执行 `align` 或 `move`：

1. 确认 G431 电源、固件运行状态与禁能态 UART 响应；
2. 用逻辑分析仪确认 MP257 TX 到达 G431 RX、G431 TX 是否有返回；
3. 若固件只在 Enable 后响应，需在机械净空、限流供电条件下明确批准首个低→高边沿测试。该动作会触发自动电角度对齐，不属于无运动 probe。

当前安全回退是固定相机：不启用正式云台 feature，不影响视觉、会话、BLE 或 M33 风扇。

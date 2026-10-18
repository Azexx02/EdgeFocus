# 云台 UART4 / 协议 2.0 / 会话跟踪设计

## 目标

把已经在板端实测通过的 STM32MP257 UART4 → STM32G431 云台链路收束成可测试、可降级的正式模块；先由独立工具验收，再接入 PRECHECK 和正式会话。主程序在独立验收通过前保持云台禁用。

## 已验证基线

- Linux UART：`/dev/ttySTM3`，115200 8N1，命令以 `\r\n` 结束。
- G431：`PROTO=2.0`，能力 `UART_ONLY|ARM|YP_CONTINUOUS|FOC_HOLD|TRACK|CIRCLE`。
- 实机已验证 `PING / VER / STAT / FAULT / ANGLE / ARM / HOLD / PITCH / YAW / TRACK / STOP`。
- `STOP` 后验收以 `BOOST=0,MOTOR=0,HOLD=0,FAULT=0` 为准；卸力后重力导致角度变化不是控制失效。
- `UARTERR` 在停止态也可能缓慢增长；只能把增量与连续无应答作为退化证据，不能用历史非零值直接封锁云台。

## 控制模式

配置仅允许两个确定值：

1. `uart_only`（默认）：只申请 UART4；使用 `ARM` 对齐；故障通过协议查询与应答新鲜度判断；紧急软件停机先写单字节 `!`，再执行 `STOP` 和状态复核。
2. `uart_gpio_interlock`（显式增强）：申请 UART4、PD4、PD6、PD7；保持 BOOT0=0，通过 Enable 上升沿对齐，并以低有效 Fault 做独立联锁。

不提供 `auto`。显式增强模式初始化失败时云台不可用，不允许静默转为 UART-only，避免产品在不知情时降低安全级别。

## 协议适配

- 初始化先 `PING`，再读取 `VER/STAT/FAULT/ANGLE`；UART-only 必须确认协议 2.0 与 `ARM/TRACK` 能力。
- UART-only 对齐：发送 `ARM`，同时等待 `BOOT_ALIGN,OK` 与 `OK,ARM`，两者缺一即失败。
- `ANGLE` 解析 `P_DEG/Y_DEG`；保留旧字段解析只用于测试兼容。
- 离散定位使用整数度 `SET YP yaw pitch`；视觉跟踪使用 0.1° 单位 `TRACK YP yaw10 pitch10`。
- 分别识别 `SET,YP_OK`、`DEG,...`、`TRACK,YP_OK`、`OK,HOLD_ON` 和 `OK,STOP`，禁止用任意 `OK` 匹配运动命令。

## 运动安全与环形坐标

G431 的角度为 0–359° 环形坐标。Linux 侧在每次 ARM 成功后记录参考角，所有 PRECHECK、人工微调和会话跟踪都使用相对参考角的最短弧计算，并受配置的 yaw/pitch 相对窗口约束。首版只开放保守窗口，不使用整圈运动，也不跨越窗口累计旋转。

离散目标和跟踪目标都先经过同一个边界检查与归一化。未知角度、未完成 ARM、未验证窗口或目标越界时拒绝运动。

## PRECHECK

- PRECHECK 不进入分心核心、不写会话分心证据，但允许视觉模型输出目标信息。
- ARM 后先原位观察；稳定找到人即 HOLD 并进入人工确认。
- 未找到时按相对参考角的有限 waypoints 搜索；到期限仍未找到则回参考位并允许人工微调。
- 人工控制仍使用 session id、递增 sequence 和 manual lease；不能绕过控制器直写串口。

## 正式会话跟踪

- FOCUSING 阶段读取现有唯一视觉链的 `VisualSnapshot`，不创建第二路摄像头。
- 优先使用可靠的人脸/头部关键点中心；不可用时使用主体框上部中心。
- 仅处理新鲜且时间戳递增的视觉帧；误差经过指数平滑、死区、比例增益和单步限幅后转换为 `TRACK YP`。
- 跟踪命令最多 10 Hz，并且只在新视觉证据到达时发送；PAUSED 只 HOLD，不跟踪。
- 短时丢人保持当前位置；持续丢失停止发 TRACK 并保持，不在正式会话中自动扫场。目标重新稳定后恢复。
- 坐标方向、死区、增益、最大步长和丢失超时均为板级配置，避免硬编码机械方向。

## 健康、故障与恢复

- 真实 Fault、串口读写失败、连续三次健康查询无应答、ARM/STOP 验证失败会进入 Fault。
- `UARTERR` 只记录增量；单次增长不立即 Fault，若与连续应答失败同时出现则作为诊断原因。
- Fault 立即停止运动；UART-only 发送 `!` 后尝试 `STOP`，GPIO 模式还要拉低 Enable。
- Fault 后每 5 秒做一次低频 `PING/FAULT/STAT`；连续健康后回到 Disabled/Recoverable，只有当前会话阶段允许时才重新 ARM。
- 云台故障只降级云台插件，视觉、会话和提醒继续运行。

## 实施与验收顺序

1. 协议解析、命令格式、双模式 I/O 与 fake-I/O 单测。
2. 独立 bring-up：只读诊断、ARM/HOLD、小角度、TRACK、STOP、最终断力状态。
3. 主机与 AArch64 clean build；独立二进制部署，正式主服务仍禁用。
4. 写入实测板级参考窗口和方向参数，验证 PRECHECK 搜索、人工租约、Fault/恢复。
5. 验证 FOCUSING 跟踪、PAUSED HOLD、退出会话 STOP；最后才解除正式服务禁用。

## 非目标

- 不修改 G431 固件；不实现多圈旋转。
- 不让云台参与分心等级判定。
- 不在本轮把 UART-only 包装成与硬件 Fault 联锁等价的安全等级。

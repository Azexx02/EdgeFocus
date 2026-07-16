# EdgeFocus 实机联动修复规格

## 目标

修复 Redmi Note 10 Pro / MIUI 14 / Android 13 实机发现的三个联动问题：连接主机后空闲计时被错误覆盖为 00:00 且无法拖动、开始 EdgeFocus 后首页没有进入 PRECHECK、APP 缺少可主动唤醒主机手环搜索/重连循环的入口。

## 状态与交互约束

1. 空闲时长只有一个权威来源：`TimerStateHolder.targetDurationMinutes`。首页和横屏控制面板的轮盘都写入该状态；主机 IDLE 快照中的 `remaining_ms = 0` 只表示没有活动会话，不得推断为 FOCUSING，也不得覆盖用户时长。
2. PRECHECK 由主机权威状态驱动。`session_control(start)` 返回 PRECHECK 后，竖屏首页与横屏控制面板都显示预检操作；PRECHECK 的剩余时间缺失或为 0 时，界面仍显示本次目标时长，正式倒计时只在主机进入 FOCUSING 后开始。
3. 手环搜索按钮在竖屏设置页和横屏设置页始终可见。只有主机声明 `band_proxy_control_v1` 时才启用；未声明时保留状态显示并说明当前固件仅支持自动搜索/重连。
4. 主机原有 `BleBandController` 持续搜索和断线重连逻辑保持不变。新增 `band_control` 只负责主动唤醒该循环：
   - `scan_connect`：未连接时跳过等待并立即进入下一轮发现；已连接时返回已连接，不打断链路。
   - `reconnect`：主动结束当前手环链路并立即进入现有重连循环。
5. 主机只有在实际注入可控制的 `DeviceManager` 时才声明 `band_proxy_control_v1`，命令响应返回是否接受、当前连接状态和动作。

## 验收

- IDLE `push_state` 即使包含 `remaining_ms: 0`，APP 仍保持可拖动的用户时长。
- 首页与控制面板从 start 响应进入 PRECHECK，可执行云台预检并确认进入正式专注。
- 两个设置入口均显示手环状态和主动搜索/重连按钮；能力缺失时按钮禁用但不隐藏。
- 主机自动搜索/重连不被关闭；APP 命令只缩短等待或要求重连。
- APP 单元测试、Debug 构建和 lint 通过；主机相关 C++ 测试与构建通过。

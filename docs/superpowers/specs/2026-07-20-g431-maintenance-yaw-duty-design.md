# G431 维护档 Yaw 静摩擦余量修正

日期：2026-07-20  
状态：已获用户批准，等待规格复审

## 1. 现场证据

协议 3 首装已完成电气校准和永久原点捕获。`current-sample` 的首个维护运动目标为 yaw `5.7°`、pitch `7.4°`；pitch 到达 `7.4°`，yaw 长时间停在约 `3.0°`，最终 `motion_settle_timeout`。编码器、电流采样和 UART 均有效，未触发 Fault。固件当前在维护 profile 中将两轴输出限制为 `axis_hold_min_duty()`；yaw 的该值为 `80` tick，而同轴电气校准的受控探测使用 `80 + 100 = 180` tick 并已成功。

## 2. 目标与非目标

目标：让维护 profile 的 yaw ±3° 验证能够克服实机静摩擦并按现有 settled 条件真实到位，以便取得有效电流样本。

非目标：不修改生产 profile，不放宽 settled 条件，不扩大维护 ±10° 安全窗，不改变 UART 协议、速度斜坡、原点、校准结果或电流保护策略。

## 3. 设计

新增显式维护输出上限：

- pitch：保持当前 `260` tick；
- yaw：从 `80` tick 调整为 `180` tick；
- 两轴维护上限不得超过各自生产 `HOLD_DUTY_TICKS`。

准确控制关系为：

```text
maintenance_yaw_max_duty = 180
output = clamp(controller_output,
               existing_yaw_min_start_duty /* 80 */,
               maintenance_yaw_max_duty)
```

配置层新增独立的 `GIMBAL_X/Y_MAINTENANCE_MAX_DUTY_TICKS`，`axis_hold_duty()` 在 maintenance mode 下只返回对应维护**上限**；`axis_hold_min_duty()` 和现有最小起步 duty 不修改。生产 mode 继续返回原生产上限。目标斜坡、PD/积分控制和 settle 门限均保持原样。这样 yaw 仍从 80 平滑启动，但最多可增加到 180 以跨过静摩擦；`180` 来自本机刚完成且成功的校准探测，不是经验占位值。

主机维护 CLI 新增一次性 `maintenance-move --axis yaw|pitch --delta-deg N`：仅通过类型化 `GimbalHardwareService` 完成 Probe → ARM MAINTENANCE → HOLD → Current Reset → 相对当前位置的小角度 verified move → Current Read → STOP。每次进程只执行一个增量运动并输出起点、最终绝对目标、最终位置、generation、settled 和电流状态，不开放任意 UART 文本。

## 4. 安全与失败处理

- 维护命令仍必须经过 `ARM MAINTENANCE`、HELLO 和校准/原点有效门。
- 维护目标仍限制在永久原点 ±10°，本次 CLI 只运行 ±3°。
- STOP、紧急字符、UART 超时和 Fault 路径不变。
- 若 yaw 仍不能在 5 秒内达到同 generation 的 `SETTLED=1`，立即 STOP，保持电流保护为 `UNCONFIGURED`，不继续加大 duty。
- 一次性维护运动的最终绝对目标必须由主机再次确认处于永久原点 ±10°；`delta-deg` 是相对当前权威位置的增量，不代表绝对目标一定在 ±3°。例如现场首点增量为 +3°，绝对目标实际为 yaw 5.7°、pitch 7.4°。
- 180 tick 在进入完整六段路径前必须分阶段验证：先单独 yaw +3°，STOP 并检查有效/未饱和电流、无 Fault；再单独 yaw -3°，STOP 并重复检查。任一阶段超时、越过 ±10°、ADC 饱和、Fault 或急停均终止验证。
- 若运动过快、振荡或机械声音异常，立即 STOP 并回刷当前 `222d08e` 固件。

## 5. 验证

1. G431 纯 C 核心测试通过。
2. Debug/Release 固件均完整重建并检查 Flash/RAM 占用。
3. 刷写后用 ST-LINK 回读校验固件。
4. 新增模式隔离单测：PRODUCTION 下 yaw/pitch 上限与修改前完全一致；MAINTENANCE 下仅 yaw 上限为 180、pitch 为 260；两轴维护上限不超过生产上限；STOP 清除 maintenance profile 后普通 ARM 使用生产上限；非维护命令不能进入维护分支。
5. `probe` 验证协议 3、校准 epoch 1、原点 epoch 1 和无 Fault。
6. 分别运行 `maintenance-move --axis yaw --delta-deg 3` 和 `--delta-deg -3`；每次完成后必须 STOP，并验证同 generation settled、目标仍在永久原点 ±10°、电流有效且未饱和、无 Fault。
7. 两个单向阶段均通过后重跑 `current-sample`，要求六段相对基准的 ±3° 路径全部以同 generation `SETTLED=1` 完成，并记录每段实际绝对目标。
8. 仅在 `current.valid=true`、`saturated=false`、`window_ms>=1000` 后评估保护阈值；本改动本身不写阈值。

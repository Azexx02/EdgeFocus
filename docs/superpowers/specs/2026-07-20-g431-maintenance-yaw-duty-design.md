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

## 6. 2026-07-21 实机复测修订：维护模式自适应抗静摩擦

### 6.1 新证据与原方案结论

候选固件 `8cabf8e` 已在实机运行第一段 yaw +3°：起点约 -0.5°、目标 +2.5°，轴移动到约 +1.1°后停滞，并在 5 秒 verified-settle 截止时间到达时安全失败。STOP 成功，无 Fault，校准与原点 epoch 均保留为 1，电流保护仍为 `UNCONFIGURED`；因此没有继续 -3°或完整六段采样。

停滞时剩余误差约 1.4°，即约 16 raw。当前 yaw 控制量约为 `16 * KP(6) + ramped static(5) = 101 tick`，明显低于已经配置的 180 tick 维护上限。由此可知，原第 3 节中“仅提高维护上限即可跨过末端静摩擦”的假设不成立；180 上限保留为安全边界，但不能作为持续输出或直接再次提高。

### 6.2 修订后的控制设计

新增 HAL 无关的 `gimbal_maintenance_stiction` 小状态机，仅由 yaw 的 maintenance profile 调用。生产 profile、pitch、协议、目标斜坡、PD 参数、settled 门限和 180 tick 上限全部保持不变。

抗静摩擦触发必须同时满足：

- 当前为 maintenance profile 且轴为 yaw；
- 目标斜坡已到终点：`target_delta_abs <= 2 raw`；
- 尚未 settled，且 `error_abs > 8 raw`；
- 滤波速度处于 `-1..+1 raw/control-cycle`；
- 上述条件连续保持 30 个 5 ms 控制周期，即 150 ms。

触发前完全返回原 PD/static `requested_duty`。触发后，临时 duty floor 从当前 `requested_duty` 起每个控制周期增加 2 tick，最终输出为：

```text
assist_floor = min(max(previous_floor, requested_duty) + 2,
                   maintenance_yaw_max_duty /* 180 */)
output = max(requested_duty, assist_floor)
```

下列任一条件出现时立即把资格计数和临时 floor 清零：退出 maintenance、轴不是 yaw、目标斜坡重新运动、进入 settled 误差带 `error_abs <= 8`、或实际速度离开 `-1..+1`。因此补偿只形成有界的“停滞解锁脉冲”，不会在轴已恢复运动后持续顶到 180，也不会进入生产模式。

`motor_pwm_hold_enable(false)`、STOP、Fault、重新 ARM 和新 yaw 目标均必须显式清除该状态。原 `gimbal_power_profile` 的生产/维护上限隔离和编译期约束继续保留。

### 6.3 可测试边界

纯 C 测试必须覆盖：

1. 生产 yaw、maintenance pitch、目标斜坡未完成或已 settled 时，输出严格等于原 requested duty。
2. maintenance yaw 停滞不足 150 ms 时不介入。
3. 满足 150 ms 后 floor 单调上升、永不超过 180，并且不改变扭矩方向。
4. 速度恢复、误差进入 settled 带、STOP/reset 或新目标到来时，计数与 floor 立即归零。
5. 归零后再次停滞必须重新等待完整 150 ms，不允许沿用旧补偿。

### 6.4 实机验证顺序

1. G431 纯 C 测试、Debug/Release 重建、Flash/RAM 检查和 ST-LINK 下载校验。
2. 无运动 `probe` 确认协议 3、calibration/origin epoch 1、无 Fault。
3. 单独运行 yaw +1°，要求同 generation `SETTLED=1`；随后 STOP，检查无 Fault、电流有效且未饱和。
4. +1°通过后，单独运行 yaw +3°并执行相同检查；失败则 STOP，不测试反向，不提高 180 上限。
5. +3°通过后再运行 yaw -3°；只有正反向均通过才运行完整 `current-sample`。
6. 本修订仍不写电流保护阈值；只有完整样本满足有效、未饱和、窗口至少 1000 ms 后，才另行评估阈值。

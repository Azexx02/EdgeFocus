# G431 维护档 Yaw 静摩擦余量修正

日期：2026-07-20  
状态：已获用户批准，等待规格复审

## 1. 现场证据

协议 3 首装已完成电气校准和永久原点捕获。`current-sample` 的首个维护运动目标为 yaw `5.7°`、pitch `7.4°`；pitch 到达 `7.4°`，yaw 长时间停在约 `3.0°`，最终 `motion_settle_timeout`。编码器、电流采样和 UART 均有效，未触发 Fault。固件当前在维护 profile 中将两轴输出限制为 `axis_hold_min_duty()`；yaw 的该值为 `80` tick，而同轴电气校准的受控探测使用 `80 + 100 = 180` tick 并已成功。

## 2. 目标与非目标

目标：让维护 profile 的 yaw ±10°验收运动能够克服实机静摩擦，在允许2°机械静差的前提下完成至少约8°同向有效位移，以便取得有效电流样本。

非目标：不修改生产 profile，不放宽 settled 条件，不扩大维护 ±10° 安全窗，不改变既有 UART 控制命令、速度斜坡、原点、校准结果或电流保护策略。允许增加一条只读、无运动副作用的维护诊断查询，但协议版本和既有命令语义不变。

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

主机维护 CLI 提供一次性 `maintenance-move --axis yaw|pitch (--delta-deg N|--target-deg N)`：仅通过类型化 `GimbalHardwareService` 完成 Probe → ARM MAINTENANCE → HOLD → 运动位置稳定验收 → Current Reset → 固定目标保持电流采样 → STOP。每次进程只执行一个运动并输出起点、最终绝对目标、最终位置、generation、真实settled和电流状态，不开放任意UART文本。

## 4. 安全与失败处理

- 维护命令仍必须经过 `ARM MAINTENANCE`、HELLO 和校准/原点有效门。
- 维护目标仍限制在永久原点 ±10°；CLI 支持绝对0°回中与最大10°相对运动。
- STOP、紧急字符、UART 超时和 Fault 路径不变。
- 若 yaw 不能在5秒内达到同generation、误差≤2°并连续稳定3个80 ms采样，立即STOP，保持电流保护为 `UNCONFIGURED`，不继续加大duty；`SETTLED=1`是更强结果但不是维护验收唯一必要条件。
- 一次性维护运动的最终绝对目标必须由主机再次确认处于永久原点±10°；`delta-deg`支持最大10°相对运动但计算出的绝对目标越界时拒绝，`target-deg`支持-10°到+10°确定性绝对目标。
- 180 tick 在进入完整六段路径前必须分阶段验证绝对位置0°、+10°、0°、-10°。任一阶段越界、ADC饱和、Fault或急停均终止验证。
- 若运动过快、振荡或机械声音异常，立即 STOP 并回刷当前 `222d08e` 固件。

## 5. 验证

1. G431 纯 C 核心测试通过。
2. Debug/Release 固件均完整重建并检查 Flash/RAM 占用。
3. 刷写后用 ST-LINK 回读校验固件。
4. 新增模式隔离单测：PRODUCTION 下 yaw/pitch 上限与修改前完全一致；MAINTENANCE 下仅 yaw 上限为 180、pitch 为 260；两轴维护上限不超过生产上限；STOP 清除 maintenance profile 后普通 ARM 使用生产上限；非维护命令不能进入维护分支。
5. `probe` 验证协议 3、校准 epoch 1、原点 epoch 1 和无 Fault。
6. 依次运行 `maintenance-move --axis yaw --target-deg 0`、`--target-deg 10`、`--target-deg 0`、`--target-deg -10`；每次完成后必须STOP，并验证同generation位置连续稳定、最终误差不超过2°、电流有效且未饱和、无Fault。
7. 两个单向阶段均通过后重跑 `current-sample`，要求六段相对基准的 ±3° 路径全部以同 generation `SETTLED=1` 完成，并记录每段实际绝对目标。
8. 仅在 `current.valid=true`、`saturated=false`、`window_ms>=1000` 后评估保护阈值；本改动本身不写阈值。

## 6. 2026-07-21 实机复测修订：维护模式自适应抗静摩擦

### 6.1 新证据与原方案结论

候选固件 `8cabf8e` 已在实机运行第一段 yaw +3°：起点约 -0.5°、目标 +2.5°，轴移动到约 +1.1°后停滞，并在 5 秒 verified-settle 截止时间到达时安全失败。STOP 成功，无 Fault，校准与原点 epoch 均保留为 1，电流保护仍为 `UNCONFIGURED`；因此没有继续 -3°或完整六段采样。

停滞时剩余误差约 1.4°，即约 16 raw。当前 yaw 控制量约为 `16 * KP(6) + ramped static(5) = 101 tick`，明显低于已经配置的 180 tick 维护上限。由此可知，原第 3 节中“仅提高维护上限即可跨过末端静摩擦”的假设不成立；180 上限保留为安全边界，但不能作为持续输出或直接再次提高。

### 6.2 修订后的控制设计

新增 HAL 无关的 `gimbal_maintenance_stiction` 小状态机，仅由 yaw 的 maintenance profile 调用。生产 profile、pitch、既有控制协议、目标斜坡、PD 参数、settled 门限和 180 tick 上限全部保持不变。

抗静摩擦触发必须同时满足：

- 当前为 maintenance profile 且轴为 yaw；
- 目标斜坡已到终点：`target_delta_abs <= 2 raw`；
- 尚未 settled，且 `error_abs > 8 raw`；
- 滤波速度处于 `-1..+1 raw/control-cycle`；
- 上述条件按 MCU 单调毫秒时钟连续保持至少 150 ms。

状态机只处理无符号 PWM 幅值，绝不生成或改变方向。`requested_magnitude` 是原 PD/static 控制器输出的绝对值，`requested_direction` 完全沿用现有控制器方向；方向无效、误差为零或控制器没有请求扭矩时不得触发补偿。触发前完全返回原 `requested_magnitude`。触发后，临时 duty floor 从当前 `requested_magnitude` 起每个控制周期增加 2 tick，最终输出为：

```text
requested_magnitude = abs(requested_duty)
requested_direction = existing_controller_direction

assist_floor = min(max(previous_floor, requested_magnitude) + 2,
                   maintenance_yaw_max_duty /* 180 */)
output_magnitude = max(requested_magnitude, assist_floor)
output_direction = requested_direction
```

状态机更新接口接收 MCU 单调时间 `now_ms`，所有时差采用无符号减法以兼容计数器回绕。首次满足资格条件时记录 `qualify_started_ms`；只有 `now_ms - qualify_started_ms >= 150` 才能触发。条件中断会清除资格起点，下一次必须重新累计真实墙钟时间。

每个新 yaw 目标最多允许一次补偿尝试：进入激活态时立即记录 `assist_started_ms` 并锁存 `assist_attempted`，在该目标生命周期内不得第二次触发。每次状态更新都必须先判断 `now_ms - assist_started_ms >= 300`，到期则在本轮输出计算前立即撤销临时 floor、恢复原 `requested_magnitude`，并锁存 `assist_exhausted`。该期限由 G431 本地单调时间判断，独立于主机 5 秒 verified-settle 超时、UART 和 STOP 是否正常；因此即使主机失联，也不会重复脉冲。正常控制任务继续运行时，补偿在真实 300 ms 到期后的首个控制调用撤销；若整个 MCU/控制中断停止运行，则由既有硬件 Fault/看门狗安全链负责关断，不把控制循环停摆误表述为软件状态机可单独解决的问题。

退出 maintenance、轴不是 yaw、目标斜坡重新运动、进入 settled 误差带 `error_abs <= 8`、控制器不再请求扭矩或请求方向反转时，立即把资格计数和临时 floor 清零。速度 `-1..+1` 只用于确认触发前确实停滞；补偿一旦触发，不因已经开始运动而提前撤销，而是在误差进入到位带、方向反转或满 300 ms 时结束。这样不会把单次编码器速度跳变误当成已完成脱粘，同时最坏持续时间和 180 tick 上限仍完全不变。`assist_attempted` 和 `assist_exhausted` 只允许由新目标、STOP、重新 ARM 或显式 reset 清除，且不会进入生产模式。

`motor_pwm_hold_enable(false)`、STOP、Fault、重新 ARM 和新 yaw 目标均必须显式清除该状态。原 `gimbal_power_profile` 的生产/维护上限隔离和编译期约束继续保留。

为使实机验证可观察，协议 3 增加只读维护查询 `MAINTASSIST?`。它不改变目标、HOLD、输出或任何保护状态，仅返回与控制状态分离的最近一次诊断快照：

```text
MAINTASSIST,GEN=<target_generation>,ATTEMPTED=<0|1>,EXHAUSTED=<0|1>,ACTIVE_MS=<0..300>,PEAK_FLOOR=<0..180>
```

诊断快照在补偿开始、结束或到期时更新，跨 STOP 保留，直到新 yaw 目标开始时清零；STOP 仍立即清除实际资格计数、floor 和激活态。查询需要已完成 HELLO，且只有 maintenance ARM 生命周期产生的数据才可标记 `ATTEMPTED=1`。该查询是新增的只读兼容扩展，协议版本保持 3.0，未知命令和既有响应不变。

### 6.3 可测试边界

纯 C 测试必须覆盖：

1. 生产 yaw、maintenance pitch、目标斜坡未完成或已 settled 时，输出幅值和方向严格等于原控制器输出。
2. maintenance yaw 停滞不足 150 ms 时不介入；以 5 ms、20 ms 和 50 ms 的不规则调用间隔验证资格按 `now_ms` 而不是调用次数计算，并覆盖毫秒计数器回绕。
3. 满足 150 ms 后 floor 单调上升、永不超过 180；分别覆盖正向和负向请求，并验证输出相位/方向与原控制器完全一致。
4. 激活后速度恢复本身不得提前撤销；误差进入 settled 带、方向反转、STOP/reset、新目标到来或达到 300 ms 时，计数与 floor 归零。
5. 归零后再次停滞必须重新等待完整 150 ms，不允许沿用旧补偿。
6. 首次激活即锁存 `assist_attempted`；无论因误差进入到位带、方向反转、控制器不再请求扭矩、STOP/reset、新目标，还是达到300 ms而撤销floor，同一目标均不得再次触发；速度恢复本身不得作为撤销条件。
7. 激活达到 300 ms 时，在该次输出计算前立即撤销 floor 并额外锁存 `assist_exhausted`；以 5 ms、20 ms 和 50 ms 调用间隔验证基于 `now_ms` 的期限，并覆盖计数器回绕。即使主机不发送 STOP，也不得再次触发或在状态机继续运行时保持 180。
8. 方向无效、误差为零或原控制器不请求扭矩时不触发补偿；新目标、STOP、重新 ARM 和显式 reset 可以清除 `assist_attempted` 与 `assist_exhausted`。
9. 诊断快照正确记录 generation、实际激活时长和峰值 floor；STOP 清除控制状态但不擦除最近快照，新 yaw 目标清零快照，查询本身不得改变任何控制状态。

### 6.4 实机验证顺序

1. G431 纯 C 测试、Debug/Release 重建、Flash/RAM 检查和 ST-LINK 下载校验。
2. 无运动 `probe` 确认协议 3、calibration/origin epoch 1、无 Fault。
3. 使用绝对目标0°在维护窗口内自动回中；要求误差≤2°并连续稳定3个80 ms样本，失败则STOP并提示人工检查，不循环撞限位。
4. 依次运行绝对+10°、0°和-10°，要求方向正确、每个10°行程至少约8°同向有效位移。每次位置验收后才执行 `CURRENT RESET`，保持目标并每100 ms轮询CURRENT，最多等待2000 ms，直到 `WINDOW_MS>=1000` 且 `VALID=1`；读取同generation的 `MAINTASSIST?` 并确认仍受300 ms/180 tick限制，随后无条件STOP。
5. 只有0°、+10°、再次0°和-10°均通过才运行完整 `current-sample`；任一失败均不提高180上限。
6. CLI 对成功和失败路径都执行 finally 风格 STOP；为凑满电流窗口不得继续改变目标。只有完整样本满足有效、未饱和、窗口至少 1000 ms 后，才另行评估保护阈值；本修订仍不写阈值。

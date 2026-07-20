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

`axis_hold_duty()` 在 maintenance mode 下返回对应维护上限，生产 mode 继续返回原生产上限。最小起步 duty、目标斜坡、PD/积分控制和 settle 门限均保持原样。这样 yaw 仍从低输出平滑启动，但有足够余量跨过静摩擦；`180` 来自本机刚完成且成功的校准探测，不是经验占位值。

## 4. 安全与失败处理

- 维护命令仍必须经过 `ARM MAINTENANCE`、HELLO 和校准/原点有效门。
- 维护目标仍限制在永久原点 ±10°，本次 CLI 只运行 ±3°。
- STOP、紧急字符、UART 超时和 Fault 路径不变。
- 若 yaw 仍不能在 5 秒内达到同 generation 的 `SETTLED=1`，立即 STOP，保持电流保护为 `UNCONFIGURED`，不继续加大 duty。
- 若运动过快、振荡或机械声音异常，立即 STOP 并回刷当前 `222d08e` 固件。

## 5. 验证

1. G431 纯 C 核心测试通过。
2. Debug/Release 固件均完整重建并检查 Flash/RAM 占用。
3. 刷写后用 ST-LINK 回读校验固件。
4. `probe` 验证协议 3、校准 epoch 1、原点 epoch 1 和无 Fault。
5. 重跑 `current-sample`，要求六段 ±3° 路径全部以同 generation `SETTLED=1` 完成。
6. 仅在 `current.valid=true`、`saturated=false`、`window_ms>=1000` 后评估保护阈值；本改动本身不写阈值。

# G431 协议 3 维护与首装向导

适用工程：`focus_app_national` + `G431_national`。所有维护命令复用正式 `GimbalHardwareService`，禁止另开原始 UART 终端与主程序抢占串口。

## 1. 安全边界

1. 停止 `focus_app.service` 后才允许独立 CLI 占用板端云台设备。
2. 清空云台周围机械空间，确认电机、电源、排线和 Fault 输入正常。
3. 所有可能运动的 CLI 都要求 `--allow-motion --confirm-board`。
4. 电气校准只表示固件已接受后开始；不得在 `CAL,ACCEPTED` 后立即断电或 STOP，需等待 operation 完成。
5. 电流保护阈值来自实机至少 1 秒的有效、未饱和采样，不得填经验占位值。
6. NVM 清除要求两重明确确认；`clear-all` 会同时撤销电气校准、机械原点和电流保护。

## 2. 首装顺序

```sh
systemctl stop focus_app.service
gimbal_bringup probe --config /opt/edgefocus/config/gimbal-board.json
gimbal_bringup calibrate-electrical --allow-motion --confirm-board
```

电气校准成功后，人工把云台摆到产品定义的机械正前方，再执行：

```sh
gimbal_bringup capture-origin --confirm-origin-capture
```

该原点写入 G431 A/B Flash 事务槽。普通 `ARM`、会话启动、故障恢复和重启均不得重新定义它。默认软件安全窗是原点两轴 ±70°。

## 3. 电流保护标定

维护低功率模式下采集：

```sh
gimbal_bringup current-sample --allow-motion --confirm-board
```

JSON 中必须同时满足：

- `current.valid=true`
- `current.saturated=false`
- `current.window_ms >= 1000`
- `samples` 随窗口增长，`sample_age_ms` 新鲜

结合多方向、空载与受控轻阻力采样确定峰值/均值阈值后写入：

```sh
gimbal_bringup protect-current-set \
  --p-peak 0 --p-avg 0 --y-peak 0 --y-avg 0 --stall-ms 250 \
  --confirm-current-protection
```

上例的 `0` 只是参数位置示意，工具会拒绝无效组合；必须替换为实测值，且 peak 大于对应 average。

### 3.1 维护 yaw 运动验证与脱粘诊断

新固件通过 `MAINT_ASSIST_DIAG` 能力声明只读诊断 `MAINTASSIST?`。该辅助仅作用于维护模式 yaw：目标斜坡完成、误差大于 8 raw、速度在 ±1 raw、连续停滞至少 150 ms 时，一次性把既有请求下限每个控制周期提高 2 tick，最高 180，最长 300 ms。速度只用于触发前确认停滞；触发后持续到误差进入到位带、方向反转或 300 ms 到期，单个编码器速度跳变不会提前撤销；生产模式和 pitch 不变。

上板必须分级执行。维护验收目标为永久零位的 `0° / +10° / -10°`，不再使用容易受静摩擦影响的±3°小行程。若任一轴起点超出±10°，仅在两轴raw有效、样本年龄≤50 ms、无Fault且仍处于持久化产品安全窗（当前±70°）时，CLI才允许一次特殊 `AUTO_CENTER_RECOVERY` 双轴回到 `(0°,0°)`；该恢复不能用于其他目标，5秒超时、持续背离或越出产品安全窗都会立即STOP。随后执行请求目标并最终STOP，无需人工持续扶正：

```sh
gimbal_bringup maintenance-move --axis yaw --target-deg 0 --allow-motion --confirm-board
gimbal_bringup maintenance-move --axis yaw --target-deg 10 --allow-motion --confirm-board
gimbal_bringup maintenance-move --axis yaw --target-deg 0 --allow-motion --confirm-board
gimbal_bringup maintenance-move --axis yaw --target-deg -10 --allow-motion --confirm-board
```

维护验收要求同generation、位置有效、无Fault且目标未越界；误差不超过2°并连续稳定3个80 ms采样即可通过，`SETTLED=1` 是更强结果但不是该维护命令的唯一必要条件。10°目标仍保证约8°同向有效位移；该规则不改变生产控制器的settled定义。

每次输出应包含 `maintenance_assist`：其 `target_generation` 与运动 generation 一致，`active_ms <= 300`，`peak_floor <= 180`。到位后工具先执行 `CURRENT RESET`，保持固定目标并每 100 ms 读取一次电流，最多等待 2 秒取得完整、未饱和的 1000 ms 窗口；Fault、饱和或超时均失败。无论成功与否，工具退出前都执行 STOP。

只有0°、+10°、再次0°和-10°验证全部通过，才允许继续完整 `current-sample`；本步骤不写入任何电流保护阈值。

## 4. 生产前验证

```sh
gimbal_bringup arm-production --allow-motion --confirm-board
gimbal_bringup probe
```

生产使能要求协议 3 协商成功、电气校准有效、机械原点有效、电流保护有效。工具退出会 best-effort STOP 并撤销 Enable。

## 5. 隐藏网页维护

入口：`http://<board-ip>:<demo-port>/maintenance`。主展示页不提供链接。

开放条件：

- 无正式会话；
- 演示 runtime 为 `LIVE_CAMERA`，预录 NPU/纯前端回放均未运行；
- 预检控制器不在搜索、跟踪或人工租约状态。

页面读取一次性口令，操作员原样输入后，主机会先执行 STOP 并确认 `DISABLED`，然后签发仅 60 秒有效的 token。NVM 清除还要求固定二次确认文本。网页只传维护枚举和结构化参数，不接受 UART 字符串。

## 6. 恢复与清除

```sh
gimbal_bringup nvm-clear-origin --confirm-nvm-clear --confirm-second
gimbal_bringup nvm-clear-all --confirm-nvm-clear --confirm-second
```

- `clear-origin` 后只需重新人工摆正并 capture-origin。
- `clear-all` 后必须重新执行电气校准、原点捕获和电流保护标定。
- 任一维护失败先 STOP、断开使能并导出 CLI JSON；不要循环高速 ARM/CAL。

## 7. 日志留存

每次维护把标准输出重定向为按时间命名的 JSON 文件，至少保存：协议版本、校准/原点 epoch、operation generation/progress/status、电流窗口、阈值、UART transcript 和最终 STOP 结果。例如：

```sh
mkdir -p /var/log/edgefocus/gimbal-maintenance
gimbal_bringup probe > /var/log/edgefocus/gimbal-maintenance/probe-$(date +%Y%m%d-%H%M%S).json
```

验收时不得把 `firmware_accepted=true` 等同于物理完成；校准看 operation 结束与 epoch/valid，生产运动看同generation的 `POSE,SETTLED=1`。`maintenance-move` 例外地按同generation位置误差≤2°且连续3个80 ms样本稳定验收，并保留真实settled字段。

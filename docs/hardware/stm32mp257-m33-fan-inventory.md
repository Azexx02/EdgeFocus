# STM32MP257 M33 风扇实施盘点

状态：M33 已开机接管 TIM17_CH1N/PB10；PB11 tach、温控循环、A35 bridge、健康门和 Linux fallback 已上板验证，三项开机服务已 enabled。

- 盘点日期：2026-07-15
- 目标板：ALIENTEK STM32MP257 Evaluation Board
- 板端地址：`192.168.10.174`

## 1. 运行板事实

| 项目 | 实测值 | 实施含义 |
|---|---|---|
| Linux CPU online | `0-1` | 两个 A35 均在线；风扇策略不属于业务会话线程 |
| DT model | `ALIENTEK STM32MP257 Evaluation Board` | 该字符串沿用厂商板型，但实际是已修改底板；构建骨架可参考 ATK BSP，pinmux/电气/资源归属必须以自研板 v17 DT、网表和实测为准 |
| compatible | `st,stm32mp257d-atk`, `st,stm32mp257` | 后续固件/overlay 必须保留板型匹配 |
| M33 remoteproc | `remoteproc0`, `running` | 当前由风扇固件占用；切换/回滚必须先停止 bridge |
| 当前 M33 firmware | `EdgeFocus_FanController_CM33_NonSecure_stripped.elf` | 已由 `edgefocus-m33-fan.service` 开机加载 |
| firmware format | `ELF` | 当前 ATK 启动脚本走 non-secure ELF 路径，不要求首轮 TEE 签名 bin |
| RPMsg 设备 | `/dev/ttyRPMSG0` 及 rpmsg control/name-service | 当前 endpoint 是 rpmsg-tty；业务协议不得假定一次 TTY write 等于一帧 |

厂商 `st-m33firmware-load.service` 当前为 disabled；其默认脚本和 echo ELF 保留用于人工回滚：

```text
/sbin/st-m33firmware-load-default.sh start
```

默认项目目录：

```text
/usr/local/Cube-M33-examples/STM32MP257D-ATK/Demonstrations/OpenAMP_TTY_echo
```

板上只有启动脚本、测试脚本和预编译 ELF，没有可修改的 STM32CubeIDE/CubeMP2 源工程。

## 2. 温度、PWM 与 tach 事实

### 温区

| thermal zone | type | 盘点温度 |
|---|---|---:|
| zone0 | `soc-thermal` | 约 44.38°C |
| zone1 | `cpu-thermal` | 约 45.31°C |

Linux bridge 只允许按 type 精确选择 `cpu-thermal`。找不到或出现多个同名温区时停止发送温度，由 M33 的 3 秒 TTL 进入安全高转，不回退到“第一个温区”。

### 当前 Linux PWM 基线

```text
/sys/class/pwm/pwmchip0 -> timer@40270000/pwm
npwm=1
pwm0/period=40000
pwm0/duty_cycle=36000
pwm0/enable=1
pwm0/polarity=normal
driver=stm32-pwm
```

DT 路径为 `/soc@0/bus@42080000/timer@40270000/pwm`，provider/child compatible 分别为 `st,stm32mp25-timers` 与 `st,stm32mp25-pwm`，状态均为 okay。

运行 pinctrl 已进一步确认 PB10/AF7 对应 `TIM17_CH1N`（互补输出），不是普通 `TIM17_CH1`；default pinconf 为 pull-down、push-pull、low slew。M33 固件必须启动 CH1N，并匹配这些板级电气设置。

Linux 的 `pwm0 unexport` 不等于释放定时器。实际绑定层级为：

```text
stm32-timers/40270000.timer
  -> stm32-pwm/40270000.timer:pwm
```

受控接管需要先停写入者、unexport，再按子设备到父设备顺序 unbind；失败回滚按父到子顺序 bind。

### PB11 / CAN

- GPIOB 为 `gpiochip1`，PB11 已在 Linux 原型和 M33 EXTI2 line 11 两条路径上实测到规则上升沿。
- CAN 节点 `/soc@0/bus@42080000/can@402d0000` 保持禁用；PB11 由 M33 申请为上拉输入和 rising-edge EXTI，不恢复 FDCAN1 复用。
- 2 PPR 下实测：34,000 ns 约 660～1,140 RPM，30,000/22,000/14,000/8,000 ns 分别约 1,770/3,060/4,440/5,760 RPM。1/2/4 PPR 候选中只有 2 PPR 的满速换算落入额定 6000 RPM 的允许窗口。
- M33 与 Linux fallback 都能从 PB11 得到非零测速；这证明方向、连续性和堵转诊断可用，但风扇接口端的 PWM 波形仍需示波器或逻辑分析仪独立留档。

## 3. 工具链与源码盘点

- 交叉编译机：`hu@192.168.10.6`。
- 可用：CMake 3.28、g++ 13.3、现有 A35 Yocto SDK/离线依赖缓存。
- 本机已安装 STM32CubeIDE 1.17.0 和 GCC 12.3.rel1；ATK 源包位于 `_workspace/m33/STM32Cube_ATK_FW_MP2_V1.0.0`。
- 仓库使用 CMake/Ninja 复用厂商 HAL、OpenAMP、resource table 与 linker script，不修改或提交整个厂商包。
- `firmware/m33/fan_controller/atk/build_atk.ps1` 已可复现生成 remoteproc ELF。

## 4. 当前仓库已经完成的边界

| 层 | 路径 | 状态 |
|---|---|---|
| 控制核心 | `platform/fan/core/` | 已实现并通过主机测试 |
| 固定二进制协议 | `platform/fan/core/FanProtocol.*` | 已实现 CRC、版本、长度和分片边界 |
| Linux bridge | `platform/fan/bridge/` | 已严格编译；只在 M33 capability 握手后发送 `cpu-thermal` |
| M33 便携任务 | `firmware/m33/fan_controller/FanControllerApp.*` | 已主机测试；不包含猜测的 HAL/RIF/pinmux |
| systemd 服务 | `platform/fan/board/`, `platform/fan/bridge/edgefocus-fan-bridge.service` | loader、bridge、健康门和 fallback 已安装；校准门禁未批准且未 enable |
| 板级 firmware | `firmware/m33/fan_controller/atk/` | 已从 ATK echo 派生并可构建；stage 0-9、PWM 和完整控制循环已上板通过 |

重启契约已覆盖：Linux bridge `boot_id` 改变时，M33 清空旧温度 sequence 窗口，立即回到 BOOT_SAFE，接收新进程从头开始的序号；两帧合法温度后才恢复受控状态。

## 5. 当前上板结论（2026-07-15）

- stage 0-9、TIM17 reset/config、CC1NE、MOE、CEN、CCR 写入和 100 ms 完整控制循环均已通过；控制循环曾连续验证 60 秒。
- 旧版约 32 秒后的整板 watchdog 复位根因不是 PWM，而是 non-secure M33 调用 `HAL_GetUIDw0()`。当前 boot id 使用本地运行计数生成 nonce，禁止直接访问 UID/OTP。
- 独立 bridge 已安装为 `/usr/local/bin/edgefocus-fan-bridge`，状态写入 `/run/edgefocus/fan-status.json`；只精确读取 `cpu-thermal`。
- bridge 正常连接时实测 `protocol_ready=true`、`CONTROLLED/NORMAL`、当前低温档 duty 34,000 ns、约 660～1,140 RPM；用户确认常温档可偏低，启动健康门采用 600～1500 RPM 宽容区间。
- 停止 bridge 6 秒后 M33 remoteproc 仍为 running；重启后约 1 秒报告 `FAIL_SAFE`，两帧有效温度后约 2 秒恢复 `CONTROLLED`。
- 恢复后持续检查 52 秒无重启、无协议错误；`focus_app.service` 与 bridge 均 active，Linux `stm32-timers`/`stm32-pwm` 仍未绑定。
- PB11 tach 已接入，稳定运行时 `current_faults=0`；8,000 ns M33 安全档约 5,760 RPM，Linux fallback 两次幂等执行约 5,880～6,120 RPM。
- M33 停止且 Linux timer/PWM unbound 的 30 秒窗口记录 7,009 个 PB11 上升沿，证明硬件默认自转路径持续存在。
- `edgefocus-m33-fan.service`、`edgefocus-fan-bridge.service`、`edgefocus-fan-healthcheck.service` 均为 enabled；厂商 loader 为 disabled。修复启动 job 自取消问题后，两次完整 systemd reboot 均自动进入 `CONTROLLED`，健康门成功，且每次只有一个 bridge 进程。

## 6. 当前保留风险与后续确认

1. 用户已明确豁免本板的示波器/逻辑分析仪截图；`waveform_verified=false` 必须保留，不能把豁免描述成已测。更换风扇、电源板、底板或 PWM 电路后豁免失效。
2. 已完成两次完整 systemd reboot 和一次物理断电重上电；三次均自动进入 `CONTROLLED`，物理冷启动 boot id 为 `567037614887`。
3. 独立 M33 watchdog 的时钟、窗口和喂狗策略仍未启用；当前依赖控制循环安全态与系统 watchdog。
4. Linux fallback 已手动故障注入；bridge start-limit 自动触发可在非展示窗口继续补测。
5. 物理断电后 RTC 回到 2000-01-01、Linux 初始时间回到 2024-02-28；本轮已重新校时并写 RTC，但若没有有效后备供电，后续断电仍会丢时钟，应由 App/网络在启动后同步。

## 7. 当前运行与后续维护顺序

1. 备份当前 ELF、DTB/overlay、M33 loader 脚本、Linux PWM 状态和回滚命令。
2. 正常运行保持厂商 loader disabled，M33 loader、bridge、健康门 enabled。
3. 更换相关硬件后撤销 waiver，重新做波形、PPR 和全档转速标定。
4. 任一启动健康门或 fallback 失败时，按 `m33-fan-service-operations.md` 恢复厂商 echo。

参考规格：[`../superpowers/specs/2026-07-15-m33-fan-control-design.md`](../superpowers/specs/2026-07-15-m33-fan-control-design.md) 与 [`../superpowers/specs/2026-07-15-m33-fan-autostart-and-calibration-design.md`](../superpowers/specs/2026-07-15-m33-fan-autostart-and-calibration-design.md)。

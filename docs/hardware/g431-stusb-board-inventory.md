# G431 与 STUSB4500 板级事实清单

更新时间：2026-07-15

适用板端：`root@192.168.10.174`

本文件只记录已经由网表、官方资料或板端只读检查确认的事实。机械参数在完成受限运动测试前保持 `UNVERIFIED`。

## 当前回滚基线

- `focus_app.service`：active
- `edgefocus-m33-fan.service`：active
- `edgefocus-fan-bridge.service`：active
- 正式程序：`/opt/edgefocus/focus_app`
- SHA-256：`eebd9f86e2a4d659f645a5a83e774b2f8ce8eccec8a17a14ac441fed42b2f25a`
- 已知 release 备份：`/opt/edgefocus/releases/20260714-233500`
- 正式 unit：`/etc/systemd/system/focus_app.service`
- unit 仍包含 `EDGEFOCUS_DISABLE_GIMBAL=1`
- 当前风扇：M33 remoteproc running，firmware 为 `EdgeFocus_FanController_CM33_NonSecure_stripped.elf`，bridge 状态为 `CONTROLLED/NORMAL`

注意：当前 unit 的 `ExecStart` 直接指向 `/opt/edgefocus/focus_app`，不是 `/opt/edgefocus/current/bin/focus_app`。部署与回滚必须以实际 unit 和 `deploy_to_board.py` 的备份逻辑为准。

## G431 链路

| MP257 资源 | 方向 | 板端只读事实 | 约束 |
|---|---|---|---|
| `/dev/ttySTM1` | 双向 | 字符设备存在，当前 `fuser` 无占用 | UART4，115200 8N1 |
| PD4 | 输出到 G431 | 当前为未申请 input | `BOOT0_CTRL_G431`；正常运行只能保持 0 |
| PD6 | 输入自 G431 | 当前为未申请 input | `G431_FAULT_OUT`，开漏低有效 |
| PD7 | 输出到 G431 | 当前为未申请 input | `G431_ENABLE_IN`，高有效 |

最新自研板网表：

`hardware_bringup/20260715_new_tel/Netlist_Schematic1_2026-07-15.tel`

网表确认 PD6 网络由 R75 10 kΩ 上拉到 `MCU_3.3V`。首次拉高 PD7 前仍必须在板端实测：

1. 健康释放时 PD6 稳定为高；
2. G431 Fault 时 PD6 可稳定拉低；
3. Fault 清除后 PD6 可恢复为高。

G431 的 NRST 没有连接到 MP257 可控 GPIO。PD4 只是 BOOT0，单独改变不会重启 G431；首版不得提供远程 Bootloader 或伪重启功能。

## STUSB4500 链路

| 资源 | 已验证值 |
|---|---|
| Linux I2C | `/dev/i2c-1` / I2C4 |
| SDA/SCL | PD10 / PD11，AF6 |
| 7 位地址 | `0x2A` |
| Device ID `0x2F` | `0x25` |
| 当前 `PORT_STATUS_1(0x0E)` | `0x00` |
| 当前 `TYPEC_MONITORING_STATUS_1(0x10)` | `0x04` |
| 当前物理状态 | 未接外部 VBUS，与寄存器事实一致 |

I2C4 同时连接 MP2762。产品实现只能读取 STUSB4500 `0x2A` 的明确寄存器，禁止扫描整条总线、写 STUSB4500 NVM 或触碰 MP2762。

普通轮询使用：

- `0x0E bit0 ATTACH`
- `0x10 bit3 VBUS_READY`

`0x0D/0x0F` 含 read-clear 事件位，不用于普通轮询。

## 未验证机械配置

以下字段在独立 bring-up 的受限运动阶段实测前均为 `UNVERIFIED`：

- yaw 正方向
- pitch 正方向
- 默认中心 yaw/pitch
- yaw 最小/最大软件限位
- pitch 最小/最大软件限位
- small nudge 角度
- 最大安全目标速度
- waypoint 上/下/左/右相对偏移

任何 `UNVERIFIED` 字段都必须阻止运动命令，不能回退到 0～180°、0～359°或示例程序中的占位范围。

## 本轮只读检查声明

截至本文更新时间，本轮只执行了：systemd、设备节点、GPIO line info、进程占用和 STUSB 静态寄存器读取。没有申请 GPIO 输出、没有写 PD4/PD7、没有向 UART4 发送命令、没有驱动电机、没有写 I2C。

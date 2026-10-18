# G431 云台独立 Bring-up 手册

目标板：`root@192.168.10.174`

本工具与正式 `focus_app` 共用 `platform/gimbal` 核心，但不由正式服务自动启动。

## 安全前置条件

1. `focus_app.service` 必须保持 `EDGEFOCUS_DISABLE_GIMBAL=1`。
2. `/dev/ttySTM1`、PD4、PD6、PD7 不得有其他 owner。
3. 首轮只运行 `probe`；未确认 PD6 三种电平行为前禁止 `align/move`。
4. `move` 前必须完成机械净空、限流供电、方向和软限位标定。
5. `config/gimbal-board.json` 的 `motion_verified=false` 时工具必须拒绝运动。

## 无运动检查

```sh
gimbal_bringup probe --config /opt/edgefocus/config/gimbal-board.json
```

`probe` 会以安全低电平申请 BOOT0/Enable，并只发送 `VER/PING/STAT/FAULT/ANGLE` 诊断命令。它不会拉高 Enable。

## 对齐和运动

以下命令只能在无运动检查通过、工程人员确认机械净空后执行：

```sh
gimbal_bringup align --allow-motion --confirm-board
gimbal_bringup move --axis yaw --delta-deg 2 --allow-motion --confirm-board
```

工具退出时始终 best-effort STOP，并最终拉低 Enable。禁止使用原始 UART 透传、BOOT0 high 或未验证的占位角度。

## 回滚

任何异常先停止工具并确认 PD7 为低，再检查：

```sh
gpioinfo | grep -E 'PD4|PD6|PD7'
fuser /dev/ttySTM1
systemctl is-active focus_app.service edgefocus-m33-fan.service edgefocus-fan-bridge.service
```

不要通过 BOOT0 模拟复位。当前自研板没有 MP257 可控的 G431 NRST。

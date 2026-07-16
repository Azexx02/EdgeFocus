# G431 单向 STOP 诊断设计

## 目的

在 MP257 UART4 持续 `rx=0`、无法取得 `OK,G431` 或 `BOOT_ALIGN,OK` 的情况下，仅用一次有界实验判断 `MP257 TX → G431 RX` 是否仍可执行命令。该实验只服务于硬件定位，不属于产品启动流程，也不能证明云台已经可供主程序使用。

## 入口和隔离

- 只扩展独立 `gimbal_bringup`，不修改、部署或启动正式 `focus_app`。
- 命令为 `oneway-stop`，必须同时提供 `--allow-motion --confirm-board --diagnose-no-rx`；缺少任一参数即拒绝。
- 正式 `enableAndAlign()`、PRECHECK、配置门槛和 `BOOT_ALIGN,OK` Ready 规则保持不变。
- 禁止 `PITCH`、`YAW`、`SET`、`TRACK`、`CIRCLE` 及任何目标角度命令。

## 有界执行序列

1. 独占申请 `/dev/ttySTM1`、PD4、PD6、PD7；原子设置 BOOT0=0、Enable=0。
2. 检查 PD6 为高；保持安全低态 500 ms。期间任一 GPIO/UART/Fault 错误立即退出。
3. 发送一次 `PING` 作为原始链路记录，但不以其回复作为本诊断的继续条件。
4. 拉高 Enable，触发 G431 自动对齐；等待 10 秒。等待期间每 25 ms 读取 PD6，并收集所有 UART 原始字节。
5. 若 PD6 始终为高，发送 `HOLD 1`；继续观察 500 ms。
6. 发送 `STOP`；保持 Enable 高 1 秒，供现场人员判断电机是否明显卸力，同时继续监测 PD6 和收集 UART。
7. 无论成功、失败、异常还是进程退出，都将 Enable 拉低并释放资源。

## 判定

- 现场确认 `STOP` 后明显卸力：说明 `MP257 TX → G431 RX` 和命令解析大概率正常，故障集中到 `G431 TX → R78 → MP257 PG15` 返回链。
- 自动对齐发生但 `STOP` 后不卸力：不能证明接收链正常，优先检查 `MP257 TX → R77 → G431 PC11` 或固件命令解析。
- PD6 变低：立即禁能，停止本轮，不自动重试。
- 工具只报告“序列已发送”和原始证据，不自动宣称控制成功；最终结论必须结合现场观察。

## 验证与回滚

- Host fake-I/O 测试断言命令顺序为 `PING → Enable=1 → HOLD 1 → STOP → Enable=0`。
- 测试断言 Fault、写失败和提前退出均以 Enable=0 结束。
- ARM 只构建和部署独立工具及测试，不替换正式主程序。
- 诊断结束后复核 PD4/PD6/PD7=`0/1/0`、ttySTM1 无 owner、正式服务仍 active 且带 `EDGEFOCUS_DISABLE_GIMBAL=1`。

## 一次性 PITCH 5 补充测试

- 用户已确认机械净空，并接受 `PITCH 5` 是绝对角度、实际位移可能超过 5°的风险。
- 新入口仅为独立工具的 `oneway-pitch5`，除原三重确认外还必须提供 `--confirm-absolute-angle`。
- 固定序列为 `PING → Enable/等待 10 秒 → HOLD 1 → PITCH 5 → 等待 1 秒 → STOP → 等待 1 秒 → Enable=0`。
- 禁止重复、禁止换轴、禁止发送其他目标角度；Fault 或任一 I/O 错误仍立即禁能。

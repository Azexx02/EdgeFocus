# M33 风扇分阶段 Bring-up 设计

- 日期：2026-07-15
- 状态：已获用户口头批准，待书面复核
- 范围：仅定位自研底板 PB10/TIM17_CH1N 初始化停点，不启用 PB11 tach、Linux bridge 或自启动

## 1. 背景与目标

修正 PB10 为 `TIM17_CH1N/AF7` 后，M33 固件不再导致整机复位，remoteproc 能进入 `running`，但 `/dev/ttyRPMSG0` 未出现。当前无法区分是 GPIO、TIM 时钟、TIM 配置、CH1N 启动还是 OpenAMP 初始化停住。

本轮目标是用一次可回滚测试取得“最后成功阶段”，而不是直接验证完整温控功能。

## 2. 方案选择

采用“通信先行、硬件分阶段”的方案：

1. M33 先完成 IPCC、OpenAMP 和 VIRT_UART 初始化。
2. `/dev/ttyRPMSG0` 建立后，M33 依次执行板级初始化阶段。
3. 每个阶段开始和结束时向 RPMsg 发送固定 ASCII 标记，并留出短暂刷新时间。
4. Linux 部署脚本持续读取标记；若 endpoint 消失、超时或 M33 停止，报告最后成功阶段并回滚。

不采用以下方案：

- 仅延长 `/dev/ttyRPMSG0` 等待：无法定位硬件停点。
- 直接重复完整初始化：只能得到相同的 remoteproc running/RPMsg missing 结果。

## 3. 阶段定义

| 阶段 | 操作 | 成功标记 |
|---|---|---|
| 0 | OpenAMP + VIRT_UART ready | `EFAN_STAGE:0:OK` |
| 1 | 申请 GPIOB10、TIM17 和所需 RCC/RIF 资源 | `EFAN_STAGE:1:OK` |
| 2 | 读取 TIM17 时钟并校验可整除为 1 MHz tick | `EFAN_STAGE:2:OK` |
| 3 | HAL TIM17 PWM 初始化与 master config，输出仍关闭 | `EFAN_STAGE:3:OK` |
| 4 | 配置 PB10 为 AF7、pull-down、low slew | `EFAN_STAGE:4:OK` |
| 5 | 配置 CH1 并启动 `HAL_TIMEx_PWMN_Start` | `EFAN_STAGE:5:OK` |
| 6 | 创建便携 `FanControllerApp` 并进入 100 ms tick | `EFAN_STAGE:6:OK` |

每个阶段开始前发送 `EFAN_STAGE:<n>:BEGIN`。无法返回的阶段就是首个高可信停点。

## 4. 代码边界

- `main.c` 只调整启动顺序：先通信，后调用分阶段板级初始化。
- `edgefocus_fan_board.cpp` 将现有初始化拆成有序阶段；生产控制算法和协议不变。
- bring-up 标记使用现有 VIRT_UART，不新增共享内存、持久化或主程序依赖。
- 诊断延时仅在显式 bring-up 编译开关启用时存在；正式固件默认关闭。
- PB11 tach 始终保持禁用。

## 5. Linux 部署与回滚

部署仍遵守：停止写入者 → unexport PWM → unbind `stm32-pwm` 子设备 → unbind `stm32-timers` 父设备 → 启动 M33。

验证窗口内：

- 等待 `/dev/ttyRPMSG0` 最多 10 秒。
- 连续读取阶段标记，直到阶段 6 或超时。
- 任一失败都先停止 M33，再按父到子恢复 Linux timer/PWM 驱动。
- 不重新启用 `fan-temp-control.service`；硬件默认风扇仍保持转动。
- 测试结束后恢复厂商默认 echo，除非阶段 6 成功且用户明确要求继续风扇验证。

## 6. 验收标准

诊断版通过条件：

1. A35 不复位，`focus_app.service` 不受影响。
2. `/dev/ttyRPMSG0` 在 10 秒内出现。
3. 能取得单调、无跳号的阶段标记。
4. 阶段 6 成功时 remoteproc、RPMsg 和 SSH 连续稳定至少 30 秒。
5. 失败时 Linux timer/PWM 重新绑定，默认 echo 可恢复。

本轮不把肉眼风扇转速、tach RPM、温度曲线或自适应增压作为通过条件；它们属于后续硬件功能验证。

## 7. 故障处理

- 卡在阶段 1：检查 RIF/RCC/GPIO/TIM 资源归属。
- 卡在阶段 2：检查 M33 可见的 RCC 时钟树和 TIM17 kernel clock。
- 卡在阶段 3：对照 ATK timer 示例的 HAL MSP/复位/时钟初始化。
- 卡在阶段 4：检查 GPIOB RIF、pinctrl 释放和自研底板电气设置。
- 卡在阶段 5：检查 TIM17_CH1N、CCER/MOE、极性和 HAL extended driver。
- 卡在阶段 6：检查 C++ 对象构造、回调和初始 duty 契约。

得到首个停点后只修对应层，不扩大到 tach、bridge 或主程序。

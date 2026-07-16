# M33 风扇控制实施计划

依据规格：`docs/superpowers/specs/2026-07-15-m33-fan-control-design.md`

## 实施原则

- 先只读盘点，再写硬件配置；未知 TIM/RIF/pinmux 不用猜测值填充。
- 先完成与 HAL 无关的可测试控制核心，再连接 Linux bridge 和 M33外设。
- Linux 与 M33任一时刻只能有一个 PB10/PB11 所有者。
- 首次上板只手动启动，不修改默认启动链；故障注入和回滚通过后才自动启动。
- 每个阶段保持独立可回退，不把未验证的 M33固件捆入正式 `focus_app` 发布。

## 阶段 1：板端与工具链事实盘点

只读采集并保存到 `docs/hardware/stm32mp257-m33-fan-inventory.md`：

1. 板端 CPU online、OpenSTLinux/kernel、device-tree model/compatible；
2. remoteproc 实例、M33 state/firmware、`st-m33firmware-load.service`；
3. thermal zone type/temp、PWM chip symlink/driver/period/duty/enable；
4. GPIO chip、PB10/PB11 line、`can@402d0000` status；
5. 当前 DTB 名称、可提取 DT、RIF 和 reserved-memory；
6. 编译机上的 STM32CubeMP2、OpenAMP 示例、arm-none-eabi 与 Yocto SDK；
7. 风扇规格中的 pulses-per-revolution 和电气失效默认态。

退出条件：PB10 TIM/通道/AF、PB11输入方式、M33启动模式和可复现构建路径都有证据。

## 阶段 2：纯控制核心与协议

新增：

```text
platform/fan/core/FanControlCore.h
platform/fan/core/FanControlCore.cpp
platform/fan/core/FanProtocol.h
platform/fan/core/FanProtocol.cpp
platform/fan/tests/test_fan_control.cpp
platform/fan/tests/test_fan_protocol.cpp
```

实现并测试：

- 普通五档曲线和 2°C 迟滞；
- 70°C/62°C 自适应增压状态机；
- 10 秒趋势窗口、5 秒升速、80°C 临界最高速；
- 温度序号、范围、3 秒 TTL 和两帧恢复；
- tach/RPM/堵转确认；
- BOOT_SAFE/CONTROLLED/FAIL_SAFE 和 fault flags；
- 固定宽度 RPMsg 消息、长度、版本和 CRC32。

退出条件：主机单测覆盖所有边界和故障转换；核心不依赖 Linux、STM32 HAL 或主程序状态。

## 阶段 3：Linux fan bridge

新增：

```text
platform/fan/bridge/FanBridge.cpp
platform/fan/bridge/RpmsgTransport.cpp
platform/fan/bridge/ThermalSource.cpp
platform/fan/bridge/fan-bridge.service
platform/fan/tests/test_fan_bridge.cpp
```

实现：

- 只按明确 type 查找 `cpu-thermal`，不回退任意温区；
- 1 Hz 温度上报、HELLO/版本握手、重连和 M33 boot id 变化处理；
- 原子写 `/run/edgefocus/fan-status.json`；
- journal 诊断和退出时 `SHUTDOWN_NOTICE`；
- 没有 RPMsg endpoint 时有限退避等待，不占用 PWM。

退出条件：使用伪 RPMsg transport 完成断线、重启、乱序和状态文件测试。

## 阶段 4：M33 固件与板级配置

在阶段 1 确认 CubeMP2 结构后建立：

```text
firmware/m33/fan_controller/
```

实现顺序：

1. 从匹配 OpenSTLinux 的官方 OpenAMP 示例建立最小可复现工程；
2. 引入共享控制核心/协议的 freestanding 兼容部分；
3. 配置 PB10 TIM PWM，启动第一条语句进入 8,000 ns BOOT_SAFE；
4. 配置 PB11输入捕获或 EXTI；
5. 非阻塞 OpenAMP endpoint；
6. WWDG，只在控制/PWM/协议任务健康时刷新；
7. board-specific DTS/RIF/reserved-memory 补丁和构建说明。

退出条件：固件可重复构建；无 Linux endpoint 时仍持续安全 PWM；资源表可被 remoteproc 正确解析。

## 阶段 5：分阶段上板

1. 保存当前 DTB、服务、PWM 状态和 Linux fan 原型回滚包；
2. 工程人员在场，手动停止 Linux fan service；
3. 切换经过验证的 M33外设归属并重启；
4. 手动加载 M33固件，不设置 auto-start；
5. 示波器验证 25 kHz 和每档占空时间；
6. 启动 bridge，逐项做温度模拟、70/62增压、TTL和RPMsg断线；
7. 验证 tach、堵转、M33 reset、A35 reboot；
8. 回滚完整演练。

退出条件：所有安全测试通过，且 `focus_app`/Demo 启停不改变风扇。

## 阶段 6：稳定化和产品接入

- 24 小时 NPU 压力测试；
- 启用 M33自动加载和 `edgefocus-fan-bridge.service`；
- 将 M33 firmware、DTB/overlay、bridge 和回滚信息纳入独立原子发布；
- 更新 `docs/architecture/current-architecture.md`；
- 可选：`HealthMonitor` 只读 fan-status，不建立控制依赖；
- 云台/电源板另立规格，不在本计划顺带实现。

## 首轮允许修改范围

- 允许：`platform/fan/`、`firmware/m33/fan_controller/`、相关测试、硬件文档和独立部署辅助。
- 暂不允许：`SessionOrchestrator`、`DistractionPipelineFeature`、Demo、App/手环协议、云台、电源板和正式发布启动链。

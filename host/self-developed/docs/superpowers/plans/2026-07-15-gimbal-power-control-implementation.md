# 云台、外部供电与 Home/Go 实施计划

依据规格：`docs/superpowers/specs/2026-07-15-gimbal-power-heterogeneous-control-design.md`

## 实施目标

在不扩大 M33 职责、不影响现有风扇开机链和正式视觉/Fusion 真值的前提下，完成：

1. A35/Linux 对 UART4、PD4/PD6/PD7 的唯一安全所有权；
2. 独立云台 bring-up、无运动检查、对齐和受限小角度验证；
3. STUSB4500 只读外部供电检测及 Home/Go 策略；
4. 正式 PRECHECK 的有界自动搜索、故障恢复提示和 App 手动控制；
5. 删除全部电量与旧供电兼容字段，形成新版 App 契约；
6. 通过 host、交叉编译、板端故障注入和原子回滚后，才打开正式云台 feature flag。

## 不可跨越的门槛

- `focus_app.service` 在阶段 1～7 继续保留 `EDGEFOCUS_DISABLE_GIMBAL=1`。
- 第一次写 PD7 或发送运动命令只能由独立 bring-up 工具执行；主程序不能抢跑。
- PD4 始终为 BOOT0=0，首版不提供远程 Bootloader、复位或透传命令。
- 未实测 PD6 健康高、Fault 拉低、释放恢复之前，禁止 PD7=1。
- 未完成机械方向和安全范围标定之前，只允许单轴、低速、小角度动作。
- `/dev/i2c-1` 只访问 `0x2A` 的只读寄存器 `0x0E/0x10/0x2F`，不扫描总线、不写 STUSB4500/MP2762/NVM。
- GimbalHardwareService、bring-up 工具和旧 GimbalMonitor 任一时刻只能有一个 UART/GPIO 所有者。
- Demo 不伪造供电或云台状态；M33 风扇服务、PB10/PB11 和 RPMsg 链不改动。

## 阶段 0：冻结基线和回滚点

**修改文件**

- `docs/hardware/g431-stusb-board-inventory.md`（新增）
- `task_plan.md`
- `progress.md`

**步骤**

1. 记录当前 git 状态、当前板端 release、`focus_app.service` 环境和风扇服务状态。
2. 只读记录 `/dev/ttySTM1`、PD4/PD6/PD7、`/dev/i2c-1`、STUSB `0x2A` 和当前寄存器值。
3. 保存当前 release 与 systemd unit 的回滚路径，确认旧版本回滚后仍禁用云台。
4. 把网表、G431 协议、STUSB 地址与寄存器依据写入 inventory，未知机械参数明确标为 `UNVERIFIED`。

**验证**

```powershell
git -c safe.directory=C:/Users/Aze/Desktop/self_developed/code/focus_app status --short
ssh root@192.168.10.174 "systemctl is-active focus_app edgefocus-m33-fan edgefocus-fan-bridge; systemctl show focus_app -p Environment"
```

**退出条件**：存在可恢复的板端基线；本阶段没有 GPIO 输出、UART 写或 I2C 写。

## 阶段 1：建立可测试的云台协议与状态核心

**新增文件**

- `platform/gimbal/GimbalTypes.h`
- `platform/gimbal/GimbalProtocol.h`
- `platform/gimbal/GimbalProtocol.cpp`
- `platform/gimbal/GimbalStateMachine.h`
- `platform/gimbal/GimbalStateMachine.cpp`
- `platform/gimbal/IGimbalIo.h`
- `platform/gimbal/tests/test_gimbal_protocol.cpp`
- `platform/gimbal/tests/test_gimbal_state_machine.cpp`

**修改文件**

- `CMakeLists.txt`
- `tests/CMakeLists.txt`

**步骤（测试先行）**

1. 先写失败测试：CRLF/LF、半行、超长行、同步响应与异步 `BOOT_ALIGN` 交错、`ERR/STAT/FAULT/ANGLE` 类型化解析。
2. 实现协议 parser 和事件类型，不让上层读取原始 UART 文本。
3. 先写状态机测试：所有合法/非法转移、3 秒对齐超时、PD6 Fault、STOP 成功/超时、Fault 锁存和 recovery probe。
4. 实现纯状态机；I/O 只通过 `IGimbalIo` 注入，host 测试不得访问真实设备。
5. 固化恢复规则：Fault 每 5 秒被动探测；PD6 与 `PING/FAULT/STAT` 连续 3 次健康后只进入 `Disabled + recoverable`。
6. 固化命令白名单和角度夹取；不实现 `!`、原始透传、BOOT0 或复位。

**验证**

```powershell
cmake --build build --target test_gimbal_protocol test_gimbal_state_machine
ctest --test-dir build -R "gimbal_(protocol|state_machine)" --output-on-failure
```

**退出条件**：协议与状态机在无 Linux/GPIO/UART 依赖下通过全部 host 测试。

## 阶段 2：Linux I/O 和进程级 GimbalHardwareService

**新增文件**

- `platform/gimbal/LinuxGimbalIo.h`
- `platform/gimbal/LinuxGimbalIo.cpp`
- `platform/gimbal/GimbalHardwareService.h`
- `platform/gimbal/GimbalHardwareService.cpp`
- `platform/gimbal/tests/test_gimbal_hardware_service.cpp`

**修改文件**

- `device/GpioHelper.h`
- `device/GpioHelper.cpp`
- `CMakeLists.txt`
- `tests/CMakeLists.txt`

**步骤（测试先行）**

1. 为 GPIO helper 增加输入读取、错误返回和 RAII 关闭；保持 character-device ioctl，不引入第二套 sysfs GPIO。
2. `LinuxGimbalIo` 只负责 `/dev/ttySTM1` 115200 8N1、PD4/PD6/PD7 申请和安全初值；构造/打开失败必须保证 PD7=0。
3. `GimbalHardwareService` 作为进程级 `Module` 和唯一所有者，使用单 reader 路由异步事件与命令 promise。
4. 增加串行 command queue、generation、超时和 snapshot；对外只暴露 `enableAndAlign/stopAndDisable/moveTo/hold/status/probeRecovery`。
5. Fault 路径先撤销 PD7，再记录锁存原因；正常关闭执行 best-effort STOP，最终保证 PD7=0。
6. 用 fake I/O 测试：错序响应、迟到 ACK、读线程退出、串口断链、PD6 下降、重复 shutdown 和恢复探测绝不运动。

**验证**

```powershell
cmake --build build --target test_gimbal_hardware_service
ctest --test-dir build -R gimbal_hardware_service --output-on-failure
```

**退出条件**：Linux 后端可编译；所有失败路径都能证明最终 PD7 低且无后台自动重试。

## 阶段 3：独立 bring-up 工具与无运动上板

**新增文件**

- `tools/gimbal_bringup.cpp`
- `config/gimbal-board.json`
- `docs/hardware/g431-bringup-runbook.md`

**修改文件**

- `CMakeLists.txt`
- `deploy_to_board.py`（只增加独立工具打包，不接入正式服务）

**工具接口**

```text
gimbal_bringup probe
gimbal_bringup align --allow-motion --confirm-board
gimbal_bringup stop
gimbal_bringup move --axis yaw|pitch --delta-deg N --allow-motion --confirm-board
gimbal_bringup recovery-watch
```

**步骤**

1. 默认命令只有 `probe`，只读 PD6 和 UART VER/PING/STAT/FAULT/ANGLE；工具启动即确保 BOOT0=0、Enable=0。
2. motion 子命令要求两个显式确认参数，并拒绝未验证或缺失的板级配置。
3. 工具启动时检测 tty/GPIO 占用；发现 `focus_app` 云台入口或其他进程占用即退出。
4. 交叉编译并原子上传工具，不替换 `focus_app` binary。
5. 板端只执行无运动 probe：确认 PD4=0、PD7=0、PD6 上拉、电平读取和真实 UART 返回格式。
6. 保存原始日志到 `docs/hardware/g431-bringup-results-2026-07-15.md`。

**退出条件**：无运动 probe 通过；PD6 上拉行为与真实 UART 格式有记录；正式服务仍禁用云台。

## 阶段 4：受限对齐、Fault、STOP 和机械标定

**修改文件**

- `config/gimbal-board.json`
- `docs/hardware/g431-bringup-results-2026-07-15.md`

**执行顺序**

1. 确认机械净空、限流供电和现场人员后，执行一次 Enable 低 100 ms→高。
2. 捕获完整 `BOOT_ALIGN,START/OK`；3 秒无 OK 则自动 PD7=0，停止本阶段。
3. 验证 `STOP -> OK,STOP -> PD7=0`，再验证重复对齐。
4. 通过撤销 Enable/固件可控 Fault 验证 PD6 拉低、A35 禁能和 5 秒低频恢复探测；探测不得自动使能。
5. 单轴小角度确认 yaw/pitch 符号，再逐步测出默认中心、软限位、small nudge 和最大安全速度。
6. 双轴动作仅在两个单轴方向均确认后执行。
7. 把 measured 值写入配置；未测值保持阻止运动的 `UNVERIFIED`，不得写猜测默认值。

**退出条件**：对齐、STOP、Fault、恢复探测和安全范围全部有实测记录；独立工具可重复安全关闭。

## 阶段 5：STUSB4500 与 WorkloadProfilePolicy

**新增文件**

- `platform/power/PowerTypes.h`
- `platform/power/Stusb4500Status.h`
- `platform/power/Stusb4500Status.cpp`
- `platform/power/IPowerSourceIo.h`
- `platform/power/LinuxI2cPowerSourceIo.h`
- `platform/power/LinuxI2cPowerSourceIo.cpp`
- `platform/power/PowerSourceService.h`
- `platform/power/PowerSourceService.cpp`
- `platform/power/WorkloadProfilePolicy.h`
- `platform/power/WorkloadProfilePolicy.cpp`
- `platform/power/tests/test_stusb4500_status.cpp`
- `platform/power/tests/test_workload_profile_policy.cpp`

**修改文件**

- `CMakeLists.txt`
- `tests/CMakeLists.txt`

**步骤（测试先行）**

1. 测试并实现四项真值表：`00=ABSENT`、`11=PRESENT`、`10/01=UNKNOWN`。
2. 测试并实现 250 ms 采样、连续 3 次确认、连续 raw UNKNOWN 2 秒、3 次读失败和 2 秒无成功样本。
3. Linux I2C 后端固定 `/dev/i2c-1`、地址 `0x2A`，只允许读取 `0x0E/0x10/0x2F`。
4. 测试并实现 power epoch：只有相反的已确认事实递增；跨 UNKNOWN 按最后已确认事实判断。
5. 测试并实现 Home/Go：外电 Home、无外电 Go、UNKNOWN 无覆盖时 Go；手动覆盖只活到下一次确认插拔且不持久化。
6. `PowerSourceService` 为 optional 进程服务；I2C 不可用不能阻止 `focus_app` 启动。

**验证**

```powershell
cmake --build build --target test_stusb4500_status test_workload_profile_policy
ctest --test-dir build -R "stusb4500|workload_profile" --output-on-failure
```

**退出条件**：全部策略可用 fake I2C/clock 重放；没有任何总线扫描和写 API。

## 阶段 6：删除电量字段并接入新版 App 供电契约

**修改文件**

- `app/SystemStateStore.h`
- `device/DeviceManager.cpp`
- `device/DeviceManager.h`
- `network/AppCommandService.cpp`
- `network/AppCommandService.h`
- `network/BleGattServer.cpp`
- `network/BleGattServer.h`
- `network/DemoHttpServer.cpp`
- `network/DemoHttpServer.h`
- `ui/StatusPage.cpp` 及仍引用电量的旧 UI 编译单元
- `tests/test_device_manager.cpp`
- `tests/test_ble_gatt.cpp`
- `tests/test_demo_http.cpp`
- `tests/test_integration.cpp`
- `docs/integration/app-band-protocol-v2.1.md`
- `docs/integration/app-update-handoff-2026-07-15.md`（新增）

**步骤（测试先行）**

1. 先把协议测试改为断言响应不存在 `battery_*` 和旧 `power_source`。
2. 从 `DeviceStatus/Preferences` 删除电量、充电、旧供电来源和持久化 power profile；删除 `normalizePowerStatus()` 对用户选择的反复覆盖。
3. App 状态只返回：`external_power_state`、nullable `external_power_present`、`external_power_sample_age_ms`、`power_profile`、`power_profile_source`、`manual_override_active`。
4. 新增手动 Home/Go 命令，直接调用 `WorkloadProfilePolicy`；不写 SQLite，插拔事件自动清覆盖。
5. 删除 Demo 的 `set_power_state`、电量回退 67%、旧 UI 电量显示和任何 battery 兼容别名。
6. 协议文档标记为破坏性同步升级，明确旧 App 不受支持，并生成 App 工作区交接文档。
7. 全仓搜索确保产品代码无 `battery_percent/battery_voltage/battery_charging/power_source` 残留；历史规格说明除外。

**退出条件**：host API 测试通过；新版响应只有唯一供电命名；Demo 无供电模拟入口。

## 阶段 7：Home/Go 接入视觉调度并量化回归

**新增文件**

- `tests/data/home_go_regression_manifest.json`
- `tests/test_workload_inference_profile.cpp`
- `scripts/run_home_go_regression.py`

**修改文件**

- `vision/DistractionPipelineFeature.h`
- `vision/DistractionPipelineFeature.cpp`
- `main.cpp`
- `tests/CMakeLists.txt`

**步骤**

1. 将只读 `WorkloadProfilePolicy` 注入正式 `DistractionPipelineFeature`；不经过 `SystemStateStore`。
2. PRECHECK 固定约 4 FPS，FOCUSING Home 约 4 FPS、Go 约 3 FPS，PAUSED 保持既有约 1 FPS。
3. 通过丢弃过早帧处理最新帧，不降低 CameraPipeline/浏览器预览帧率，不堆积队列。
4. 冻结认可视频的路径、SHA-256 和场景覆盖；同一手机事实分别运行 Home/Go。
5. 输出逐 250 ms 时间桶对齐报告：Presence、Prone、综合等级、转换次数、状态延迟、p99/最大推理间隔和 FPS。
6. 不满足批准指标时只调整 Go 采样频率，不修改模型、2000 ms 窗口或阈值。

**退出条件**：Go ≥2.8 FPS、最大间隔 <500 ms、无连续性清空，并满足规格中的 95% 一致率和延迟指标。

## 阶段 8：GimbalSessionFeature 与 PRECHECK

**新增/替换文件**

- `device/GimbalSessionFeature.h`
- `device/GimbalSessionFeature.cpp`
- `device/GimbalPrecheckController.h`
- `device/GimbalPrecheckController.cpp`
- `tests/test_gimbal_precheck.cpp`

**修改文件**

- `session/SessionDomain.h`
- `session/SessionSnapshotPublisher.h`
- `session/SessionSnapshotPublisher.cpp`
- `session/SessionRuntime.cpp`（仅在确有生命周期缺口时）
- `tests/test_session_runtime.cpp`
- `tests/CMakeLists.txt`

**步骤（测试先行）**

1. 用 fake gimbal、fake clock 和 fake VisualSnapshot 测试：当前位置已有可用人物时零运动。
2. 实现固定 9 waypoint 序列，最后一个为中心；每步 2 秒、观察 1 秒、第 27 秒停止排队、第 30 秒硬截止。
3. 人物连续至少 2 个 fresh 观测且不少于 400 ms 后 STOP/HOLD，记录当前角度。
4. 实现 search generation；skip 时停止接收目标、清队列、STOP/HOLD 成功或超时禁能后才授予手动租约。
5. 旧 generation 的 ACK、到达和视觉事件全部丢弃。
6. `GimbalSessionFeature` 作为 optional feature：PRECHECK 失败进入 degraded，正式相机/视觉/会话继续。
7. FOCUSING/PAUSED 首版只 HOLD；ENDING 与异常 stop 执行 `stopAndDisable()`。
8. 把新 GimbalStatus 投影到 `SessionSnapshotPublisher`，不扩展旧 DeviceStatus 云台语义。

**退出条件**：全部 PRECHECK 时间、取消、租约和降级测试通过；无持续人脸跟踪代码。

## 阶段 9：App 云台命令与正式会话接线

**修改文件**

- `network/AppCommandService.h`
- `network/AppCommandService.cpp`
- `network/BleGattServer.h`
- `network/BleGattServer.cpp`
- `main.cpp`
- `tests/test_ble_gatt.cpp`
- `tests/test_integration.cpp`
- `docs/integration/app-band-protocol-v2.1.md`
- `docs/integration/app-update-handoff-2026-07-15.md`

**步骤**

1. App 只开放 `gimbal_nudge/gimbal_center/gimbal_stop/gimbal_realign/precheck_skip_auto_adjust/precheck_confirm_position`。
2. 每条命令校验 session id、PRECHECK phase、控制租约、去重序列、方向和 small step；拒绝原始角度/UART/BOOT0。
3. `gimbal_realign()` 只接受 `Disabled + recoverable`，一次调用只尝试一次完整对齐；成功直接进入手动阶段，不重启自动搜索。
4. `main.cpp` 注册 `PowerSourceService` 和 `GimbalHardwareService` 进程模块；SessionRuntime 在视觉 feature 之后添加 optional `GimbalSessionFeature`。
5. 保留单独 `EDGEFOCUS_GIMBAL_FEATURE` 灰度开关；关闭时服务保持 PD7 低且会话不创建 feature。
6. 移除 AppCommandService 对旧 GimbalMonitor 的异步原始命令线程，改为类型化 session feature 命令入口。
7. 页面只读展示新状态，不添加网页云台控制。

**退出条件**：host BLE/集成测试覆盖允许与拒绝路径；关闭 feature flag 时现有正式会话行为不变。

## 阶段 10：主机全回归与 ARM 全目标编译

**验证顺序**

```powershell
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
python deploy_to_board.py --help
git -c safe.directory=C:/Users/Aze/Desktop/self_developed/code/focus_app diff --check
```

随后使用现有交叉编译入口构建所有 ARM 目标，至少检查：

- `focus_app`
- `gimbal_bringup`
- 所有新 gimbal/power tests
- `test_session_runtime`
- `test_ble_gatt`
- `test_integration`
- `test_distraction_*`

**退出条件**：host 全回归、ARM 全目标和安装包清单通过；无硬件 feature 自动启用。

## 阶段 11：板端供电验证与主程序灰度

**步骤**

1. 先只部署 PowerSourceService，云台 feature 仍关闭。
2. 物理验证无外电、插电、反复插拔、raw UNKNOWN/I2C 错误和 Home/Go 手动覆盖。
3. 验证插拔清覆盖、UNKNOWN 不清覆盖、重启不恢复覆盖。
4. 跑冻结 Home/Go 视频回归并保存报告。
5. 启用进程级 GimbalHardwareService，但 session feature 仍关闭；确认 PD7 始终低、Fault 恢复探测无运动。
6. 打开 session gimbal 灰度开关，执行一次正式 PRECHECK：已有用户零移动、四向搜索、见人停、无人回中、skip 和手动 nudge。
7. 注入 UART timeout、PD6 Fault、NPU 不可用和用户取消，确认均降级固定相机且会话不失败。
8. 每轮验证后检查风扇 M33/bridge、正式视觉、BLE App/手环和 Demo 未回归。

**退出条件**：全部板端验收通过，才从生产 unit 移除 `EDGEFOCUS_DISABLE_GIMBAL=1`；否则回滚旧 release 并继续禁用。

## 阶段 12：架构、部署与 App 交接

**修改文件**

- `docs/architecture/current-architecture.md`
- `AGENTS.md`
- `docs/hardware/g431-stusb-board-inventory.md`
- `docs/hardware/g431-bringup-runbook.md`
- `docs/integration/app-band-protocol-v2.1.md`
- `docs/integration/app-update-handoff-2026-07-15.md`
- `deploy_to_board.py`

**步骤**

1. 把 A35/NPU、M33 风扇、G431 电机执行和 STUSB 检测的真实实现更新到当前架构图。
2. 明确进程级 GimbalHardwareService/PowerSourceService 和会话级 GimbalSessionFeature 的唯一所有权。
3. 记录已测机械参数、回滚命令、feature flag 和故障恢复方式。
4. 给 App 工作区交付新字段、命令、phase 限制、错误码和同步发布要求；不描述 Demo 模式。
5. 部署脚本增加新 binary/config 的校验，并保持原子 release、健康检查与失败回滚。

**退出条件**：其他 agent 只读 `AGENTS.md`、当前架构、协议和 runbook 即可安全接手；文档不把未测项目写成已完成。

## 最终完成定义

- 独立 bring-up 与正式主程序复用同一云台核心，没有第二套串口状态机。
- Fault 能低频发现恢复，但不会后台使能或自动运动。
- PRECHECK 搜索有明确数量、时间、generation 和租约边界。
- 供电只来自 STUSB4500 静态只读事实；Home/Go 只有一个策略写者。
- 产品与协议中不存在电量、充电、旧 `power_source` 或旧 App 兼容分支。
- Go 视觉稳定性达到量化指标；安全检测、BLE 和 M33 风扇不降频。
- 云台失败只降级固定相机，不阻塞正式会话。
- 主机、ARM、板端、故障注入、部署回滚和文档均通过。

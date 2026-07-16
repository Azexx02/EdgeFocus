# 主程序云台融合设计

- 日期：2026-07-16
- 目标仓库：`code/focus_app`
- 已验证来源：`_workspace/gimbal_standalone`，提交 `586e511`
- 部署目标：STM32MP257 `192.168.10.174`，G431 UART4 `/dev/ttySTM3`

## 1. 目标

把独立 C++ 云台程序已经在真实板端贯通的“搜索、候选居中、构图确认、持续追踪和安全停机”能力融合到 `focus_app`，并由正式会话生命周期统一所有权。

融合后的产品语义为：

1. 云台只在正式会话的 PRECHECK 与 FOCUSING 阶段工作。
2. PRECHECK 可以经过当前画面观察、常规搜索和全俯仰恢复搜索寻找用户。
3. 找到候选人后先完成受限居中，再进行三帧构图确认。
4. 成功后进入 HOLD 并交出 App 手动微调租约。
5. 只有 App 的“确认就绪”提交成功后才进入 FOCUSING 和持续追踪。
6. PRECHECK 视觉只服务预检，不产生正式分神 episode、提醒或会话统计。

自动搜索绝对上限为 75 秒。超时或搜索节点耗尽时回参考中位、HOLD 并进入手动调节，不把“未找到用户”升级为 required feature 失败。

## 2. 非目标

- 不启动独立云台子进程。
- 不创建第二个摄像头或第二个 Pose/NPU 实例。
- 不改变 Presence、Prone、Phone Fusion 和提醒判定语义。
- 不改变 App 已有 PRECHECK 手动命令、`session_id`、递增 `sequence` 和 manual lease 合同。
- 不把 Demo 模式接入云台。
- 不在本次融合中重构无关的会话、蓝牙、手环、风扇或供电模块。

## 3. 方案选择

采用“原生补齐主程序现有云台模块”：

- `CameraPipeline` 继续是现场相机唯一所有者。
- `DistractionPipelineFeature` 继续是正式 Pose/NPU 唯一所有者。
- `GimbalSessionFeature` 只读取 `SessionSnapshotPublisher` 中的最新 `VisualSnapshot`。
- `GimbalHardwareService` 继续独占 UART4 和 GPIO/Fault 安全控制。
- 已验证独立程序的通用搜索、构图和到位验证算法移植为主程序原生类型，不并存第二套硬件状态机。

不采用静态链接整套独立程序，因为两边已有不同的协议、状态和生命周期类型；不采用子进程，因为会分裂 UART、摄像头、会话和故障恢复所有权。

## 4. 会话状态与数据流

```text
SessionOrchestrator.start()
  -> SessionRuntime.prepare()
  -> GimbalSessionFeature.prepare()
  -> GimbalHardwareService.enableAndAlign()
  -> PRECHECK
       current observation
       -> primary sweep
       -> full-pitch recovery sweep
       -> candidate freeze
       -> candidate centering (<= 3 s)
       -> second freeze
       -> 3-frame framing confirmation
       -> HOLD + manual lease
  -> App confirm_ready
  -> FOCUSING
       -> GimbalVisualTracker continuous tracking
  -> PAUSED
       -> HOLD, no tracking command
  -> ENDING
       -> best-effort reference return
       -> STOP ACK
       -> motor/hold/fault verification
       -> disable
```

`GimbalSessionFeature` 的 50 ms worker 可以高频轮询，但每个视觉帧只按递增 `frame_id` 消费一次。视觉证据仍以主程序约 4 FPS 的节拍更新，云台不得重复消费同一帧产生多次运动。

## 5. 组件改动

### 5.1 `GimbalHardwareService` 与硬件接口

在 `IGimbalSessionHardware` 增加满足业务层验证所需的原子操作，具体命名可按现有代码风格调整：

- 刷新并返回最新 ANGLE，单次超时时允许一次重试。
- 冻结当前位置：读取稳定实际角度，再 SET 到该实际角度并复核。
- 移动并验证到位：SET ACK 后轮询 ANGLE；两轴误差不超过 1.5°才算完成。
- 稳定但存在 2°到 3°静态误差时，最多进行两次有界闭环补偿。

SET ACK 只表示命令已接收，不再等同于机械已经到位。所有操作继续经过现有递归操作锁，不能与后台 Fault/健康查询并发写 UART。

正常停止按 `STOP -> OK,STOP -> STAT/FAULT/ANGLE -> disable` 执行。故障或取消时安全停机优先于回中；只有硬件健康时才执行结束回参考中位。

### 5.2 搜索计划

常规搜索保持已验证范围：yaw ±45°、pitch ±20°。搜索不是在每个点静止等待，而是在运动过程中持续消费新 Pose 快照。

常规范围未取得合格目标后，执行单次连续全俯仰恢复：

```text
0, +34, +68, +102, +136, +170,
-170, -136, -102, -68, -34, 0
```

这些是相对 ARM 对齐参考的 pitch 节点；跨越 `+170 -> -170` 使用环形最短方向，从而形成一圈连续覆盖。yaw 在恢复阶段保持参考中位。75 秒绝对上限对常规与恢复阶段共同计时，到期立即终止自动搜索。

硬限制与业务搜索范围分开：硬件 pitch 窗口允许已实测的完整环形恢复，常规搜索仍限制在 ±20°。yaw 硬限制为 ±45°。

### 5.3 候选关联、居中和构图

主程序只消费视觉链已经选定的座位主体。发现候选后保存主体框，并用 IoU、中心位移和面积比例关联后续帧，防止居中期间切换到另一个人。

候选居中：

- 最长 3 秒。
- 使用与正式追踪相同的映射与方向配置。
- 肩部关键点 5/6 的中点优先；头部关键点和主体框依次回退。
- 单步最大 1.5°，命令周期不小于 100 ms。
- 连续两帧进入死区才视为居中完成。
- 连续两帧关联失败则回到原扫描节点继续搜索。
- 到时仍可见则允许以当前部分居中位置进入构图确认。

居中后再次冻结实际位置，再执行原有三帧独立构图窗口。阈值沿用独立程序已验证实现；允许单帧缺失计 0 分，但只有完整三周期窗口才能确认。构图失败时对当前位置建立冷却区并恢复扫描。

### 5.4 PRECHECK 与 App 手动租约

目标确认成功后：

- 云台进入 HOLD。
- `subject_found=true`。
- PRECHECK 状态进入 Manual。
- `manual_lease=true`。
- App 仍可发送 left/right/up/down、center、stop 和 confirm 命令。

自动搜索超时或无目标完成后：

- 健康时先验证回到参考中位，再 HOLD。
- `subject_found=false`。
- 同样进入 Manual 并交出租约。

App `confirm_ready` 仍由 `SessionOrchestrator` 提交正式会话。进入 FOCUSING 时撤销 manual lease、重置 tracker 连续性并开始追踪；PAUSED 只 HOLD；Resume 重置 tracker 后恢复追踪。

### 5.5 配置与启用

正式 `config/gimbal-board.json` 改为已验证配置并设置 `motion_verified=true`。配置继续使用 UART-only，加入常规搜索范围、全俯仰恢复 extent、75 秒绝对上限、居中与构图参数。

`EDGEFOCUS_DISABLE_GIMBAL=1` 仅作为紧急维修开关。未设置时主程序按配置完整性与运行时健康自动启用；配置缺失或不合法时拒绝创建云台 feature，但主程序其他功能正常启动。

## 6. 故障与恢复

下列任一条件立即停止 PRECHECK 运动和 FOCUSING 追踪：

- UART 连续无响应或命令返回错误。
- PD6 Fault 有效。
- ARM/BOOT_ALIGN 超时或失败。
- ANGLE 无法读取、稳定性不足或到位验证失败。
- 目标命令超出已验证硬限制。
- STOP 后无法确认安全状态。

云台 feature 标记 degraded，但视觉、Fusion、App、手环、存储与正式会话继续运行。`GimbalHardwareService` 维持安全禁用并按现有 5 秒低频策略探测；连续三个健康样本后才允许重新 ARM。

恢复时：

- 仍在 PRECHECK：从当前画面观察重新开始搜索，重新计算 75 秒窗口。
- 已在 FOCUSING：先 HOLD，重置 tracker，再恢复追踪。
- PAUSED：只恢复 HOLD。
- ENDING/IDLE：保持禁用，不重新启动运动。

## 7. 对外状态

不新增跨设备协议字段。沿用 `GimbalSnapshot` 的 `state`、`health`、`waypoint_index`、`subject_found`、`manual_lease`、`tracking` 与 `detail`。

`detail` 使用稳定枚举式文本表达当前阶段，例如：

- `observing_current_view`
- `primary_search`
- `recovery_search`
- `candidate_centering`
- `framing_confirmation`
- `subject_found_manual`
- `search_timeout_manual`
- `recovering`

展示页和 App 不根据这些文本重建业务状态，主状态仍以 typed fields 为准。

## 8. 验证与验收

### 8.1 主机自动测试

- 搜索计划节点、环形连续性和 75 秒绝对截止。
- ANGLE 重试、冻结稳定性、静态误差补偿和到位复核。
- 当前画面直接发现目标时不先大范围运动。
- 候选居中成功、部分居中、连续两帧丢失恢复扫描。
- 三帧构图确认、单帧缺失和构图拒绝冷却。
- 目标成功与无目标超时均进入正确 manual lease。
- FOCUSING/PAUSED/Resume/ENDING 的追踪和 HOLD 行为。
- Fault 后 feature 降级以及连续健康恢复。
- PRECHECK 不建立正式分神 episode、提醒或统计。
- App 手动命令的 session、sequence 和 lease 门禁保持有效。

### 8.2 ARM 构建与板端分阶段验证

1. 构建全部 ARM 目标和测试。
2. 部署前备份当前 release。
3. IDLE 启动检查：服务 active，未开始会话时云台不运动。
4. PRECHECK：真实相机、真实 NPU 自动搜索并完成候选居中和构图。
5. 验证 HOLD、手动微调和 App 确认门禁。
6. FOCUSING：确认连续 TRACK 成功并能在短暂丢失后恢复。
7. PAUSED/Resume：暂停不追踪，恢复后重新追踪。
8. ENDING：健康时回参考中位，STOP ACK、电机关闭、Fault 清零。
9. 检查展示页、App、手环、Fusion、提醒、存储和 Demo 不受回归影响。

云台板端链路的最低通过证据包括：目标获取、至少一次有效持续 TRACK、只执行一次正常 ARM、`STOP ACK`、`motion_off_verified`、`fault_clear`，且 `focus_app.service` 最终保持 active。

部署健康检查失败时使用现有发布脚本回滚上一 release。

## 9. 完成标准

- 正式主程序不依赖 `EDGEFOCUS_DISABLE_GIMBAL=1` 才能稳定运行。
- PRECHECK 在 75 秒内按批准流程自动搜索；找到目标后真正朝向用户并停在手动确认状态。
- App 确认前不进入正式追踪或分神记录。
- 确认后云台持续跟踪，暂停和结束行为正确。
- 云台任何失败只局部降级，且能安全停机和低频恢复。
- 主机测试、ARM 构建、板端验证和现有主程序关键回归全部通过。

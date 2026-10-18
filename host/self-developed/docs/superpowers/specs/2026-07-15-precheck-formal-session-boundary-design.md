# PRECHECK 与正式会话边界设计

- 日期：2026-07-15
- 状态：用户已批准设计语义，待书面复核
- 适用仓库：`code/focus_app`

## 1. 目标

PRECHECK 是正式专注会话之前的设备准备阶段，不属于会话统计区间。
它可以打开摄像头并运行视觉模型，以支持人物存在判断、姿态证据展示和云台位置调整；但 PRECHECK 产生的任何数据都不得进入正式分神判定、时序累计、提醒、数据库会话或报告。

## 2. 冻结语义

1. App 发出 start 后，系统进入 PRECHECK，并预留一个 `session_id` 作为本次启动流程和云台命令的关联 ID。
2. 预留 ID 不代表正式会话已经开始；PRECHECK 时正式 `session.active=false`、正式 elapsed 为 0。
3. PRECHECK 允许运行：相机采集、预览、Pose、Presence、Prone 视觉模型、视觉质量门、云台搜索和人工调节。
4. PRECHECK 禁止运行或写入：PhoneFact session、Fusion、DistractionAssessment 更新、episode、compatibility risk、ReminderPolicy、SQLite session row、会话报告、正式分心/提醒计数。
5. App 确认位置并进入 FOCUSING 的单调时刻，是正式会话唯一开始时刻。完整目标时长从此刻计算。
6. PRECHECK 取消、超时、硬件失败或进程退出时，不生成会话记录、结束报告或“中断会话”。
7. PRECHECK 的视觉时序不得泄漏到正式会话：确认时清空 FrameQueue、重置 Prone 时序窗口、主体跟踪连续性和 Fusion 连续性。
8. PAUSED 仍属于已经开始的正式会话，只暂停累计；它不等同于 PRECHECK。

## 3. 推荐实现边界

采用“同一流程 ID、延迟正式启动”，不拆成第二套 App/云台协议。

### 3.1 会话编排

`SessionOrchestrator::start()`：

- 取得正式视觉资源租约，生成并保留 `session_id`。
- 创建 runtime，准备视觉与可选云台能力。
- 发布 typed PRECHECK runtime snapshot。
- 不创建 SQLite row，不启动正式计时。

`SessionOrchestrator::confirmReady()`：

- 生成唯一的 `FormalStartContext`，包含同一个正式 monotonic/wall-clock 开始时刻，并在一次串行化转移中调用所有 required feature 的正式启动钩子。
- 任一 required feature 无法创建正式状态时，不得进入 FOCUSING；清理尚未完成的启动并保持可解释失败。
- 成功后把 phase 切换为 FOCUSING，并以此刻作为 monotonic/wall-clock 正式开始时间。

为避免把“准备模型”和“开始业务”混在一起，`ISessionFeature` 增加显式正式启动生命周期（名称可为 `beginFormalSession(const FormalStartContext&)`），而不是让各 feature 在 `enterPhase(Focusing)` 内各自读取不一致的时钟并偷偷创建状态。

required feature 的正式启动顺序固定为“内存业务核心在前、SQLite/旧投影提交在后”。正式持久化是最后一个 required commit 点；若此前步骤失败，不存在数据库 row。持久化成功后不再执行可能阻止正式启动的 required 初始化。optional feature 失败只降级。

### 3.2 相机所有权

`CameraPipeline` 不再以旧 `SystemStateStore.session.active` 作为唯一采集开关，而读取 typed runtime phase：

- PRECHECK / FOCUSING / PAUSED：相机资源有效。
- IDLE / ENDING：释放相机。
- PAUSED 仍保持约 1 FPS。

这样 PRECHECK 可以使用相机，同时旧 session 投影保持未激活，不需要向 `SystemStateStore` 增加新的业务真值。
runtime begin/end 仍显式唤醒相机等待条件，避免等待旧 session 投影或最长一秒轮询。

### 3.3 视觉与分神核心

`DistractionPipelineFeature::prepare()` 只加载 Pose/Prone 模型并启动视觉 worker，不调用 `PhoneFactBuffer::beginSession()` 或 `DistractionAssessmentEngine::beginSession()`。

PRECHECK 帧只更新 `VisualSnapshot`；PhoneSnapshot 保持 inactive/stale，Assessment 保持空/UNKNOWN。即使运行 Prone 模型，其输出也只是预检视觉证据。

正式启动钩子执行以下顺序：

1. 清空待处理帧。
2. 重置主体框连续性和 Prone 窗口。
3. 以正式 session_id 启动 PhoneFactBuffer。
4. 启动 DistractionAssessmentEngine。
5. 清空 PRECHECK assessment/reminder 残留并开始接受 FOCUSING 帧。

之后只有 FOCUSING 调用 Fusion `update()` 和 compatibility publication。PAUSED 可继续低频视觉恢复，但不更新正式 episode。

### 3.4 持久化与兼容投影

`LegacySessionProjectionFeature::prepare()` 只校验可启动条件并保存 pending context，不写 `SystemStateStore.session` 或 SQLite。

正式启动钩子才：

- 创建 SQLite session row 和 SessionStart event。
- 设置 `SystemStateStore.session.active=true`。
- 设置正式 start_ms、完整 target/remaining、零计数。
- 唤醒 ReminderPolicy 等旧消费者。

若 PRECHECK 尚未正式开始，`stop()` 只清理 pending 状态；不得调用 `SessionController::endSession()`。

### 3.5 App 契约

- start 成功后返回 phase=`PRECHECK` 和预留 `session_id`，用于后续确认与云台命令。
- typed snapshot 明确区分 `runtime_active` 与 `formal_active`。PRECHECK 时 runtime_active=true、formal_active=false；对外 `session.active=false`，页面使用 typed phase/precheck 状态展示“正在预检”。
- `phone_sync` 在 PRECHECK 返回 `formal_session_not_started`，App 应在确认进入 FOCUSING 后立即发送当前手机事实，并继续 2 秒心跳。
- confirm 成功响应中的正式 `start_time/elapsed/remaining` 从确认时刻开始。
- 取消 PRECHECK 不产生 `push_session_end`。
- PRECHECK 阶段收到 end/cancel 时必须返回“预检已取消”的结果，不能读取并误推送 `SystemStateStore.last_report` 中上一场会话的报告。

## 4. 故障与并发

- start/confirm/cancel/end 继续由 `SessionOrchestrator` 单锁串行化。
- confirm 的正式启动必须具备回滚路径，避免 SQLite 已写入但 Phone/Fusion 未启动的半会话。
- PRECHECK generation 取消后，旧视觉/云台回调不得在 FOCUSING 后重新写入预检状态。
- Demo 资源租约仍在进入 PRECHECK 前取得；PRECHECK 结束或取消后释放。
- 云台故障只降级可选 feature，不得伪造正式会话已经开始。

## 5. 验收测试

1. start→PRECHECK 后 SQLite 中不存在该 ID，正式 session.active=false，elapsed=0。
2. PRECHECK 连续输入人在/趴下/手机事实，不产生 assessment、episode、risk、提醒或计数。
3. PRECHECK 视觉 snapshot 和云台人物观察仍持续更新。
4. PRECHECK cancel/timeout 不产生报告、SessionEnd 或中断记录。
5. confirm 时才创建唯一 SQLite row，正式 start time 接近 confirm，而不是 start 请求。
6. confirm 后目标时长完整，第一帧 Fusion 不继承 PRECHECK 的 Prone/主体/episode 连续性。
7. PRECHECK phone_sync 被明确拒绝；FOCUSING 首次 phone_sync 正常接受。
8. PAUSED/Resume 仍保留已开始会话的累计语义。
9. 相机在 PRECHECK 正常开启，IDLE/ENDING 释放，且不引入第二个 VideoCapture。
10. host 全测试、ARM 全目标编译和板端 start→PRECHECK→confirm→end 冒烟通过。
11. PRECHECK cancel 后不会重复推送上一场 `push_session_end`。

## 6. 非目标

- 不改变 Presence/Prone 模型本身。
- 不更改三级分神判定阈值。
- 不启用尚未通过 UART/机械门禁的真实云台运动。
- 不把 PRECHECK 另建为可查询的历史记录。
- 不更换现有预留 `session_id`，避免扩大 App 与云台协议改动。

## 7. 云台由运行时健康决定

此前生产 service 固定设置 `EDGEFOCUS_DISABLE_GIMBAL=1`，是硬件/固件开发期间的临时总闸。云台开发完成后，正常产品启动不再依赖 `EDGEFOCUS_GIMBAL_FEATURE=1` 人工放行：

1. 删除 feature opt-in；只要板级配置完整，程序就创建进程级硬件服务和会话级 optional feature。
2. `EDGEFOCUS_DISABLE_GIMBAL` 仅保留为故障维修时的紧急运维总闸，正常 service unit 不设置它。
3. 机械限位、中心、微调步长和方向偏移仍必须来自实测配置。运行时健康检查不能替代静态安全边界；配置不完整时禁止运动。
4. 每次 PRECHECK 由程序检查 UART 通信、PD6 Fault、当前状态和对齐结果。全部通过才进入可用状态并执行搜索；失败则自动降级为无云台 PRECHECK。
5. Fault 或 UART 失联立即 STOP/撤销 Enable。后台只做低频被动恢复探测；本次 PRECHECK 不自动恢复运动，用户显式重新对齐或下一次 PRECHECK 才重新评估。
6. App capability 表示板端已具备并启用了安全配置；实际 `available/ready/manual_lease/fault_reason` 由当次 PRECHECK 状态返回。按钮只在实际 ready/lease 条件满足时开放。

当前本地 `motion_verified=false` 配置保持不变，直到开关维修完成并重新实测限位。因而本地代码可以完成自动判定逻辑，但不会凭空启用运动。

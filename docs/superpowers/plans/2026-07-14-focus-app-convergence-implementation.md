# EdgeFocus 主程序收束与融合实施计划

日期：2026-07-14  
依据：`docs/superpowers/specs/2026-07-14-focus-app-session-architecture.md`

## 原则

- 使用渐进式替换，不同时保留两套生产决策者。
- 先建立可测试的类型与生命周期，再接硬件。
- 每一阶段完成后运行相关测试；上板前必须完成主机纯逻辑回归。
- 不删除独立 `distraction_core`，主程序内形成可独立测试的同源库，待稳定后再决定是否统一源码来源。

## 阶段 0：冻结基线和迁入核心

1. 在 `.gitignore` 忽略 `.superpowers/` 浏览器临时会话。
2. 建立 `core/distraction/` 内部静态库。
3. 迁入 `vision/temporal/fusion/portable_model_json/stai_pose_runtime` 源码与模型契约。
4. 把独立子项目三组纯逻辑测试迁入主程序测试树。
5. 添加“同输入同输出”契约测试样本。

完成条件：主程序构建可链接新核心，纯逻辑测试通过，旧链尚未删除但没有被新代码调用。

## 阶段 1：会话领域类型与稳定快照

新增：

- `session/SessionPhase.h`
- `session/SessionContext.h`
- `session/SessionSnapshot.h`
- `session/SessionEvent.h`
- `session/SessionSnapshotPublisher.{h,cpp}`
- `session/ISessionFeature.h`

工作项：

1. 将 phase 扩展为 IDLE/PRECHECK/FOCUSING/PAUSED/ENDING。
2. 定义 PhoneFact、VisualEvidence、DistractionAssessment、ReminderEvent、GimbalState。
3. SnapshotPublisher 使用单写者事件合并，替代整对象覆盖。
4. 提供旧 App API 的只读兼容序列化层，不允许兼容层反向改写领域状态。
5. 添加顺序、乱序、跨 session、快照一致性测试。

完成条件：所有新模块可以仅依赖稳定类型编译；旧 SystemStateStore 不再是新链的业务接口。

## 阶段 2：SessionOrchestrator 与 Runtime

新增：

- `session/SessionRuntime.{h,cpp}`
- `session/SessionOrchestrator.{h,cpp}`
- `session/SessionFeatureRegistry.{h,cpp}`

工作项：

1. Process 启动只创建 Bluetooth/API/Config/Storage/Health/Demo HTTP。
2. StartSession 原子创建 Runtime 并进入 PRECHECK。
3. 实现 phase transition 与严格启停顺序。
4. PAUSED 采用低功耗保持策略。
5. ENDING 幂等关闭、flush、销毁。
6. 启动时关闭 interrupted session。
7. 添加重复 start/pause/resume/end、启动失败回滚、stop 超时与跨会话状态隔离测试。

完成条件：空闲无 Camera/NPU/Gimbal/Reminder 线程；测试可创建和销毁多次会话而不泄漏状态。

## 阶段 3：接入真实视觉和融合

新增或替换：

- `vision/DistractionPipeline.{h,cpp}`
- `vision/CameraSourceAdapter.{h,cpp}`
- `behavior/PhoneFactBuffer.{h,cpp}`

工作项：

1. 复用唯一 CameraSource。
2. 接入 STAI Pose runtime、主目标选择、58 维特征和 OnlineProneClassifier。
3. 接入 FusionCore，输出新 DistractionAssessment。
4. PhoneFactBuffer 按 session/sequence/freshness 验证 App 上报。
5. 新 SnapshotPublisher 发布 health/visual/phone/assessment。
6. 用同一视频回放比对独立子程序与主程序结果。
7. 切断旧 BehaviorEngine、FocusStateMachine、EventDetector、RiskScorer 的生产注册。

完成条件：主程序只有新核心产生分神等级；独立子程序与主程序回放结果一致。

## 阶段 4：提醒与蓝牙重构

新增或重构：

- `network/BluetoothGateway.{h,cpp}`
- `network/PhoneGattEndpoint.{h,cpp}`
- `device/BandNotificationSink.{h,cpp}`
- `policy/NotificationOutbox.{h,cpp}`
- `policy/ReminderPolicy.{h,cpp}`

工作项：

1. ReminderPolicy 改为消费 assessment/episode，不消费 risk_score。
2. NotificationRouter 仅保留 APP/BAND。
3. App 提醒事件持久化、ACK、重试和 event_id 去重。
4. 手环发送三级指令并记录 delivery 状态。
5. 统一协调 BlueZ adapter 上的 GATT Server/Client。
6. 删除 DeviceManager 对 ScreenController/SoundController 的依赖和生产源文件。
7. 保留旧手机命令的兼容解析，但忽略 App 上报的 distraction_level。

完成条件：App 与手环可并行使用；板端不再执行屏幕或声音动作。

## 阶段 5：云台会话插件

新增或重构：

- `device/GimbalSessionPlugin.{h,cpp}`
- `device/GimbalCommandArbiter.{h,cpp}`
- `device/OpenCvSubjectTracker.{h,cpp}`

工作项：

1. 保留并封装 GimbalController 串口协议。
2. 将全局 GimbalMonitor 改为 SessionFeature。
3. PRECHECK 实现安全搜索序列和手动 lease。
4. FOCUSING 用 NPU subject box 重锚定 OpenCV tracker。
5. 加入 deadband、限频、软限位和丢失保持。
6. PAUSED/ENDING 安全 STOP。
7. 故障进入 DEGRADED_FIXED_CAMERA，主链继续。
8. 添加自动/手动仲裁、越界、超时和故障旁路测试。

完成条件：只有一个相机采集和一个串口命令所有者；插件故障不终止 SessionRuntime。

## 阶段 6：演示页和备用链路

重构 `DemoHttpServer` 与前端：

1. 删除 override 生产状态接口。
2. `/api/snapshot` 只读稳定 SessionSnapshot。
3. `/api/session/*` 显式调用 SessionOrchestrator。
4. 增加 LIVE_CAMERA / RECORDED_INPUT 输入选择。
5. Recorded input 每帧进入真实 NPU/Fusion。
6. 增加 EvidenceBundle 格式与 EVIDENCE_REPLAY。
7. Evidence replay 禁止提醒和正式统计。
8. 预览与推理 overlay 解耦刷新，显示帧新鲜度、NPU latency、source 标签。
9. 预留至少一套 Presence、Prone、Phone 三种现场备用证据。

完成条件：三种来源在页面上不可混淆；高帧率预览不伪造推理帧。

## 阶段 7：清理旧架构

1. 从 CMake 和 main.cpp 删除旧 VisionEngine/BehaviorEngine/FSM/RiskScorer 注册。
2. 删除困倦、闭眼、转头、视觉手机和旧 risk_score 对外字段。
3. 删除 ScreenController、SoundController 和旧板端 UI。
4. 将 SystemStateStore 缩减为兼容桥或完全删除。
5. 更新配置、systemd unit、安装模型与静态资源。
6. 更新 App/BLE 协议文档。

完成条件：仓库搜索不到生产使用的旧状态；构建安装包包含模型、时序分类器、配置和演示资源。

## 阶段 8：验证与部署

### 主机

- core 单元测试
- session lifecycle 测试
- snapshot 并发/顺序测试
- phone category episode 连续性测试
- reminder delivery/ACK 测试
- gimbal arbiter 纯逻辑测试
- prerecorded replay 集成测试

### 板端

1. 停止独立子程序服务，释放 camera/NPU。
2. 部署到新 release 目录，不覆盖现有可回滚版本。
3. 验证 IDLE 不占相机/NPU。
4. 验证 PRECHECK、手动云台、自动搜索。
5. 验证 FOCUSING Presence/Prone/Phone、App/手环提醒。
6. 验证 PAUSED 低功耗保持和恢复。
7. 验证云台故障旁路。
8. 验证 LIVE、RECORDED、EVIDENCE_REPLAY。
9. 完成冷启动、连续三次会话、异常退出恢复测试。

### 发布

- 冻结模型、配置、静态页面和 EvidenceBundle checksum。
- 保留上一 release 和一键回滚。
- 记录板端 FPS、NPU latency、内存、温度和蓝牙送达率。


# EdgeFocus App 主机适配实施计划

- 设计规格：`docs/superpowers/specs/2026-07-16-edgefocus-host-alignment-design.md`
- 改造前基线：`ff073ab`
- 已审查规格：`e6e58d8`
- 执行方式：按依赖顺序连续推进，阶段间不等待用户批准

## 0. 基线验证

1. 记录 `git status`，只保留已知 `_workspace` 未跟踪文件。
2. 执行 `gradlew.bat testDebugUnitTest`，记录现有失败。
3. 执行 `gradlew.bat assembleDebug`，确认本地构建链可用。
4. 盘点 Room v7 schema、Manifest 权限和现有 Hilt module。

## 1. 协议模型与 BLE 分层

### 新增

- `data/ble/model/HostProtocolModels.kt`
- `data/ble/HostJsonCodec.kt`
- `data/ble/BleRequestScheduler.kt`
- `data/ble/ConnectionSupervisor.kt`
- 对应 JVM 测试

### 修改

- `data/remote/BleCommand.kt`
- `data/remote/BleManager.kt`
- `data/remote/BleChunkAssembler.kt`
- `data/remote/BoardJsonParser.kt`
- `data/remote/PushEventHandler.kt`

### 目标

- notification 订阅后执行 `client_hello` 和 `get_current_state` 才进入 READY。
- 支持 P0–P4 请求优先级与预览取消。
- 精确解析 `schema_version`、capabilities、`runtime_snapshot`、`data.ack_status`、外部供电、PRECHECK 和可靠事件 TTL。
- `session_control(start)` 固定 `source_mode=live_camera`。
- 移除 phone_sync 的 distraction_level。

## 2. 新 Room 结构

### 新增实体/DAO

- `HostEventEntity` / `HostEventDao`
- `SyncOutboxEntity` / `SyncOutboxDao`
- `ProtocolCheckpointEntity` / `ProtocolCheckpointDao`

### 修改

- `AppDatabase.kt`
- `DatabaseMigrations.kt`
- `Session.kt` / `SessionDao.kt`
- `Settings.kt` / `SettingsDao.kt`

### 目标

- 开发期允许破坏性升级到新 schema。
- LOCAL Session 与 Sync Outbox 同事务写入。
- Event Inbox 保存 ACK 阶段、TTL 本地 deadline、boot count、dispatch claim/effect 状态。
- checkpoint 保存 phone sequence 与 PRECHECK command sequence。

## 3. 唯一会话协调器

### 新增

- `domain/session/AppUiSnapshot.kt`
- `domain/session/AppSessionCoordinator.kt`
- `domain/session/LocalSessionRuntime.kt`
- `domain/session/HostSessionClient.kt`
- `data/session/SessionStore.kt`
- `data/session/SessionSyncOutbox.kt`
- `data/session/HostEventInbox.kt`

### 迁移

- 从 `TimerStateHolder` 移出会话状态机、连接恢复、保存、补传和主机完成判定。
- `TimerStateHolder` 临时变成兼容 facade，页面切换完成后删除或最小化。

### 测试

- LOCAL/EDGEFOCUS owner 冻结。
- Activity 重建与竖横屏共享 session_id。
- EDGEFOCUS PRECHECK/RUNNING/PAUSED/ENDING 断线恢复。
- 主机 IDLE 不携匹配报告时不本地完成。

## 4. PhoneFactMonitor

### 新增/修改

- 新增 `domain/phone/PhoneFactMonitor.kt`
- 修改 `AppMonitorService.kt`
- 修改 `AppMonitorBleUsagePolicy.kt`
- 修改 `AppMonitorForegroundPolicy.kt`
- 修改 `AppCategorizer.kt`

### 目标

- MainActivity、ControlPanelActivity、切换窗口、黑屏屏保、系统非交互均输出 `is_in_use=false`。
- UNKNOWN_PERMISSION/UNKNOWN 停止 phone_sync。
- RUNNING 首帧立即发送，可信事实每 2 秒新 sequence。
- sequence 由 checkpoint、runtime snapshot 和 last accepted 最大值恢复。
- LOCAL 只供本地策略消费，不向主机发送。

## 5. 可靠提醒与后台交付

### 新增

- `domain/alert/AlertDeliveryCoordinator.kt`
- `service/HostAlertNotificationService.kt` 或等价进程组件
- `presentation/shared/AlertOverlay.kt`
- 通知渠道与稳定 notification ID 工具

### 目标

- `push_alert`：received → presented → user_ack。
- `push_session_end`：received → presented，不发 user_ack。
- TTL 使用接收时 `elapsedRealtime + remaining_ttl_ms`，检测手机重启。
- 外部效果 at-most-once，崩溃后 uncertain 不重复声音/振动。
- 任意前台页面显示全局覆盖；后台/系统锁屏发通知和振动。

## 6. PRECHECK、预览、云台与手环/供电

### 新增

- `presentation/shared/PrecheckPanel.kt`
- `domain/precheck/PrecheckController.kt`
- `domain/band/BandProxyClient.kt`
- `presentation/shared/BandStatusPanel.kt`
- `presentation/shared/PowerProfilePanel.kt`

### 目标

- 真实 base64 图像解码与 Compose 绘制。
- PRECHECK 期间 1–2 FPS P4 可取消刷新。
- 精确云台命令、发送前持久 sequence、两步确认进入 FOCUSING。
- 无 `band_proxy_control_v1` 时只读状态；有能力才启用搜索/重连。
- 使用外部供电与 Home/Go，删除电量和默认 67%。

## 7. 屏保与 Activity 生命周期

### 新增/修改

- 新增 `presentation/shared/BlackScreenSaver.kt`
- 修改 `MainActivity.kt`
- 修改 `ControlPanelActivity.kt`
- 修改 `AndroidManifest.xml`
- 扩展 Settings 与设置 UI

### 目标

- 两 Activity 设置 `FLAG_KEEP_SCREEN_ON`。
- 黑屏层首次触摸只唤醒，不透传。
- SCREEN 提醒唤醒 App 视觉层。
- MIUI 14 / Android 13 权限与前台服务声明正确。

## 8. 页面接入与旧逻辑删除

### 修改

- `HomeViewModel.kt` / `HomeScreen.kt`
- `ControlPanelViewModel.kt` / `ControlPanelScreen.kt`
- `SettingsViewModel.kt` / `SettingsScreen.kt`
- `DashboardViewModel.kt` / `DashboardScreen.kt`
- `NavigationGraph.kt`

### 约束

- 首页视觉与操作结构不大改。
- 横屏保持独立三页 Pager、圆形计时器和图标按钮。
- Page 2 为本次统计，Page 3 为控制面板设置。
- 正常状态不显示“主机权威/已同步”。
- 删除电量、困倦、板端屏幕/声音、手机最终等级和旧 risk/focus_state。
- 移除页面各自保存、监控和断线补偿。

## 9. 自动化验证

1. `gradlew.bat testDebugUnitTest`
2. `gradlew.bat assembleDebug`
3. `gradlew.bat connectedDebugAndroidTest`（真机可用时）
4. Room 重启、Inbox/Outbox、coordinator、断线恢复测试。
5. 抓包断言：
   - hello → current state → READY；
   - phone_sync 无 distraction_level；
   - push_session_end 无 user_ack；
   - start 含 live_camera；
   - PRECHECK 两步确认；
   - sequence 跨进程递增。

## 10. 真机联调与收尾

按设计规格第 17.4 节顺序执行 Redmi Note 10 Pro / MIUI 14 / Android 13 与当前主机联调。

- 每步记录 request/session/event/epoch 与主机响应。
- 发现问题先补自动化回归测试，再修实现。
- 最后执行全量单元测试、Debug 构建、安装、关键流程回归。
- 更新规格中的实际能力差异和仍需主机部署的 capability。


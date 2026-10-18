# EdgeFocus 国赛版 App 自适应训练实施计划

> 日期：2026-08-07；范围：`EdgeFocusApp` A1–A2，不修改主机生产源码或 G431。
> 规格：主机仓库 `docs/国赛功能和体验优化项/国赛版本功能补全规划.md`、`自适应训练与App交互详细设计.md`；协议以主机 H4 文档/fixture 为准。

## 现状、边界与顺序

只读核对：`CameraPreviewDecoder.kt` 的 `produceState(initialValue=null, preview keys...)` 会在新帧解码前丢旧图；`FeedbackDialog.kt` 仍有六个旧值；协议在 `BoardJsonParser.kt`/`HostJsonCodec.kt`/`BleManager.kt`；`AppSessionCoordinator.kt` 是唯一会话/LOCAL outbox 汇合点；Room v8，已有 `Session` 与按 session 唯一的 `SyncOutboxEntity`；各 Screen/ViewModel 不得另算计划。

硬边界：主机未声明 `adaptive_training_v1` 时隐藏新 UI 而旧会话照常；EDGEFOCUS 未连接仍禁用，LOCAL 可离线开始。App 不推导 stage、倍率、reason、下一时长，不改 Presence/Prone/PhoneFact。Bitmap 不入 Room；仅退出 PRECHECK、session 改变、主动关闭时清空。预览保持 BLE 最低优先级，不影响命令、状态、可靠 ACK、PhoneFact。LOCAL 只显示标注过的缓存建议，主机响应覆盖缓存。每项先 Red、再最小 Green、最后重构；本次仅写计划，不提交。

依赖：`A1 preview reducer/coordinator → shared render → 三值/outbox → A2 protocol fixture → Room cache → coordinator state/actions → Home/横屏 → 报告/Dashboard/Settings → LOCAL/重连集成`。A1 可与主机并行；A2 parser 必须等待 H4 fixture。

基线（仓库根目录 PowerShell）：

```powershell
.\gradlew.bat testDebugUnitTest lintDebug assembleDebug
# 有设备时
.\gradlew.bat connectedDebugAndroidTest
```

## A1：last-good-frame

### Task A1.1：纯预览状态机

**Files**
- Create: `app/src/main/java/com/edgefocus/app/presentation/shared/PreviewUiState.kt`
- Create: `app/src/main/java/com/edgefocus/app/presentation/shared/CameraPreviewStateReducer.kt`
- Create: `app/src/test/java/com/edgefocus/app/presentation/shared/CameraPreviewStateReducerTest.kt`
- Modify: `app/src/test/java/com/edgefocus/app/presentation/shared/CameraPreviewDecodePolicyTest.kt`

**Red**：先用非 Bitmap token 测 request started 保留 last-good；request/chunk/parse/decode failure 只更新 stale/error；成功仅在 session/generation 相同且 sequence 不旧时原子替换并记录 success time；迟到结果不覆盖；退出 PRECHECK、换 session、主动关闭清空。运行：

```powershell
.\gradlew.bat testDebugUnitTest --tests 'com.edgefocus.app.presentation.shared.CameraPreviewStateReducerTest'
```

确认因缺实现/断言失败。

**Green**：实现泛型 `PreviewUiState<T>`（字段为 `lastGoodBitmap`、`lastGoodSequence`、`lastSuccessAtMs`、`requestInFlight`、`stale`、`errorMessage`、`sourceSessionId`）及纯 reducer；重复运行至绿。

**Refactor**：表驱动所有失败与生命周期；提交点 `fix(app): preserve last good preview state`。

### Task A1.2：共享协调器、后台解码和双页面

**Files**
- Create: `app/src/main/java/com/edgefocus/app/presentation/shared/CameraPreviewCoordinator.kt`
- Modify: `app/src/main/java/com/edgefocus/app/presentation/shared/CameraPreviewDecoder.kt`
- Modify: `app/src/main/java/com/edgefocus/app/data/TimerStateHolder.kt`
- Modify: `app/src/main/java/com/edgefocus/app/presentation/ui/home/HomeScreen.kt`
- Modify: `app/src/main/java/com/edgefocus/app/presentation/ui/controlpanel/ControlPanelScreen.kt`
- Create: `app/src/test/java/com/edgefocus/app/presentation/shared/CameraPreviewCoordinatorTest.kt`
- Create: `app/src/androidTest/java/com/edgefocus/app/presentation/shared/CameraPreviewLastGoodFrameTest.kt`

**Red**：可控 decoder 测 A 成功→B pending/失败仍显示 A→C 成功才换；session-1 迟到不得污染 session-2。Compose 测旧图与“刷新中/上一成功时间/陈旧或失败”并存，首次无好帧才占位；Home/横屏消费同一 state。

**Green**：coordinator 在 `Dispatchers.Default` 做已有大小/base64/GRAY8/JPEG 安全解码，成功经 reducer 提交；移除按新 key 重建 null 的所有权。由已有 phase/session/payload 驱动，不新增 polling。

**Verify**：
```powershell
.\gradlew.bat testDebugUnitTest --tests 'com.edgefocus.app.presentation.shared.*'
.\gradlew.bat connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.edgefocus.app.presentation.shared.CameraPreviewLastGoodFrameTest
```
提交点 `fix(app): share last-good preview across layouts`。

### Task A1.3：传输失败与 BLE 优先级

**Files**
- Modify: `app/src/main/java/com/edgefocus/app/data/remote/BleChunkAssembler.kt`
- Modify: `app/src/main/java/com/edgefocus/app/data/remote/BleManager.kt`
- Modify: `app/src/main/java/com/edgefocus/app/data/ble/BleRequestScheduler.kt`
- Modify: `app/src/test/java/com/edgefocus/app/data/remote/BleChunkAssemblerTest.kt`
- Modify: `app/src/test/java/com/edgefocus/app/data/ble/BleRequestSchedulerTest.kt`
- Modify: `app/src/test/java/com/edgefocus/app/presentation/shared/CameraPreviewCoordinatorTest.kt`（该文件由 A1.2 创建）

**Red**：timeout、缺片/超限、JSON/base64/decode 失败均保留旧帧；饱和时 session_control/state/reliable ACK/PhoneFact 先于 preview；训练 UI 不增加 preview 请求。先写断言后运行（预期失败）：

```powershell
.\gradlew.bat testDebugUnitTest --tests 'com.edgefocus.app.data.remote.BleChunkAssemblerTest' --tests 'com.edgefocus.app.data.ble.BleRequestSchedulerTest' --tests 'com.edgefocus.app.presentation.shared.CameraPreviewCoordinatorTest'
```

**Green**：只桥接带 session/generation/sequence 的失败事件，不放宽安全上限、不记录 base64。完成最小实现后原样重跑上述命令（预期通过），再执行验证：

```powershell
.\gradlew.bat testDebugUnitTest --tests 'com.edgefocus.app.data.remote.BleChunkAssemblerTest' --tests 'com.edgefocus.app.data.ble.BleRequestSchedulerTest' --tests 'com.edgefocus.app.presentation.shared.CameraPreviewCoordinatorTest' --rerun-tasks
```

提交点 `fix(app): retain preview through transport failures`。

## A1：三值反馈与可靠覆盖

### Task A1.4：唯一反馈类型

**Files**
- Create: `app/src/main/java/com/edgefocus/app/domain/model/SessionFeedback.kt`
- Modify: `app/src/main/java/com/edgefocus/app/presentation/ui/components/FeedbackDialog.kt`
- Modify: `app/src/main/java/com/edgefocus/app/data/local/entity/Session.kt`
- Modify: `app/src/main/java/com/edgefocus/app/data/remote/BoardJsonParser.kt`
- Modify: `app/src/main/java/com/edgefocus/app/data/remote/BleManager.kt`
- Create: `app/src/test/java/com/edgefocus/app/domain/model/SessionFeedbackTest.kt`
- Modify: `app/src/test/java/com/edgefocus/app/data/remote/BoardJsonParserTest.kt`

**Red**：仅 `too_much/just_right/too_little` 合法；UI 精确“提醒过多/适中/提醒过少”；command/parser round-trip 不转换；旧六值拒绝。

**Green**：enum/sealed wire type 贯穿 UI→coordinator→Room/outbox→BLE；删除所有旧映射。

**Verify**：
```powershell
.\gradlew.bat testDebugUnitTest --tests 'com.edgefocus.app.domain.model.SessionFeedbackTest' --tests 'com.edgefocus.app.data.remote.BoardJsonParserTest'
Get-ChildItem app/src -Recurse -File | Select-String -Pattern 'too_many|not_enough|perfect|well_detected|missed|"other"' | ForEach-Object { throw "legacy feedback remains: $_" }
```
提交点 `fix(app): converge session feedback to three values`。

### Task A1.5：反馈 Outbox 与 Room migration

**Files**
- Create: `app/src/main/java/com/edgefocus/app/data/local/entity/FeedbackOutboxEntity.kt`
- Create: `app/src/main/java/com/edgefocus/app/data/local/dao/FeedbackOutboxDao.kt`
- Modify: `app/src/main/java/com/edgefocus/app/data/local/AppDatabase.kt`
- Modify: `app/src/main/java/com/edgefocus/app/data/local/DatabaseMigrations.kt`
- Modify: `app/src/main/java/com/edgefocus/app/data/session/SessionPersistenceStore.kt`
- Modify: `app/src/main/java/com/edgefocus/app/domain/session/AppSessionCoordinator.kt`
- Create: `app/src/test/java/com/edgefocus/app/data/session/FeedbackOutboxPolicyTest.kt`
- Create: `app/src/androidTest/java/com/edgefocus/app/data/local/AdaptiveDatabaseMigrationTest.kt`

**Red**：同 session 最新反馈覆盖 pending；sent 后修改产生新 revision；timeout/reject 保留“待同步”；accepted 且权威 profile/plan 解析成功才清对应 revision；重连 flush；v8→新版保留旧数据和唯一约束。先写 JVM 与 migration 断言后运行（预期失败；后者要求已连接 emulator/device）：

```powershell
.\gradlew.bat testDebugUnitTest --tests 'com.edgefocus.app.data.session.FeedbackOutboxPolicyTest'
.\gradlew.bat connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.edgefocus.app.data.local.AdaptiveDatabaseMigrationTest
```

**Green**：反馈与 LOCAL completion 分表，或为 outbox 增 kind/revision，禁止现有 session unique 使两种命令互相覆盖；coordinator 统一重试。升级 Room、补 migration/schema export，不 destructive migrate。完成实现后原样重跑上述两条（预期通过），再执行完整验证：

```powershell
.\gradlew.bat testDebugUnitTest --tests 'com.edgefocus.app.data.session.FeedbackOutboxPolicyTest' --rerun-tasks
.\gradlew.bat connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.edgefocus.app.data.local.AdaptiveDatabaseMigrationTest
.\gradlew.bat :app:lintDebug :app:assembleDebug
```

提交点 `feat(app): queue idempotent feedback updates`。

## A2：协议、缓存和唯一状态源

### Task A2.1：主机 fixture 驱动协议

**Files**
- Create: `app/src/main/java/com/edgefocus/app/domain/model/AdaptiveTrainingModels.kt`
- Modify: `app/src/main/java/com/edgefocus/app/data/ble/HostJsonCodec.kt`
- Modify: `app/src/main/java/com/edgefocus/app/data/remote/BoardJsonParser.kt`
- Modify: `app/src/main/java/com/edgefocus/app/data/remote/BleManager.kt`
- Modify: `app/src/main/java/com/edgefocus/app/data/ble/ConnectionSupervisor.kt`
- Create: `app/src/test/resources/adaptive_training_protocol_v1.json`
- Create: `app/src/test/java/com/edgefocus/app/data/remote/AdaptiveTrainingProtocolTest.kt`
- Modify: `app/src/test/java/com/edgefocus/app/data/ble/HostJsonCodecTest.kt`

**Red**：从 H4 fixture 测 capability gate、`get_current_state.adaptive`、plan/profile/version、start duration/plan/adoption、feedback 的 profile update+next plan、preferences/reset confirm、LOCAL 全字段；malformed/error/旧 response 不覆盖 cache；unsupported 不影响旧 session。先复制主机 fixture、写断言并运行（预期失败）：

```powershell
Copy-Item 'C:/Users/Aze/Desktop/self_developed/code/focus_app_national/tests/data/adaptive_training_protocol_v1.json' 'app/src/test/resources/adaptive_training_protocol_v1.json' -Force
.\gradlew.bat testDebugUnitTest --tests 'com.edgefocus.app.data.remote.AdaptiveTrainingProtocolTest' --tests 'com.edgefocus.app.data.ble.HostJsonCodecTest'
```

**Green**：建立 `TrainingPlan/ProfileSummary/ProfileUpdate/SessionTrainingReport/PlanAdoption` 数据类型；parser 不算建议；所有写命令继续现有 request id/scheduler。完成最小实现后原样重跑上述测试（预期通过），再验证测试与 fixture：

```powershell
.\gradlew.bat testDebugUnitTest --tests 'com.edgefocus.app.data.remote.AdaptiveTrainingProtocolTest' --tests 'com.edgefocus.app.data.ble.HostJsonCodecTest' --rerun-tasks
$host='C:/Users/Aze/Desktop/self_developed/code/focus_app_national/tests/data/adaptive_training_protocol_v1.json'
$app='app/src/test/resources/adaptive_training_protocol_v1.json'
if ((Get-FileHash $host).Hash -ne (Get-FileHash $app).Hash) { throw 'protocol fixtures diverged' }
```

提交点 `feat(app): parse adaptive training protocol v1`。

### Task A2.2：权威 cache 与 LOCAL 元数据

**Files**
- Create: `app/src/main/java/com/edgefocus/app/data/local/entity/AdaptiveCacheEntity.kt`
- Create: `app/src/main/java/com/edgefocus/app/data/local/dao/AdaptiveCacheDao.kt`
- Modify: `app/src/main/java/com/edgefocus/app/data/local/entity/Session.kt`
- Modify: `app/src/main/java/com/edgefocus/app/data/local/entity/Settings.kt`
- Modify: `app/src/main/java/com/edgefocus/app/data/local/AppDatabase.kt`
- Modify: `app/src/main/java/com/edgefocus/app/data/local/DatabaseMigrations.kt`
- Modify: `app/src/main/java/com/edgefocus/app/data/session/SessionPersistenceStore.kt`
- Create: `app/src/test/java/com/edgefocus/app/data/session/AdaptiveCachePolicyTest.kt`
- Modify: `app/src/androidTest/java/com/edgefocus/app/data/local/AdaptiveDatabaseMigrationTest.kt`

**Red**：新 version/generatedAt 覆盖，旧/失败不倒退；区分 authoritative/offline/stale/pending/unsupported。Session 保存 target/end/early、L1–3、三类 phone、evidence scope、feedback、plan/adoption、三个版本。LOCAL 无等级时三值为 0 且 PHONE_ONLY；同 session 覆盖。migration 默认值正确。先写 policy/migration 断言后运行（预期失败；migration 命令要求设备）：

```powershell
.\gradlew.bat testDebugUnitTest --tests 'com.edgefocus.app.data.session.AdaptiveCachePolicyTest'
.\gradlew.bat connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.edgefocus.app.data.local.AdaptiveDatabaseMigrationTest
```

**Green**：Room 仅缓存主机输出和 App 意图，不存 Bitmap/算法；cache 更新与 outbox ack 原子；设置只存 onboarding/待同步意图。完成最小实现后原样重跑上述两条（预期通过），再验证 Room schema、lint 与 APK：

```powershell
.\gradlew.bat testDebugUnitTest --tests 'com.edgefocus.app.data.session.AdaptiveCachePolicyTest' --rerun-tasks
.\gradlew.bat connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.edgefocus.app.data.local.AdaptiveDatabaseMigrationTest
.\gradlew.bat :app:lintDebug :app:assembleDebug
```

提交点 `feat(app): cache authoritative adaptive state`。

### Task A2.3：共享 `AdaptiveTrainingUiState`

**Files**
- Create: `app/src/main/java/com/edgefocus/app/domain/session/AdaptiveTrainingUiState.kt`
- Modify: `app/src/main/java/com/edgefocus/app/domain/session/AppUiSnapshot.kt`
- Modify: `app/src/main/java/com/edgefocus/app/domain/session/AppSessionCoordinator.kt`
- Modify: `app/src/main/java/com/edgefocus/app/domain/session/LocalSessionRuntime.kt`
- Create: `app/src/test/java/com/edgefocus/app/domain/session/AdaptiveTrainingCoordinatorTest.kt`

**Red**：host/cache/mode/duration/phase/outbox reducer 始终输出同 plan/profile version；adopt 更新时长并记 ADOPTED，之后转环记 ADJUSTED；stale 不阻止；start 携带字段；LOCAL 冻结缓存 plan；EDGEFOCUS gate 不变；PRECHECK/PAUSED 不累计；重连 state/成功响应覆盖全局。

**Green**：coordinator 提供唯一 state 和 adopt/select/start/feedback/preferences/reset 动作；ViewModel 只投影，不直接 BLE、不另存报告、不生成计划。

```powershell
.\gradlew.bat testDebugUnitTest --tests 'com.edgefocus.app.domain.session.AdaptiveTrainingCoordinatorTest'
```
提交点 `feat(app): centralize adaptive training UI state`。

## A2：页面闭环

### Task A2.4：首页与首次引导

**Files**
- Modify: `app/src/main/java/com/edgefocus/app/presentation/viewmodel/HomeViewModel.kt`
- Modify: `app/src/main/java/com/edgefocus/app/presentation/ui/home/HomeScreen.kt`
- Modify: `app/src/main/java/com/edgefocus/app/presentation/ui/home/DurationSelectorRing.kt`
- Create: `app/src/main/java/com/edgefocus/app/presentation/ui/home/AdaptiveTrainingCard.kt`
- Create: `app/src/main/java/com/edgefocus/app/presentation/ui/home/AdaptiveOnboardingCard.kt`
- Modify: `app/src/test/java/com/edgefocus/app/presentation/viewmodel/HomeStartActionPolicyTest.kt`
- Create: `app/src/androidTest/java/com/edgefocus/app/presentation/ui/home/AdaptiveHomeScreenTest.kt`

**Red**：一次性引导说明规则学习、默认现有时长、默认开启可关闭；IDLE/FINISHED 展示 stage/duration/reason/goal，BASELINE 不伪造结论；采用/转环分别 ADOPTED/ADJUSTED；LOCAL 标离线建议；unsupported 隐藏；PRECHECK 仅紧凑目标；连接 gate 不变。先写 JVM/Compose 断言后运行（预期失败；第二条要求设备）：

```powershell
.\gradlew.bat testDebugUnitTest --tests 'com.edgefocus.app.presentation.viewmodel.HomeStartActionPolicyTest'
.\gradlew.bat connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.edgefocus.app.presentation.ui.home.AdaptiveHomeScreenTest
```

**Green**：Screen 纯渲染 coordinator；手动范围保留 5–120，建议保持 10–60。完成最小实现后原样重跑上述两条（预期通过），再验证：

```powershell
.\gradlew.bat testDebugUnitTest --tests 'com.edgefocus.app.presentation.viewmodel.HomeStartActionPolicyTest' --rerun-tasks
.\gradlew.bat connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.edgefocus.app.presentation.ui.home.AdaptiveHomeScreenTest
.\gradlew.bat :app:lintDebug
```

提交点 `feat(app): add adaptive guidance to focus home`。

### Task A2.5：横屏同源

**Files**
- Modify: `app/src/main/java/com/edgefocus/app/presentation/viewmodel/ControlPanelViewModel.kt`
- Modify: `app/src/main/java/com/edgefocus/app/presentation/ui/controlpanel/ControlPanelScreen.kt`
- Modify: `app/src/main/java/com/edgefocus/app/presentation/ui/controlpanel/ConfigPage.kt`
- Create: `app/src/androidTest/java/com/edgefocus/app/presentation/ui/controlpanel/AdaptiveControlPanelTest.kt`

**Red**：与 Home 相同 plan/profile/version/时长；IDLE、PRECHECK preview、FOCUSING/PAUSED 目标/提醒、FINISHED 报告/反馈均同源；ViewModel 无 planner/第二次 save/direct BLE。先写 Compose 断言后运行（预期失败，要求设备）：

```powershell
.\gradlew.bat connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.edgefocus.app.presentation.ui.controlpanel.AdaptiveControlPanelTest
```

**Green**：复用 shared card、preview、report/feedback 与 coordinator action。完成最小实现后原样重跑（预期通过），再联合验证横竖屏：

```powershell
.\gradlew.bat connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.edgefocus.app.presentation.ui.controlpanel.AdaptiveControlPanelTest
.\gradlew.bat connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.edgefocus.app.presentation.ui.home.AdaptiveHomeScreenTest
```

提交点 `feat(app): reuse adaptive flow in control panel`。

### Task A2.6：会后报告与反馈修改

**Files**
- Create: `app/src/main/java/com/edgefocus/app/presentation/ui/components/SessionTrainingReportSheet.kt`
- Modify: `app/src/main/java/com/edgefocus/app/presentation/ui/components/FeedbackDialog.kt`
- Modify: `app/src/main/java/com/edgefocus/app/presentation/ui/home/HomeScreen.kt`
- Modify: `app/src/main/java/com/edgefocus/app/presentation/ui/controlpanel/ControlPanelScreen.kt`
- Modify: `app/src/main/java/com/edgefocus/app/presentation/viewmodel/HomeViewModel.kt`
- Modify: `app/src/main/java/com/edgefocus/app/presentation/viewmodel/DashboardViewModel.kt`
- Create: `app/src/androidTest/java/com/edgefocus/app/presentation/ui/components/SessionTrainingReportSheetTest.kt`

**Red**：报告显示 completion/effective focus、L2/3 次数与时长、主要来源、基线结论、版本/计划；FINISHED 不静默回 IDLE，等待时不造数据；反馈成功显示更疏/保持/更密及 next plan，timeout 待同步；Dashboard 修改走同覆盖接口；LOCAL 显 PHONE_ONLY/缓存建议/待主机更新。先写 Compose 断言后运行（预期失败，要求设备）：

```powershell
.\gradlew.bat connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.edgefocus.app.presentation.ui.components.SessionTrainingReportSheetTest
```

**Green**：单一 report sheet；ACK snooze 与长期 feedback 分离。完成最小实现后原样重跑（预期通过），再与三值/Outbox 联合验证：

```powershell
.\gradlew.bat connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.edgefocus.app.presentation.ui.components.SessionTrainingReportSheetTest
.\gradlew.bat testDebugUnitTest --tests 'com.edgefocus.app.domain.model.SessionFeedbackTest' --tests 'com.edgefocus.app.data.session.FeedbackOutboxPolicyTest'
```

提交点 `feat(app): close session report and feedback loop`。

### Task A2.7：Dashboard

**Files**
- Modify: `app/src/main/java/com/edgefocus/app/presentation/viewmodel/DashboardViewModel.kt`
- Modify: `app/src/main/java/com/edgefocus/app/presentation/ui/dashboard/DashboardScreen.kt`
- Modify: `app/src/main/java/com/edgefocus/app/data/local/dao/SessionDao.kt`
- Create: `app/src/main/java/com/edgefocus/app/presentation/viewmodel/AdaptiveDashboardPolicy.kt`
- Create: `app/src/test/java/com/edgefocus/app/presentation/viewmodel/AdaptiveDashboardPolicyTest.kt`
- Create: `app/src/androidTest/java/com/edgefocus/app/presentation/ui/dashboard/AdaptiveDashboardScreenTest.kt`

**Red**：最近 5 场完成率、高风险占比、主要来源、adoption；当前 stage/reason 来自 cache；详情含版本/plan/feedback/下一建议；不得只以总时长增长称有效；PHONE_ONLY 不伪造 high-risk。先写 JVM/Compose 断言后运行（预期失败；第二条要求设备）：

```powershell
.\gradlew.bat testDebugUnitTest --tests 'com.edgefocus.app.presentation.viewmodel.AdaptiveDashboardPolicyTest'
.\gradlew.bat connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.edgefocus.app.presentation.ui.dashboard.AdaptiveDashboardScreenTest
```

**Green**：policy 仅排序/比率/format，不生成建议。完成最小实现后原样重跑上述两条（预期通过），再验证：

```powershell
.\gradlew.bat testDebugUnitTest --tests 'com.edgefocus.app.presentation.viewmodel.AdaptiveDashboardPolicyTest' --rerun-tasks
.\gradlew.bat connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.edgefocus.app.presentation.ui.dashboard.AdaptiveDashboardScreenTest
.\gradlew.bat :app:lintDebug
```

提交点 `feat(app): show adaptive training trends`。

### Task A2.8：Settings

**Files**
- Modify: `app/src/main/java/com/edgefocus/app/presentation/viewmodel/SettingsViewModel.kt`
- Modify: `app/src/main/java/com/edgefocus/app/presentation/ui/settings/SettingsScreen.kt`
- Modify: `app/src/main/java/com/edgefocus/app/data/local/entity/Settings.kt`
- Create: `app/src/main/java/com/edgefocus/app/presentation/viewmodel/AdaptiveSettingsPolicy.kt`
- Create: `app/src/test/java/com/edgefocus/app/presentation/viewmodel/AdaptiveSettingsPolicyTest.kt`
- Create: `app/src/androidTest/java/com/edgefocus/app/presentation/ui/settings/AdaptiveSettingsScreenTest.kt`

**Red**：unsupported 隐藏；enabled、目标 10–60、channels 发 update；pending/error/success 可见且以主机值覆盖；展示 version/time；关闭不删历史；reset 二次确认且 `confirm=true`，成功只重置画像。先写 JVM/Compose 断言后运行（预期失败；第二条要求设备）：

```powershell
.\gradlew.bat testDebugUnitTest --tests 'com.edgefocus.app.presentation.viewmodel.AdaptiveSettingsPolicyTest'
.\gradlew.bat connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.edgefocus.app.presentation.ui.settings.AdaptiveSettingsScreenTest
```

**Green**：ViewModel 调 coordinator；离线意图不假装生效。完成最小实现后原样重跑上述两条（预期通过），再验证：

```powershell
.\gradlew.bat testDebugUnitTest --tests 'com.edgefocus.app.presentation.viewmodel.AdaptiveSettingsPolicyTest' --rerun-tasks
.\gradlew.bat connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.edgefocus.app.presentation.ui.settings.AdaptiveSettingsScreenTest
.\gradlew.bat :app:lintDebug
```

提交点 `feat(app): add adaptive personalization settings`。

### Task A2.9：LOCAL、重连与跨屏一致性

**Files**
- Modify: `app/src/main/java/com/edgefocus/app/domain/session/AppSessionCoordinator.kt`
- Modify: `app/src/main/java/com/edgefocus/app/data/ble/ConnectionSupervisor.kt`
- Modify: `app/src/main/java/com/edgefocus/app/data/session/SessionPersistenceStore.kt`
- Create: `app/src/test/java/com/edgefocus/app/domain/session/AdaptiveReconnectIntegrationTest.kt`
- Create: `app/src/androidTest/java/com/edgefocus/app/presentation/AdaptiveCrossScreenStateTest.kt`

**Red**：LOCAL completion+upload+feedback revision 可恢复；重连只上传一次并覆盖 plan/profile；重复 accepted、修改反馈、重启不丢/错清；新 state 覆盖、旧 push/response 不倒退；五页面同 version；unsupported 全隐藏但旧会话正常。先写 JVM/跨屏 Compose 断言后运行（预期失败；第二条要求设备）：

```powershell
.\gradlew.bat testDebugUnitTest --tests 'com.edgefocus.app.domain.session.AdaptiveReconnectIntegrationTest'
.\gradlew.bat connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.edgefocus.app.presentation.AdaptiveCrossScreenStateTest
```

**Green**：READY 后固定 `negotiate → refresh state → flush LOCAL/feedback → cache authoritative responses`，沿用 scheduler/request id/backoff；页面只观察 coordinator。完成最小实现后原样重跑上述两条（预期通过），再执行聚焦回归：

```powershell
.\gradlew.bat testDebugUnitTest --tests 'com.edgefocus.app.domain.session.AdaptiveReconnectIntegrationTest' --tests 'com.edgefocus.app.domain.session.AdaptiveTrainingCoordinatorTest' --tests 'com.edgefocus.app.data.session.AdaptiveCachePolicyTest' --tests 'com.edgefocus.app.data.session.FeedbackOutboxPolicyTest' --rerun-tasks
.\gradlew.bat connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.edgefocus.app.presentation.AdaptiveCrossScreenStateTest
.\gradlew.bat :app:lintDebug :app:assembleDebug
```

提交点 `feat(app): reconcile local adaptive state after reconnect`。

## 最终回归与验收

```powershell
.\gradlew.bat clean testDebugUnitTest lintDebug assembleDebug
.\gradlew.bat connectedDebugAndroidTest   # 有设备时
```

现场链路：A→坏 B→C 不空白且换 session 不回流；preview flood 下关键队列优先；无 capability 隐藏新 UI 且旧 EDGEFOCUS 正常；三场从 BASELINE 进入确定阶段且跨横竖屏一致；feedback timeout→待同步→重连覆盖，修改同场仅最新 pending；LOCAL 离线报告→重连只上传一次→权威 plan 覆盖。报告必须含 completion/effective/L2+L3/来源/结论；Dashboard 不只看总时长；reset 二次确认。

```powershell
git status --short
git diff --check
git diff --name-only | Select-String -Pattern 'G431|firmware' | ForEach-Object { throw "out-of-scope: $_" }
```

不得删除或提交既有 `_workspace` 未跟踪资产。

## 覆盖追踪

- last-good-frame 全规则与最低优先级：A1.1–A1.3；三值与 Outbox：A1.4–A1.5。
- capability/state/start/feedback/preferences/reset/LOCAL：A2.1–A2.3。
- 首页/引导/PRECHECK/会中：A2.4；横屏：A2.5；报告/可改反馈：A2.6；Dashboard：A2.7；Settings：A2.8。
- LOCAL 缓存补传、重连权威覆盖、跨页面一致：A2.2/A2.3/A2.9；降级与非目标由开头边界及最终验收覆盖。

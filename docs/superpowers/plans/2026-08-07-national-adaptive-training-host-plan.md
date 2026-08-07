# EdgeFocus 国赛版主机自适应训练实施计划

> 日期：2026-08-07
> 范围：`focus_app_national` 主机 H1–H4；**不修改 G431**。
> 权威规格：`docs/国赛功能和体验优化项/国赛版本功能补全规划.md`、`docs/国赛功能和体验优化项/自适应训练与App交互详细设计.md`。

## 目标、现状与硬边界

目标是在现有 `SessionOrchestrator -> DistractionAssessment -> ReminderPolicy`、SQLite、报告和 `AppCommandService` 接缝上增加确定性提醒决策、会话训练汇总、可解释画像与下一场计划，并保持旧 App 的基础会话兼容。

只读核对到的现状：领域类型集中在 `session/SessionDomain.h`；`storage/schema.h` 仍是旧全量 schema，`SQLiteStore.*` 与 `ReportGenerator.*` 已承载 session/feedback/history；`ReminderPolicy.*` 当前自行持有 level/cooldown，旧测试在 `tests/test_reminder_policy.cpp`；命令路由已有 `get_current_state`、`session_feedback`、`upload_local_session`、preferences 与 hello capability；构建由根 `CMakeLists.txt` 和 `tests/CMakeLists.txt` 管理。

边界：

- 不新增视觉状态/模型，不改变 Presence、Prone、PhoneFact、Fusion TTL/阈值或 PRECHECK 正式边界。
- `NOT_PRESENT/UNKNOWN`（代码中对应非 `Applicability::Active`）以及空 level 永不提醒；PRECHECK/PAUSED/ENDING 不提醒也不累计。
- 画像与计划是跨会话服务；进入 FOCUSING 时冻结版本，本场反馈只影响下一场。
- 当前库只有测试数据：实施采用新版全量建库，部署清旧 DB，不增加旧生产库迁移代码。
- 每个任务严格 Red → Green → Refactor；Red 必须先运行并确认因预期断言/缺符号失败，不能以编译环境错误冒充。
- 本计划实施时允许逐任务提交；本次计划编写不提交。

## 依赖顺序

`H1 domain/schema/transaction -> H1 accumulator/report -> H2 pure decision -> H2 policy shell -> H3 personalization -> H3 planner/services/hooks -> H4 protocol/docs -> full regression`。

H2 依赖 H1 的冻结版本与持久化模型；H3 依赖 H1 的 summary/事务；H4 最后暴露已稳定的服务。App A1 可并行，App A2 仅在 H4 JSON fixture 冻结后开始联调。

## 基线命令

从仓库根目录运行（Windows PowerShell）：

```powershell
cmake -S . -B build-national-host -DEDGEFOCUS_BUILD_TESTS=ON -DEDGEFOCUS_WITH_OPENCV=OFF -DEDGEFOCUS_WITH_STAI_MPU=OFF
cmake --build build-national-host --config Debug -j 4
ctest --test-dir build-national-host -C Debug --output-on-failure
```

若复用已有 build 目录，先执行第一条以刷新新增 target。后续聚焦命令均假定此目录。

---

## H1：领域、全量 schema 与原子事务

### Task H1.1：冻结领域合同和 schema 建库

**Files**

- Modify: `session/SessionDomain.h`
- Modify: `storage/schema.h`
- Modify: `storage/SQLiteStore.h`
- Modify: `storage/SQLiteStore.cpp`
- Modify: `tests/test_storage.cpp`
- Modify: `tests/CMakeLists.txt`

**Red**

1. 在 `tests/test_storage.cpp` 先加空库测试：断言创建 `session_training_summary`、`personalization_profile`、`training_plan`、`profile_update_event`；逐列检查 summary 的全部规格字段及 session 的冻结字段。
2. 加 `SessionPolicySnapshot` round-trip 测试，覆盖 `policy_version/profile_version/training_plan_id/planner_version/selected_duration_min/ADOPTED|ADJUSTED|NOT_AVAILABLE`。
3. 加约束测试：反馈只接受 `too_much/too_little/just_right`；profile 固定 `profile_id=default`；active plan 唯一。
4. 运行并确认失败：

```powershell
cmake --build build-national-host --config Debug --target test_storage -j 4
ctest --test-dir build-national-host -C Debug -R '^test_storage$' --output-on-failure
```

**Green**

1. 在 `SessionDomain.h` 增加 `PlanAdoption`、`SessionPolicySnapshot`、`SessionTrainingSummary` 领域类型；summary 精确包含 target/focusing/effective focus、L1/L2/L3、prone、三类 phone、early/end reason、reminder/ACK、plan adoption 和三个版本。
2. 在 `schema.h` 全量定义四张新表和必要索引/约束；保留现有表供兼容。`training_plan` 保存 `based_on_sessions_json`、有效期、active；profile 保存 channels、source digest 和 update reason；event 保存旧/新版本和值。
3. 在 `SQLiteStore` 增加显式 transaction helper（begin/commit/rollback 或 RAII），以及 summary/profile/plan/event 的强类型读写接口；不把算法塞入 store。
4. 只实现使测试通过的建库和 round-trip；执行同一聚焦命令确认 Green。

**Refactor / verification**

- 将 enum wire conversion 集中为单一函数，未知数据库值失败而非静默伪造。
- 用 `sqlite3` 查询确认空目录一次初始化成功；测试结束删除临时 DB。
- Commit point: `feat(host): add adaptive training domain and schema`。

### Task H1.2：反馈/profile/plan 原子事务与幂等覆盖

**Files**

- Modify: `storage/SQLiteStore.h`
- Modify: `storage/SQLiteStore.cpp`
- Modify: `tests/test_storage.cpp`

**Red**

1. 先写 `saveFeedbackAndAdaptiveState(...)` 事务测试：保存/覆盖 feedback → 条件更新 profile/event → 切换 active plan，在每个注入失败点都应整体回滚。
2. 覆盖同 session 同内容、同 session 修改反馈、相同 `source_digest`：前者不新建 version/event/plan，修改后只重算一次。
3. 覆盖 LOCAL summary 同 `session_id + content digest` 重传无变化；内容变化覆盖 summary 并触发一次后续重算。
4. 运行 `test_storage`，确认接口缺失或原子断言失败。

**Green**

1. 增加单一事务 API，使 service 能在一个 SQLite transaction 内完成 feedback/profile/event/active-plan；异常路径统一 rollback。
2. 对反馈使用 session 主键覆盖；对 LOCAL summary 保存内容摘要/版本以判断 no-op 与 changed。
3. 增加仅测试可用的 fault injection callback/step enum，不在协议层模拟失败。
4. 重跑 `test_storage` 至通过。

**Refactor / verification**

```powershell
ctest --test-dir build-national-host -C Debug -R '^test_storage$' --repeat until-fail:20 --output-on-failure
```

- Commit point: `feat(host): make adaptive updates transactional and idempotent`。

## H1：正式会话汇总、报告与历史

### Task H1.3：纯 `SessionSummaryAccumulator`

**Files**

- Create: `session/SessionSummaryAccumulator.h`
- Create: `session/SessionSummaryAccumulator.cpp`
- Create: `tests/test_session_summary_accumulator.cpp`
- Modify: `tests/CMakeLists.txt`

**Red**

1. 用显式 monotonic 时间片测试：仅 FOCUSING 累积；PRECHECK/PAUSED/ENDING 为 0；phase 边界不重复计时，时钟倒退不产生负数。
2. 覆盖 applicable L1/L2/L3、effective focus、prone、phone learning/entertainment/other 独立时长；学习手机可统计但不自动算干扰。
3. 覆盖提醒 issued/ACK 去重、early end/end reason、冻结版本原样进入 summary。
4. 运行：

```powershell
cmake --build build-national-host --config Debug --target test_session_summary_accumulator -j 4
ctest --test-dir build-national-host -C Debug -R '^test_session_summary_accumulator$' --output-on-failure
```

**Green**

- 实现无 SQLite/BLE 依赖的 accumulator；输入 snapshot/assessment/phase 与 monotonic now，结束时产出 immutable summary。
- 对同一采样区间只归入一个等级/phone 类；无有效 assessment 不臆测等级。
- 重跑聚焦测试。

**Refactor / verification**

- 表驱动所有 phase/category，加入 1 ms 边界测试。
- Commit point: `feat(host): accumulate formal session training summaries`。

### Task H1.4：接入 session end、报告、历史与 LOCAL

**Files**

- Modify: `session/SessionRuntime.h`
- Modify: `session/SessionRuntime.cpp`
- Modify: `session/SessionOrchestrator.h`
- Modify: `session/SessionOrchestrator.cpp`
- Modify: `storage/ReportGenerator.h`
- Modify: `storage/ReportGenerator.cpp`
- Modify: `network/AppCommandService.cpp`
- Modify: `tests/test_session_runtime.cpp`
- Modify: `tests/test_storage.cpp`
- Modify: `tests/test_ble_gatt.cpp`

**Red**

1. runtime 测试先断言正式进入 FOCUSING 时冻结 snapshot；暂停、预检取消、prepare_failed 不成为有效 summary。
2. 报告测试断言完成率、有效专注、L2/L3 次数与时长、主要来源、版本/plan/feedback 暴露；历史分页携带相同字段。
3. LOCAL parser 测试覆盖 target/end/early_end、三个 level、三个 phone 时长、`evidence_scope=PHONE_ONLY`、feedback、plan id/adoption；缺失/负值/非法反馈拒绝。
4. 运行三个 target，确认 Red。

**Green**

1. `SessionRuntime/Orchestrator` 只在正式开始创建 accumulator 并冻结 profile/plan/policy；结束时保存 summary，取消预检不保存有效样本。
2. `ReportGenerator` 从结构化 summary 生成报告，不从 event 文本反推；主要来源排除 phone learning，除非独立 prone 最终使 PRONE 成为主要来源。
3. LOCAL 上传先规范化并按 session 幂等写 summary；此任务只接入存储，重算 hook 在 H3 完成。
4. 运行聚焦测试至 Green。

**Refactor / verification**

```powershell
ctest --test-dir build-national-host -C Debug -R 'test_(session_runtime|storage|ble_gatt)' --output-on-failure
```

- Commit point: `feat(host): persist adaptive summaries in reports and local upload`。

---

## H2：`ReminderDecisionEngine` 与投递外壳

### Task H2.1：先锁定旧证据回归，再实现纯决策器

**Files**

- Create: `policy/ReminderDecisionEngine.h`
- Create: `policy/ReminderDecisionEngine.cpp`
- Create: `tests/test_reminder_decision_engine.cpp`
- Modify: `tests/test_distraction_fusion.cpp`
- Modify: `tests/test_reminder_policy.cpp`
- Modify: `tests/CMakeLists.txt`
- Create: `config/ReminderPolicyConfig.h`（仅保存 L1/L2/L3 默认重复冷却；不得写入 App/协议）

**Red**

1. 先保留/扩展 fusion 回归：Presence/Prone/PhoneFact 语义、学习手机边界、NOT_PRESENT/UNKNOWN 空 level 与长时升级均不变。
2. 新纯单测构造 `ReminderInput`，覆盖固定顺序：非 FOCUSING、非 Active/空 level、坏 session/freshness/episode 均 suppressed。
3. 覆盖新 episode FIRST、同 episode 升级 ESCALATION（无视旧级 cooldown）、同级 REPEAT；ledger key 为 episode+level+kind。
4. 覆盖默认 L1/L2/L3 90/45/20 秒，只有 REPEAT 乘 clamp 后 profile `0.75–1.50`，ACK 再乘 3；FIRST/ESCALATION 不乘画像。
5. 输出断言包括 reasons、episode、cooldown remaining、channels、policy/profile version；用 fake monotonic clock，无 sleep。
6. 运行并确认 Red：

```powershell
cmake --build build-national-host --config Debug --target test_reminder_decision_engine test_distraction_fusion -j 4
ctest --test-dir build-national-host -C Debug -R 'test_(reminder_decision_engine|distraction_fusion)' --output-on-failure
```

**Green**

- 实现无 BLE/SQLite 的决策器及 ledger 值对象；冷却从主机配置注入。
- 直接消费 `session::DistractionAssessment.level/reasons/episode`，不调用 `RiskScorer`，不 level→risk→level。
- channel 只从冻结 profile 偏好与 available channels 交集选择；安全等级不因画像降级。
- 重跑聚焦测试。

**Refactor / verification**

- 表驱动 decision kind、等级与 ACK；确认确定性相同输入产生相同输出。
- Commit point: `feat(host): add deterministic reminder decision engine`。

### Task H2.2：把 `ReminderPolicy` 收窄为投递、ACK 与日志外壳

**Files**

- Modify: `policy/ReminderPolicy.h`
- Modify: `policy/ReminderPolicy.cpp`
- Modify: `policy/NotificationRouter.h`
- Modify: `policy/NotificationRouter.cpp`
- Modify: `session/SessionDomain.h`
- Modify: `tests/test_reminder_policy.cpp`
- Modify: `tests/test_notification_router.cpp`
- Modify: `tests/test_integration.cpp`

**Red**

1. 先将现有 policy 样例改为从 assessment 驱动，断言旧合法提醒等级/渠道仍一致、非 applicable 无回归。
2. 测试一次 decision 只投递/持久化一次；日志含原始 reasons、decision kind、policy/profile version；ACK 只更新当前 episode ledger。
3. 测试 profile 读取失败使用 1.0 并记录 degraded；Level 3 escalation 仍立即投递。

**Green**

- 删除 policy 内 `base_cooling/current_cooling/last_level` 的二次推断状态，委托 decision engine；保留 `NotificationRouter` 和存储日志职责。
- 把可靠事件 ACK 映射为精确 episode/event，不改长期 profile。
- 更新 `ReminderSnapshot` 仅暴露决策结果，不伪造概率。

**Refactor / verification**

```powershell
ctest --test-dir build-national-host -C Debug -R 'test_(reminder_policy|notification_router|integration|distraction_fusion)' --output-on-failure
```

- Commit point: `refactor(host): make reminder policy a delivery shell`。

---

## H3：Personalization 与 Planner

### Task H3.1：纯 `PersonalizationEngine`

**Files**

- Create: `personalization/PersonalizationProfile.h`
- Create: `personalization/PersonalizationEngine.h`
- Create: `personalization/PersonalizationEngine.cpp`
- Create: `tests/test_personalization_engine.cpp`
- Modify: `tests/CMakeLists.txt`

**Red**

- 表驱动三值映射 `too_much=+1/just_right=0/too_little=-1`，非法旧枚举拒绝。
- 覆盖最近 8 场、最少 3 场、正式至少 5 分钟、排除 prepare_failed/预检取消、同 session 最新反馈。
- 精确断言 balance、target clamp 0.75–1.50、每次 move_towards 最大 0.10、confidence `min(n/8,1)`。
- 覆盖 digest 未变不更新；disabled 仍保留反馈但 effective multiplier=1.0；reset 返回默认参数和 `USER_RESET`，不删除历史。

```powershell
cmake --build build-national-host --config Debug --target test_personalization_engine -j 4
ctest --test-dir build-national-host -C Debug -R '^test_personalization_engine$' --output-on-failure
```

**Green**

- 实现纯函数 `recompute(oldProfile, latestValidFeedback)` 和稳定排序/摘要；不允许其输入或输出视觉事实/Fusion 配置。
- 重跑测试。

**Refactor / verification**

- 对输入顺序置换测试 digest/输出一致；Commit point: `feat(host): add bounded personalization engine`。

### Task H3.2：`PersonalizationService` 的版本、偏好、reset 和原子协调

**Files**

- Create: `personalization/PersonalizationService.h`
- Create: `personalization/PersonalizationService.cpp`
- Modify: `storage/SQLiteStore.h`
- Modify: `storage/SQLiteStore.cpp`
- Create: `tests/test_personalization_service.cpp`
- Modify: `tests/CMakeLists.txt`

**Red**

- 测试首次默认 profile、3/8 样本版本变化、重复/修改反馈、disabled、偏好 channels/目标 10–60、confirm reset、存储失败整体回滚。
- 断言每个实际变化都有 old/new/value/digest/reason event；无变化没有新 version。

**Green**

- service 读取最新有效反馈，调用纯 engine，并通过 H1 单一事务协调保存；profile id 永远 `default`。
- 提供 `snapshotForSession()` 供进入 FOCUSING 冻结；失败返回 degraded 默认 1.0。

**Refactor / verification**

```powershell
ctest --test-dir build-national-host -C Debug -R 'test_(personalization_service|personalization_engine|storage)' --output-on-failure
```

- Commit point: `feat(host): persist versioned personalization profile`。

### Task H3.3：纯 `AdaptiveTrainingPlanner`

**Files**

- Create: `training/TrainingPlan.h`
- Create: `training/AdaptiveTrainingPlanner.h`
- Create: `training/AdaptiveTrainingPlanner.cpp`
- Create: `tests/test_adaptive_training_planner.cpp`
- Modify: `tests/CMakeLists.txt`

**Red**

1. 测最近 5 场、少于 3 场 BASELINE（默认 15–30 展示范围但输出采用用户默认）。
2. 测 STABILIZE 的任一条件：最近三场完成率中位 `<.80`、高风险中位 `>.15`、至少两场早退；保持/减 5，最低 10。
3. 测 EXTEND：三场 completion 均 `>=.90`、focus efficiency 中位 `>=.80`、high risk 中位 `<=.08`、未达目标；只加 5。
4. 测 MAINTAIN：达目标或稳定但不满足延长；推荐 10–60、5 分钟步进、每场变化不超 5，休息 `<45 =>5`、`>=45 =>10`。
5. 测主要来源只比较 PRONE/PHONE_ENTERTAINMENT/PHONE_OTHER，phone learning 不独立成为主因；reason code/summary、session ids、profile/planner version 完整。
6. 测相同输入 digest 不生成第二计划、用户 5–120 覆盖不拒绝且记录 ADJUSTED。

**Green**

- 实现纯 planner、稳定 median/digest、稳定 reason enum；计划包含 ID、有效期、版本和可解释文本。
- 不读取 SQLite，不复制到协议。

**Refactor / verification**

```powershell
ctest --test-dir build-national-host -C Debug -R '^test_adaptive_training_planner$' --output-on-failure
```

- Commit point: `feat(host): generate deterministic adaptive training plans`。

### Task H3.4：`TrainingPlanService` 与三个重算触发点

**Files**

- Create: `training/TrainingPlanService.h`
- Create: `training/TrainingPlanService.cpp`
- Modify: `session/SessionOrchestrator.h`
- Modify: `session/SessionOrchestrator.cpp`
- Modify: `network/AppCommandService.h`
- Modify: `network/AppCommandService.cpp`
- Modify: `app/AppContext.h`
- Modify: `app/AppContext.cpp`
- Create: `tests/test_adaptive_training_service.cpp`
- Modify: `tests/test_session_runtime.cpp`
- Modify: `tests/test_ble_gatt.cpp`
- Modify: `tests/CMakeLists.txt`

**Red**

- 测 EDGEFOCUS session end、反馈变化、LOCAL changed upload 三个触发点；同 digest no-op。
- 测生成失败保留上一 active plan 并 stale；无计划不阻止手动开始。
- 测 feedback transaction 同时返回/保存 profile 与 active next plan；任何一步失败全回滚。
- 测开始计划存在性与 adoption：有效采用 ADOPTED；手动时长或 stale plan 记录 ADJUSTED/STALE_PLAN 但不拒绝。

**Green**

- service 读取最近有效 summary/profile，调用 planner 并持久化 active plan；将三触发点接到同一入口。
- `AppContext` 组合 service；`SessionOrchestrator` 只冻结/通知，不持有跨场算法。

**Refactor / verification**

```powershell
ctest --test-dir build-national-host -C Debug -R 'test_(adaptive_training_service|adaptive_training_planner|session_runtime|ble_gatt|storage)' --output-on-failure
```

- Commit point: `feat(host): wire adaptive recomputation triggers`。

---

## H4：协议、兼容与文档

### Task H4.1：协议合同 fixture 先行

**Files**

- Modify: `network/AppCommandService.h`
- Modify: `network/AppCommandService.cpp`
- Modify: `tests/test_ble_gatt.cpp`
- Modify: `tests/test_app_event_outbox.cpp`
- Create: `tests/data/adaptive_training_protocol_v1.json`

当前只读调用链已冻结：`AppCommandService::handleCommand()` 直接路由到 `AppCommandService::handleUploadLocalSession()`；LOCAL JSON 字段读取、校验、`sessionExists()` 幂等判断及 `SQLiteStore` 写入都在 `network/AppCommandService.cpp`，声明在 `network/AppCommandService.h`。因此本任务精确修改上述两个 AppCommandService 文件；`network/PhoneSyncParser.h` 不参与此命令，明确无需修改。

**Red**

1. hello 断言 server capability 新增 `adaptive_training_v1`，旧 capability 不消失。
2. fixture 精确断言：
   - `get_current_state.adaptive` 的 enabled/profile_version/training_plan；
   - `session_control(start)` 的 duration/plan id/adoption，stale 不拒绝；
   - `session_feedback` 仅三值，原子响应 `profile_update + next_training_plan`；
   - `update_preferences` 三字段、`get_preferences` 返回权威值；`reset_personalization` 必须 `confirm=true`；
   - LOCAL 全字段及 `PHONE_ONLY`、幂等覆盖。
3. 每个响应断言 request id；未知/缺字段错误不改变存储。
4. 老 client hello 不声明 adaptive 时仍可 start/pause/end/get state，响应增量可忽略。

**Green**

- `AppCommandService` 只校验 JSON/路由 service；序列化集中 helper，不写画像/计划算法。
- `get_current_state` 重连时总返回权威 profile/plan；报告/history 返回 summary、版本、plan、feedback、下一建议。
- 所有写命令失败返回明确 error，确保 App 保留 Outbox。

**Refactor / verification**

```powershell
cmake --build build-national-host --config Debug --target test_ble_gatt test_app_event_outbox -j 4
ctest --test-dir build-national-host -C Debug -R 'test_(ble_gatt|app_event_outbox)' --output-on-failure
```

- 导出 fixture 给 App 计划的 parser/codec 测试共用；Commit point: `feat(host): expose adaptive training protocol v1`。

### Task H4.2：协议和架构文档

**Files**

- Modify: `docs/integration/app-host-alignment-v3.md`
- Modify: `docs/architecture/current-architecture.md`
- Create: `docs/integration/adaptive-training-protocol-v1.md`

**Red（文档契约检查）**

```powershell
$doc = Get-Content -Raw -Encoding UTF8 docs/integration/adaptive-training-protocol-v1.md
'adaptive_training_v1','session_feedback','too_much','too_little','just_right','upload_local_session','reset_personalization','STALE_PLAN' | ForEach-Object { if ($doc -notmatch $_) { throw "missing $_" } }
```

第一次因文件不存在失败。

**Green**

- 写 capability、全部 request/response/error、三值 enum、版本权威、幂等键/覆盖语义、LOCAL `PHONE_ONLY`、降级/旧 App 兼容、事务失败重试。
- architecture 标明三个纯算法模块、跨会话 service、SQLite 事务和 FOCUSING 版本冻结；明确 G431/视觉无变化。
- 重跑契约检查。

**Refactor / verification**

- 用 `tests/data/adaptive_training_protocol_v1.json` 逐字段人工对照文档；Commit point: `docs(host): document adaptive training v1 contract`。

---

## 最终主机回归与验收

1. 全量构建测试：

```powershell
cmake -S . -B build-national-host -DEDGEFOCUS_BUILD_TESTS=ON -DEDGEFOCUS_WITH_OPENCV=OFF -DEDGEFOCUS_WITH_STAI_MPU=OFF
cmake --build build-national-host --config Debug -j 4
ctest --test-dir build-national-host -C Debug --output-on-failure
```

2. 重点重复运行确定性/幂等测试：

```powershell
ctest --test-dir build-national-host -C Debug -R 'test_(storage|session_summary_accumulator|reminder_decision_engine|personalization|adaptive_training|ble_gatt)' --repeat until-fail:20 --output-on-failure
```

3. 集成模拟：3 场有效会话离开 BASELINE；连续 too_much 每次 multiplier 至多 +0.10；覆盖同场反馈不重复；三场稳定仅 +5 分钟；LOCAL 同 session 重传仅一次学习；事务注入失败后三张状态均不变。
4. 安全回归：Fusion/temporal/precheck/session/reminder/integration 测试通过；确认 NOT_PRESENT/UNKNOWN、学习手机、PRECHECK/PAUSED 和 Level 3 边界未改变。
5. 新 DB 验收：在临时空目录启动建库成功；国赛部署 runbook 明确备份并删除旧测试 DB，不声称支持旧库迁移。
6. 联调前把协议 fixture/文档交给 App；主机未声明 capability 时 App 必须走旧流程。
7. 检查变更范围：

```powershell
git status --short
git diff --check
git diff --name-only | Select-String -Pattern 'firmware|platform/gimbal|G431' | ForEach-Object { throw "G431 scope violation: $_" }
```

## 覆盖追踪

- H1 domain/schema/事务：H1.1–H1.2；summary/report/history/LOCAL：H1.3–H1.4。
- H2 `DistractionAssessment` 直入、FIRST/升级/重复/ACK/版本/渠道、移除 risk round-trip：H2.1–H2.2。
- H3 Personalization 三值、3/8、0.10、有界、disabled/reset/digest：H3.1–H3.2；Planner 四阶段、5 场/3 场、步进/边界/原因：H3.3–H3.4。
- H4 capability、state/start/feedback/preferences/reset/LOCAL/report、旧 App、协议/架构文档：H4.1–H4.2。
- 非目标与安全门：开头硬边界及最终回归第 4、7 项。

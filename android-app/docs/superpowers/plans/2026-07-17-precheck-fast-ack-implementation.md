# PRECHECK 快速确认与崩溃防护实施计划

对应规格：`docs/superpowers/specs/2026-07-17-precheck-fast-ack-and-recovery-design.md`

## 任务 1：主机启动事务数据契约

- 修改 `session/SessionDomain.h`：增加 preparation state、operation id、generation、失败时间和错误字段。
- 修改 `session/SessionSnapshotPublisher.*`：begin 时发布 INITIALIZING，提供 ready/degraded/failure 更新，并在下一次 start 清理旧失败。
- 修改 `network/AppCommandService.cpp`：在稳定 runtime snapshot 输出 preparation 字段；start 读取并回显 operation_id。
- 扩展 `tests/test_session_runtime.cpp` 和相关协议测试，先覆盖字段和旧状态兼容。

## 任务 2：主机快速 ACK 与异步 prepare

- 修改 `session/SessionOrchestrator.*`：增加 C++17 单 worker、内部 lifecycle/generation/cancel 状态。
- `start()` 只分配会话并发布 PRECHECK/INITIALIZING，耗时 hook/factory/prepare/enterPhase 在 worker 中执行。
- worker 提交结果前校验 generation；stop、join、hook 均在主锁外执行。
- INITIALIZING 计入 active；duplicate start 幂等；confirm 在未 READY 时拒绝。
- end/shutdown 执行 cancel-and-join 屏障，完成前拒绝新 start，线协议不暴露 ENDING。
- 增加慢 prepare 快速返回、重复 start、初始化 confirm、取消、失败、析构和 hook-once 测试。

## 任务 3：App 权威启动 reducer

- 修改 `BoardJsonParser.kt`：解析 preparation、operation id、generation 和失败时间；旧主机 PRECHECK 缺字段默认 READY。
- 增加纯 Kotlin `StartTransactionPolicy/Reducer` 及测试，定义 ACK、轮询、失败、deadline、旧 operation 和 session 门控。
- 修改 `BoardJsonParser.buildSessionControl`/`BleManager.sessionControl`，start 携带 operation_id。
- 修改 `TimerStateHolder.kt`：一次 start + 30 秒有界核对，删除缺 phase 时推断 RUNNING 的分支；只在 ConnectionSupervisor READY 后核对，断连保留 deadline。
- 统一 ACK、主动状态和 push 的主机快照应用入口。

## 任务 4：PRECHECK 页面实时准备状态

- 将 preparation 字段映射到 `SharedTimerState`、Home/ControlPanel UI state。
- INITIALIZING 时保留原 PRECHECK 页面布局，显示准备中并禁用继续/跳过；READY/DEGRADED 恢复原操作。
- FAILED 返回首页并显示主机错误；不增加冗余“主机权威”标签。

## 任务 5：预览与 BLE 分包崩溃防护

- 修改 `BleChunkAssembler.kt`：8192 chunks、768 KiB、4 pending、128-byte chunk 上限；元数据一致性、失败清理、最老淘汰及 O(n) 组装。
- 扩展 `BleChunkAssemblerTest.kt` 覆盖所有拒绝和清理边界。
- 修改 `BleManager.kt`：大响应日志只保留类型/id/字节数，不记录 Base64；预览异常受控。
- 修改 `TimerStateHolder.kt`：预览周期 1200 ms、单在途、phase/session 门控及解析异常隔离。
- 增加共享预览解码器：支持当前 GRAY8/base64 与受限 JPEG/base64，在后台转换 Bitmap，Compose 只渲染结果。

## 任务 6：文档、构建与交付

- 更新主机 `docs/architecture/current-architecture.md` 和协议文档。
- 运行 Host 原生测试与构建；运行 App JVM 测试、lint 和 debug APK 构建。
- 检查双仓库 diff，保留用户已有未提交文件。
- 产出新 APK；若实机连接 ADB，覆盖安装并抓取启动/PRECHECK/重连日志。

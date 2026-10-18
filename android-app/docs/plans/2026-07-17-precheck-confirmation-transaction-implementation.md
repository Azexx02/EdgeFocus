# 预检确认事务可靠性实现计划

设计依据：`docs/superpowers/specs/2026-07-17-precheck-confirmation-transaction-design.md`

## 1. 先建立事务与决策测试

- 修改 `ManualGimbalTransactionCoordinatorTest.kt`：把“繁忙时假成功”改为明确失败，并增加普通命令执行中确认会等待且只执行一次的测试。
- 新增纯 Kotlin 的 `PrecheckConfirmationPolicyTest.kt`，覆盖直接 RUNNING、位置确认响应丢失后 holding/manual lease 判断、仍可手动时停止、会话变化、旧 PRECHECK 不得覆盖 RUNNING。
- 扩展 `SequencePolicyTest.kt`，覆盖本地检查点丢失/偏小、Unix 毫秒基线和系统时间回拨，确保首次云台命令不会被主机判为陈旧。
- 先运行目标单测，确认新测试在实现前失败。

## 2. 重构共享预检事务协调

- 修改 `ManualGimbalTransactionCoordinator.kt`：使用协程互斥量串行化命令；普通命令繁忙时返回 Busy，确认命令等待当前事务结束。
- 修改 `PrecheckController.kt`：在单例层建立独立生命周期的共享确认任务和 `StateFlow<Boolean>`；两个页面重复触发复用同一任务，ViewModel 销毁不取消任务。
- 确认开始时停止排队预览，拒绝新的普通预检操作，并在 `finally` 中释放共享确认状态。
- 修改 `ProtocolCheckpointStore.kt` 的云台序号分配，使用本地水位、主机水位与 Unix 毫秒的最大值递增，修复实机已确认的 `stale_command_sequence`。

## 3. 改为响应驱动并补齐不确定结果对账

- 修改 `PrecheckController.kt`：保留位置确认与 `skip_precheck` 各一次发送；直接解析命令响应中的业务结果、session ID 和 phase。
- 新增 `PrecheckConfirmationPolicy.kt`：集中判断位置确认对账和正式会话对账结果。
- 位置确认传输结果不确定时只读取一次状态：holding/manual lease 已释放则继续，仍有租约则恢复重试，RUNNING 则成功。
- `skip_precheck` 已响应 RUNNING 时直接完成；响应不确定时先检查共享状态，再最多读取一次主机状态。
- 删除原 10 秒、250 ms 的完整状态轮询循环。

## 4. 统一应用权威响应并阻止阶段回退

- 修改 `TimerStateHolder.kt`：增加复用现有解析/接管规则的命令响应应用入口。
- 同 session 已进入 RUNNING/FOCUSING 后拒绝晚到的 PRECHECK 快照，避免界面回退。
- 补充对应纯策略或状态测试，确保新 session 仍可正常进入 PRECHECK。

## 5. 同步两个预检页面的确认状态

- 修改 `HomeViewModel.kt` 与 `ControlPanelViewModel.kt`：观察单例控制器的确认 StateFlow。
- 修改 `HomeScreen.kt` 与 `ControlPanelScreen.kt`：确认期间按钮显示“正在进入专注…”，并禁用确认、跳过、微调、回中、停止、重新对准和预览刷新；不改变布局和风格。

## 6. 验证与交付

- 运行预检相关目标单测，再运行 APP 全量单元测试和 `assembleDebug`。
- 用无线 ADB `192.168.10.59:33941` 覆盖安装 APK。
- 实机重点复现“微调后立即确认”“预览传输中确认”“快速重复确认”，并循环执行预检到正式会话；读取 logcat 验证没有重复命令、轮询风暴、崩溃或 PRECHECK 回退。
- 测试结束后恢复设备到无活动会话状态，并交付 APK 路径、测试结果与剩余风险。

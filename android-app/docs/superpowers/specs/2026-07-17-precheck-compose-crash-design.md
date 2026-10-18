# PRECHECK Compose 闪退最小修复设计

## 问题与证据

Redmi Note 10 Pro（MIUI 14 / Android 13）在会话进入 PRECHECK，以及 APP 重启后恢复 PRECHECK 时稳定闪退。实机 `logcat` 显示主线程异常：

`ArrayIndexOutOfBoundsException -> androidx.compose.runtime.Stack.pop -> ComposerImpl.exitGroup`

首页在 `Column` 内容中以 `return@Column` 切换到预检内容，控制面板计时页在 `@Composable` 中直接 `return`。PRECHECK 快速状态更新会让当前 Compose 运行时在重组期间进入不稳定的分组退出路径。

## 方案

保持现有首页、横向控制面板和预检 UI 不变，只把两处提前返回改为结构化条件分支：

- PRECHECK：组合预检页面。
- 非 PRECHECK：在对应 `else` 分支组合原计时页面。
- 不使用 `return` 或 `return@Column` 中断 Composable 内容。

不升级 Compose，不拆分 Activity，不改变主机协议和 PRECHECK 状态机。

## 验证

1. 增加一个纯策略测试，覆盖 PRECHECK 与普通计时内容的互斥选择。
2. 运行 `testDebugUnitTest`、`lintDebug` 和 `assembleDebug`。
3. 覆盖安装到已连接手机。
4. 清空 `logcat` 后执行最小实机矩阵：
   - 首页进入 PRECHECK，并从 PRECHECK 转入 RUNNING。
   - 首页重启后恢复 PRECHECK，并从 PRECHECK 转入 IDLE。
   - 横向控制面板进入 PRECHECK，并从 PRECHECK 转入 RUNNING。
   - 横向控制面板重启后恢复 PRECHECK，并从 PRECHECK 转入 IDLE。
5. 全程检查 `AndroidRuntime`；所有路径均不得崩溃，且原 UI 与交互保持不变。

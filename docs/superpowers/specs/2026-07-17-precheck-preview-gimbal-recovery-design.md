# PRECHECK 预览与云台恢复设计

## 实机证据

- 主机保持 `runtime_snapshot.session.phase = PRECHECK`、`preparation_state = READY`，APP 当前不再闪退。
- APP 连续发送 `get_current_state`，手动点击刷新后 `get_camera_preview_image` 仍未获得发送机会。
- 原因是每次状态响应进入 `applyBoardSessionState()` 后再次调用 `startBoardSync()`；该方法取消当前同步协程并立刻创建新协程，使正常轮询延迟失效并饿死低优先级预览请求。
- 主机云台状态位于 `runtime_snapshot.gimbal`，且 `available = true`；响应中没有 `precheck.gimbal`，导致 APP 隐藏云台控制区。

## 最小方案

1. `startBoardSync()` 改为幂等：已有活动同步任务时直接返回，不再取消重启。只有断线、会话结束或显式停止时取消。这样保留轮询间隔，并让预览请求获得调度机会。
2. `BoardJsonParser` 在 `precheck.gimbal` 缺失时，从 `runtime_snapshot.gimbal` 构造 `PrecheckGimbal`：
   - `enabled/online` 由 `available` 与 `health_status` 推导；
   - `safe` 要求可用、未 degraded，且 health 为 ready；
   - 角度沿用 `angle_x/angle_y`。
3. 不更改主机协议，不调整首页或横向控制面板 UI。

## 验证

- 单元测试覆盖同步任务幂等策略及 runtime gimbal fallback。
- 运行 `testDebugUnitTest`、`lintDebug`、`assembleDebug`。
- 覆盖安装后进入 PRECHECK：日志应出现 `get_camera_preview_image`，页面应显示照片和云台控制。
- 强制结束并重启 APP，恢复 PRECHECK 后重复验证；全程不得出现 `AndroidRuntime` 崩溃或自动退出 PRECHECK。


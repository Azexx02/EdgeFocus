# EdgeFocus Android App：云台手动控制体验与 BLE 协议适配实施文档

版本：1.0  
日期：2026-07-19  
适用仓库：EdgeFocus Android App  
接口对象：通过 BLE JSON 与 EdgeFocus 主机通信，不直接解释 G431 串口协议

---

## 1. 文档目的

App 的职责是：

- 将用户的云台操作可靠地表达给主机；
- 展示主机权威状态；
- 不丢失用户连续输入；
- 不因一个运动请求未物理到位而阻塞 UI 数秒；
- 不把主机业务拒绝显示成“硬件坏了”；
- 提供清楚的恢复、停止、当前位置确认和前向校准入口。

当前 App 手动体感差的主要原因包括：

- `ManualGimbalTransactionCoordinator` 使用 `tryLock()`，事务期间的快速输入被故意丢弃；
- 每个方向操作等待主机同步执行最长数秒；
- BLE 请求队列只能取消尚未执行的预览，正在执行的 20 秒预览仍会阻塞高优先级控制；
- App 只发送方向，不发送实际步进，主机又硬编码 30°；
- App 只允许 PRECHECK 手动，而主机内部还存在 FOCUSING 手动路径，权限语义不一致；
- “重新对齐”按钮可能触发主机重新 ARM/电气校准，使位置参考再次变化；
- 状态模型不能区分未到位、通信退化、命令被拒绝和真实硬件 Fault。

---

## 2. App 必须遵守的硬约束

1. App 不维护独立的真实云台角度，主机状态是唯一权威；
2. App 可以做乐观 UI，但必须使用主机返回的目标和 generation 对账；
3. 用户连续点击不得直接丢弃；允许合并成净增量；
4. `gimbal_nudge` 返回 acceptance 后即结束该 BLE 事务，不等待物理 settled；
5. `gimbal_stop` 必须拥有高于预览和普通 nudge 的优先级，并清除本地待发送增量；
6. 不再把 `gimbal_realign` 展示为“重新对齐”；新 UI 使用“恢复云台”；
7. 普通 PRECHECK“确认位置”不得让用户误以为永久设置了正前方；
8. 永久前向原点必须是独立维护流程，并有明确二次确认；
9. 不得在主机版本不支持新能力时静默回退到会随机归零的旧流程；
10. 不新增无关的手机心跳或屏幕亮度推断逻辑，本次只修改云台交互。

---

## 3. 主机能力协商

`HostJsonCodec.clientHello()` 和解析模型增加识别主机能力：

```text
gimbal_async_motion_v1
gimbal_origin_v1
gimbal_fault_class_v1
gimbal_manual_step_v1
```

行为：

| 能力 | App 行为 |
|---|---|
| 全部存在 | 启用新手动控制、状态显示和前向校准 |
| 缺少 async motion | 禁止长按连续控制；提示主机需升级，不使用旧的长阻塞体验 |
| 缺少 origin | 隐藏“设置正前方”入口 |
| 缺少 fault class | 兼容显示通用错误，但不得自行推断为硬件 Fault |

建议 `HostJsonCodec.clientCapabilities` 增加：

```text
gimbal_manual_input_v2
```

---

## 4. BLE JSON 请求修改

### 4.1 `gimbal_nudge`

`HostJsonCodec.gimbalCommand()` 增加 `stepDeg` 参数：

```json
{
  "cmd": "gimbal_nudge",
  "id": 124,
  "data": {
    "session_id": "session-uuid",
    "sequence": 18,
    "direction": "LEFT",
    "step_deg": 3.0
  }
}
```

App 侧约束：

- 短按默认 `3.0°`；
- 合法范围建议 `1.0..10.0°`；
- 不再依靠主机隐藏的 30°常量；
- 不将“步进越大”当作修复小步进不动的方法；底层积分问题由 G431 修复。

### 4.2 `gimbal_stop`

- 高优先级；
- 清空 App 本地 pending delta；
- 主机返回 captured current/target；
- 保留 manual lease；
- UI 立即显示“已请求停止”，随后以主机 settled 状态确认。

### 4.3 `gimbal_center`

移动到持久化前向中心。请求仍带 session_id 和 sequence。App 不自行假设中心角度。

### 4.4 `precheck_confirm_position`

UI 文案应改成“确认当前构图”或“使用当前位置”，不得叫“设置零点”。

主机响应中的 `origin_changed` 必须为 false。App 不应因此更新任何永久校准状态。

### 4.5 `gimbal_recover`

新增命令：

```json
{
  "cmd": "gimbal_recover",
  "id": 128,
  "data": {
    "session_id": "session-uuid",
    "sequence": 22
  }
}
```

旧 `gimbal_realign` 只作为兼容别名，新 App 不再发送。按钮文案为“恢复云台”，说明为“重新连接并恢复保持，不改变正前方设置”。

### 4.6 `gimbal_capture_forward`

独立维护页使用：

```json
{
  "cmd": "gimbal_capture_forward",
  "id": 130,
  "data": {
    "sequence": 4,
    "confirm": true
  }
}
```

要求：

- 只在主机 IDLE/维护状态显示；
- 用户先用手动控制将摄像头对准正前方；
- 二次确认文案明确“将当前位置保存为以后启动和搜索的正前方”；
- 成功展示 `origin_epoch`；
- 普通 PRECHECK 页面不自动调用。

---

## 5. 响应和状态模型

### 5.1 成功 acceptance

手动运动成功不代表到位。解析模型必须包含：

```kotlin
data class GimbalCommandAcceptance(
    val accepted: Boolean,
    val operation: String?,
    val sequence: Long?,
    val motionGeneration: Long?,
    val state: String?,
    val settled: Boolean?,
    val manualLease: Boolean?,
    val currentYawDeg: Double?,
    val currentPitchDeg: Double?,
    val targetYawDeg: Double?,
    val targetPitchDeg: Double?,
    val errorCode: String? = null,
    val retryable: Boolean = false
)
```

`HostJsonCodec.parseBusinessAcceptance()` 应扩展或新增专用解析器，不能只得到 `accepted/error` 两个字段。

### 5.2 权威 Gimbal 状态

`BoardJsonParser`、`HostProtocolModels` 和 UI state 增加：

- `state`；
- `controlMode`；
- `communicationHealth`；
- `hardwareFaultActive`；
- `hardwareFaultCode`；
- `lastCommandErrorCode`；
- `lastCommandErrorRetryable`；
- `currentYawDeg/currentPitchDeg`；
- `targetYawDeg/targetPitchDeg`；
- `motionGeneration`；
- `settled`；
- `calibrationValid`；
- `originValid`；
- `calibrationEpoch/originEpoch`；
- `manualLease`；
- `detail`。

旧 `angle_x/angle_y` 仅作为兼容 fallback。

### 5.3 UI 状态解释

| 状态 | UI 表现 |
|---|---|
| accepted + moving | 方向按钮保持可用，显示轻量“移动中”而不是全屏阻塞 |
| settled | 显示已到位 |
| COMMAND error | 显示具体原因，不显示硬件故障 |
| communication degraded | 显示连接不稳定，可重试 |
| hardware fault | 显示真实故障码和恢复入口 |
| calibration required | 引导维护校准，不允许自动搜索 |
| origin required | 引导设置正前方，不允许自动搜索 |

---

## 6. 手动输入协调器重构

### 6.1 删除“事务期间输入直接丢弃”

当前 `ManualGimbalTransactionCoordinator.runOrdinary()`：

```kotlin
if (!transactionMutex.tryLock()) {
    return Result.failure(IllegalStateException("预检操作正在执行，请稍候"))
}
```

该行为必须移除。对应测试中“第二次快速输入应失败”的断言必须删除并改成“连续输入最终全部体现在净目标中”。

### 6.2 使用单消费者 actor/队列

建议新增 `ManualGimbalInputCoordinator`：

```kotlin
data class PendingDelta(
    var yawDeg: Double = 0.0,
    var pitchDeg: Double = 0.0
)
```

行为：

1. 每次短按只更新 pending delta；
2. 单个 worker 串行发送；
3. 同轴同方向合并；
4. 反方向相互抵消；
5. 前一请求 ACK 后立即发送剩余净增量；
6. 不等待主机 physical settled；
7. pending 可设置合理上限，例如每轴 ±15°，防止离线期间积累巨大动作；
8. sessionId、manual lease 或 generation 改变时清空 pending；
9. `gimbal_stop` 立即清空 pending 并抢占发送；
10. 断连时清空，不在重连后重放旧动作。

可以继续使用 per-session `sequence`，每次真正发送时分配新 sequence。

### 6.3 租约获取仍需串行

`precheck_skip_auto_adjust`/takeover 是状态切换，仍应串行并等待主机确认 `manual_lease=true`。但租约获得后，普通 nudge 不得再用同一个长事务互斥锁阻塞所有输入。

### 6.4 短按和长按

建议交互：

- 短按：3°；
- 长按：初次立即 3°，随后每 120～180 ms 累加 1.5°～2°；
- 松手：停止继续产生增量，不必每次都发送 `gimbal_stop`；
- 用户点击中央停止键才发送 `gimbal_stop`；
- App 根据 pending 和主机 target 提供方向反馈。

长按速率必须有上限，不能把 BLE 队列塞满。

### 6.5 乐观 UI 与对账

- acceptance 返回的 `target_*` 作为临时 UI 目标；
- `get_current_state` 中同 generation 或更新 generation 的 target 为权威；
- 若主机拒绝，回滚 pending 影响并显示错误；
- current angle 仅展示，不用于 App 自己计算下一目标。

---

## 7. BLE 调度器修改

### 7.1 云台命令优先级

建议：

- `gimbal_stop`：`P0_CRITICAL`；
- `precheck_skip_auto_adjust`、`gimbal_recover`：`P0_CRITICAL`；
- `gimbal_nudge`、`gimbal_center`、`precheck_confirm_position`：新增 `P1_GIMBAL_CONTROL`；
- 状态查询：`P2_STATE`；
- 预览：`P4_PREVIEW`。

若不新增枚举，普通云台控制至少使用 `P0_CRITICAL`，不能默认落入后台优先级。

### 7.2 正在执行的预览必须可取消

当前 `cancelPreviewRequests()` 只调用 `cancelQueued(P4_PREVIEW)`，无法中断已经运行、最长 20 秒的 preview。必须增加 active request 管理：

- scheduler 记录当前 request；
- 低优先级 preview operation 可被取消；
- 取消时移除对应 `pendingResponseCallbacks[requestId]`；
- 清理该请求的 chunk assembler 状态；
- 后续迟到的 preview chunks 必须丢弃，不能污染新请求；
- 高优先级 gimbal command 随后立即执行。

可新增：

```kotlin
suspend fun cancelQueuedAndActive(
    priority: HostRequestPriority,
    reason: String
)
```

### 7.3 超时

主机新协议在目标 ACK 后立即返回，因此：

- 云台命令响应超时建议 2～3 秒；
- 不再使用 12 秒来等待物理运动；
- preview 可保留较长超时，但必须可取消；
- 状态 reconciliation 在命令 ACK 丢失时使用 `get_current_state`，不要盲目重复同 sequence。

### 7.4 STOP 抢占

`gimbal_stop` 发送前：

1. 清空本地 nudge pending；
2. 取消 queued/active preview；
3. 取消尚未开始的普通 nudge；
4. 立即发送 stop；
5. 使用响应和状态查询对账。

---

## 8. PRECHECK 页面交互修改

### 8.1 自动搜索期间

- 显示“正在寻找人物”；
- 方向按钮可以提示“接管手动控制”；
- 用户点击任意方向时，可先执行 takeover，再把本次方向输入保留并发送，不能因为租约尚未获得而丢掉第一次点击；
- takeover 成功前不要重复发送多次 skipAuto。

### 8.2 手动模式

- 方向按钮不因一次 nudge 请求进入长时间 disabled；
- 显示 current、target 和 moving/settled；
- 中央按钮执行 `gimbal_stop`；
- “回正”执行 `gimbal_center`；
- “确认当前构图”执行 `precheck_confirm_position`；
- 不把“确认当前构图”和“设置正前方”放在同一按钮。

### 8.3 故障/恢复

错误展示分级：

- `MANUAL_LEASE_REQUIRED`：自动刷新状态或重新接管；
- `STALE_SEQUENCE`：重读状态，不显示硬件故障；
- `COMMUNICATION_DEGRADED`：提示连接不稳定；
- `HARDWARE_FAULT`：显示恢复按钮和具体 fault code；
- `CALIBRATION_REQUIRED`：进入维护流程；
- `ORIGIN_REQUIRED`：提示设置正前方。

“恢复云台”不得描述为“重新校准”，除非用户进入独立维护页并明确执行电气校准。

### 8.4 FOCUSING 手动控制

主机和 App 必须采用同一产品决定：

- 若只允许 PRECHECK，主机应删除/关闭 FOCUSING 手动入口；
- 若允许会话中临时调整，App 状态校验须允许 `FOCUSING + manual_control_available`，并提供明确接管/恢复自动跟踪按钮。

不得继续出现主机支持而 App 固定拒绝的状态漂移。建议由主机返回 `manual_control_available`，App 不根据 phase 自行猜测。

---

## 9. 前向原点维护页面

建议新增独立“云台校准”页面，状态来自主机：

- 电气校准是否有效；
- 前向原点是否有效；
- calibration/origin epoch；
- 当前角度；
- 当前是否 settled；
- 硬件 fault。

流程：

1. 要求会话处于 IDLE；
2. 若前向原点无效，先保持驱动关闭，由用户用手将摄像头置于机械正前方；不得临时创建软件零点来驱动绝对运动；
3. 执行 fast ARM，使固件在当前位置无扰保持；
4. 若已有旧原点且只是重新校准，可使用软件小步进微调；若原点无效，不依赖绝对角命令微调；
5. 用户点击“设为正前方”；
6. 二次确认；
7. 调用 `gimbal_capture_forward`；
8. 成功后重新读取状态并确认 origin epoch 增加；
9. 可提供“回到正前方”验证。

电气校准是更高风险操作，应单独入口、显著警告“云台会转动”，并允许用户急停。普通用户通常不需要频繁执行。

---

## 10. 文件级修改清单

### `data/ble/HostJsonCodec.kt`

- `gimbalCommand()` 增加 `stepDeg`；
- 增加 `gimbal_recover`、`gimbal_capture_forward`；
- 解析结构化 gimbal acceptance/error；
- 解析 server capabilities。

### `data/ble/model/HostProtocolModels.kt`

- 新增 Gimbal command、pose、health、fault 数据模型；
- 避免只用字符串 detail 表达全部状态。

### `data/remote/BoardJsonParser.kt`

- 解析 current/target/settled/generation；
- 解析 calibration/origin validity 和 epoch；
- 解析 command error、communication health、hardware fault；
- 保留旧字段 fallback，但新字段优先。

### `data/remote/BleManager.kt`

- 云台命令使用高优先级；
- 云台响应 timeout 调低；
- active preview 可取消；
- chunk assembler 按 request id 清理；
- `cancelPreviewRequests()` 可取消 active；
- 新增 `gimbalRecover()`、`captureForwardOrigin()`。

### `data/ble/BleRequestScheduler.kt`

- 支持 active request cancellation；
- 增加 `P1_GIMBAL_CONTROL` 或等价优先级；
- STOP 可以取消普通待执行云台请求；
- 保持 preview 防饥饿，但不能阻塞紧急控制。

### `domain/precheck/ManualGimbalTransactionCoordinator.kt`

- 移除 tryLock 丢输入；
- 租约与普通输入拆分；
- 可保留 takeover 的串行确认逻辑。

### 新增 `domain/precheck/ManualGimbalInputCoordinator.kt`

- 维护 pending yaw/pitch delta；
- 单 worker 发送；
- 合并、抵消、停止清空、断连清空；
- 对账主机 target/generation。

### `domain/precheck/PrecheckController.kt`

- nudge 传 `step_deg`；
- 首次方向点击可触发 takeover 后继续执行原点击；
- stop 清空 input coordinator；
- 使用新结构化错误；
- `realign` 改 `recover`；
- confirm 文案和语义调整；
- 不把每个 nudge 包在长互斥事务中。

### UI/ViewModel

- 按钮不因 moving 状态全部锁死；
- 显示 target/current/settled；
- 错误分级；
- 新增前向校准页；
- PRECHECK 与 FOCUSING 权限由主机字段决定。

---

## 11. App 测试要求

### 11.1 协议测试

- nudge 带 `step_deg`；
- 新旧字段解析；
- 结构化错误码；
- server capability gating；
- `gimbal_recover` 不再发送 `gimbal_realign`；
- `precheck_confirm_position` 不改变 origin 状态。

### 11.2 输入协调器测试

1. 10 次快速 LEFT 3°最终发送净 30°，不返回“操作正在执行”；
2. LEFT 5 次、RIGHT 3 次最终净 LEFT 6°；
3. nudge 请求尚未 ACK 时继续点击，pending 保留；
4. STOP 清空 pending，后续迟到 ACK 不重新触发旧动作；
5. 断连清空 pending，重连不重放；
6. sessionId 改变时清空；
7. takeover 的第一次方向点击不会丢失；
8. stale sequence 后刷新状态，不显示硬件故障。

### 11.3 BLE 调度测试

- active preview 被 stop/nudge 取消；
- 迟到 preview chunk 被丢弃；
- gimbal stop 不等待 20 秒 preview timeout；
- 高优先级控制不会被后台请求阻塞；
- 两个不同 request id 的 chunk 不混合。

### 11.4 UI/状态测试

- accepted/moving 时按钮仍可用；
- settled 正确显示；
- communication degraded 与 hardware fault 使用不同提示；
- origin/calibration required 引导不同页面；
- FOCUSING 权限依据主机字段；
- “确认当前构图”和“设为正前方”文案及功能完全分离。

### 11.5 真机联调

1. 当前镜头对人后开始会话，App 可看到主机先观察而不是立即乱转；
2. 连续快速短按方向，运动连续且不漏按；
3. 3°反向小步进可响应；
4. 点击 STOP 后云台快速冻结；
5. 搜索失败后 App 不看到重复初始化和新的随机方向；
6. 普通命令错误不显示“硬件故障”；
7. 设置正前方后断电重启，回正结果一致。

---

## 12. App 验收标准

- 快速连续输入不丢失；
- 单次方向操作不再阻塞 UI 数秒；
- preview 不再阻塞云台控制；
- 默认 3°短按可用；
- STOP 可抢占并清空待发送动作；
- current/target/settled 可见并以主机为权威；
- 错误类别展示正确；
- “恢复云台”不改变零点；
- “确认当前构图”和“设置正前方”完全分离；
- 主机能力不足时明确提示升级，不静默使用旧随机归零流程；
- 单元测试、调度测试和真机 BLE 联调记录随代码提交。

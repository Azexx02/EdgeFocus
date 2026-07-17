# PRECHECK 云台故障降级确认设计

日期：2026-07-18

## 背景

主机网页在正式会话处于 `PRECHECK` 时始终允许“确认预检”。如果云台拥有手动租约，网页先确认云台位置；其他任何状态（包括正常搜索中和故障降级）都会跳过位置确认并直接结束预检。APP 本次不会照搬网页的宽松条件，而是有意维持更严格的正常搜索保护，只为明确故障降级增加旁路。

APP 当前把所有 `manual_lease=false` 都解释为“自动搜索仍在进行”：首页和控制面板页禁用“确认并开始”，`PrecheckController` 即使被调用也会拒绝继续。云台明确返回 `state=degraded` 时，这个判断与主机网页语义不一致。

## 目标

- 云台正常自动搜索期间继续禁止“确认并开始”，避免误跳过正常调整。
- 云台搜索完成并取得手动租约后，保持现有“确认位置，再结束预检”流程。
- 云台明确处于故障降级状态时，允许用户以固定机位继续正式专注。
- 首页与控制面板页使用同一判定，避免入口行为不一致。
- 仅修改 APP；如果现有主机 `skip_precheck` 在实测中拒绝降级继续，则停止并报告，不修改主机。

## 非目标

- 不改变主机协议、网页逻辑或云台故障恢复机制。
- 不把普通离线、初始化中或未知状态自动视作可降级确认。
- 不允许用户在正常自动搜索过程中直接确认；用户仍可使用“跳过自动调整”取得手动控制权。

## 方案

### 统一判定策略

新增纯策略，严格按照以下顺序将预检云台状态归类。前面的规则优先，保证矛盾或陈旧字段不会产生歧义：

1. `NO_GIMBAL`：仅当 `gimbal == null`。保持现有可继续行为。
2. `DEGRADED`：`state` 忽略大小写后明确等于 `degraded`。可直接结束预检，不发送任何云台位置命令；即使同时带有陈旧的 `manual_lease=true`、`holding` 或 `position_confirmed`，降级仍优先。
3. `POSITION_CONFIRMED`：`manual_lease=false`，并且 `state=holding` 或 `detail=position_confirmed`。可直接结束预检。
4. `MANUAL_READY`：`manual_lease=true`。可确认，先发送 `precheck_confirm_position`。
5. `BLOCKED_UNREADY`：其余所有非空云台状态。不可确认；它可能是自动搜索、初始化、离线或未知状态，UI 可按具体字段显示更精确的说明，但不得旁路。

只有明确的 `state=degraded` 才进入故障旁路。`enabled=false`、`online=false`、`safe=false` 或 `manual_lease=false` 单独出现时仍不足以旁路，以免把解析器从 `runtime_snapshot.gimbal` 生成的不可用/初始化对象或短暂通信状态误认为无云台或故障降级。

### UI 行为

首页和控制面板页均通过统一策略计算按钮状态。策略只决定“云台是否允许确认”，最终按钮仍必须同时满足现有的 `preparationState in {READY, DEGRADED}` 和 `!isPrecheckConfirming`：

- `BLOCKED_UNREADY`：禁用“确认并开始”。
- `MANUAL_READY`、`POSITION_CONFIRMED`、`DEGRADED`、`NO_GIMBAL`：启用。
- `DEGRADED`：显示提示“云台已降级，将以固定机位继续”。
- 确认事务进行中继续禁用预览、云台操作和重复确认。

确认资格、手动控制资格和跳过自动调整资格分别派生，不复用一个布尔值：

- 方向键、回中和停止只在 `MANUAL_READY` 时启用。
- “跳过自动调整”只在云台状态明确为 `searching`、`centering` 或 `framing`、没有手动租约且不是降级时启用。
- `DEGRADED` 只启用“确认并开始”，不启用任何云台控制或“跳过自动调整”。

### 确认事务

`PrecheckController` 读取一次主机权威状态后使用同一策略：

- `MANUAL_READY`：发送位置确认，成功或校准确认后发送 `skip_precheck`。
- `POSITION_CONFIRMED`、`DEGRADED`、`NO_GIMBAL`：直接发送 `skip_precheck`。
- `BLOCKED_UNREADY`：返回明确错误，防止 UI 状态过期时绕过保护。

`skip_precheck` 的响应应用、单次状态校准、重复点击合并以及迟到 PRECHECK 防回退逻辑保持不变。

## 错误处理

- 若点击后状态从 `DEGRADED` 恢复为正常自动搜索，以控制器读取到的最新权威状态为准，不旁路。
- 若 `skip_precheck` 被主机拒绝，APP 显示主机错误并停留在预检页，不伪造正式会话状态。
- 若主机响应不确定，继续沿用现有一次校准策略。
- 若实测表明主机在云台降级时拒绝 `skip_precheck`，停止 APP 修改后的部署验证并报告需要主机修复。

本方案的安全保证基于 APP 点击后取得的最新权威快照。快照读取完成到 `skip_precheck` 被主机执行之间仍存在极短的 TOCTOU 窗口：若云台恰好从 `DEGRADED` 恢复为正常搜索，现有主机不会在执行时再次校验。完全消除此竞态需要主机提供原子“仅在仍降级时确认”语义，属于主机修改，本次按约束不实施。APP 会尽量缩短读取与发送之间的间隔，且不会基于旧 UI 快照直接旁路。

## 测试

### 单元测试

- 明确 `degraded` 且无租约：按钮可用，确认事务跳过位置确认。
- 正常搜索且无租约：按钮不可用，控制器拒绝确认。
- 手动租约：按钮可用，仍执行位置确认。
- 已确认位置：按钮可用，不重复确认位置。
- 无云台：保持可继续。
- 解析器产生的非空 `enabled=false` 云台对象：归入 `BLOCKED_UNREADY`，不得误当作 `NO_GIMBAL`。
- `enabled=false`、`online=false` 或 `safe=false` 但状态不是 `degraded`：不得误旁路。
- `degraded` 与陈旧 `manual_lease=true`/`holding`/`position_confirmed` 同时出现：仍归入 `DEGRADED`，不得发送位置确认。
- `holding` 或 `position_confirmed` 与 `manual_lease=true` 同时出现：归入 `MANUAL_READY`，保留位置确认。
- `preparationState` 为初始化或失败：即使云台降级，按钮仍不可用，控制器仍拒绝确认。
- APP UI 显示降级后、控制器读取权威状态时已恢复搜索：按 `BLOCKED_UNREADY` 拒绝，不发送 `skip_precheck`。

### 构建与实机

- 运行相关单元测试和完整 Debug 构建。
- 覆盖安装 APK。
- 在可复现云台降级时验证：按钮可点击、APP 不发送 `precheck_confirm_position`、主机接受 `skip_precheck` 并进入 `FOCUSING`。
- 回归正常云台流程，确保搜索期间仍禁用、搜索完成后仍先确认位置。

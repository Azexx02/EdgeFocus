# EdgeFocus APP 冷启动连接与主机广播恢复设计

## 背景与根因

覆盖安装 APK 会保留 Room 设置，因此已保存的 EdgeFocus 主机 MAC 不会丢失。当前问题来自两条未闭合的链路：

1. `EdgeFocusApp` 只启动 `ConnectionSupervisor`，但冷启动不会读取已保存 MAC，也不会发起扫描或直连。MAC 只在 `BleViewModel` 创建后加载，所以用户必须进入蓝牙连接页才可能连接；界面中的“下次自动连接”与实际行为不一致。
2. 主机 `BleGattServer` 在 GATT Application 注册成功后就把 `bluez_ready_` 设为 true。Advertisement 后续注册失败时不会再次尝试；收到 BlueZ `LEAdvertisement1.Release` 时也只返回成功，不更新状态或重新注册。异常断线后只能依靠重新部署或重启进程恢复广播。

远程主机在设计阶段无法通过 SSH 访问，因此第二项结论来自当前主机源码状态机审查，实施后仍需实机验证。

## 目标

- 覆盖安装或进程重启后，只要已保存主机 MAC、蓝牙已开启且权限已授予，APP 自动连接该主机。
- 冷启动首次连接失败后自动退避重试，不要求当前进程曾成功进入 READY。
- Android 运行时权限刚授予、蓝牙重新开启或主机稍后恢复时，连接链可继续恢复。
- 用户主动断开后停止自动连接，直到用户主动扫描、选择设备或明确重新连接。
- 主机 GATT 服务已注册但广播注册失败、被 BlueZ Release 或异常退订后，可自行恢复广播，不需要重新部署。
- 不改变现有 schema 2.1、会话权威、PRECHECK、NFC 控制面板和手环代理协议。

## 非目标

- 不在没有已保存 MAC 时自动选择附近第一台 EdgeFocus；首次配对仍由用户确认，避免连错设备。
- 不依赖 Android `connectGatt(autoConnect=true)` 的系统后台语义；仍使用现有显式 LE 连接和可观测重试。
- 不重启 bluetoothd，不重置适配器，不在 APP 提供主机复位入口。
- 不把主机广播恢复与手环搜索/重连混为同一状态机。

## APP 设计

### 1. 冷启动引导协调器

新增进程级 `BleAutoConnectCoordinator`，由 `EdgeFocusApp.onCreate()` 启动。它负责：

1. 读取 Room `Settings.edgeFocusMacAddress`；若为空，再读取旧 SharedPreferences `edge_focus_mac` 并迁移到 Room。
2. 将有效 MAC 写入 `BleManager.setTargetMac()`。
3. 更新共享的 `BleAutoConnectGate`，并仅在 Gate 允许时发起已知 MAC 的显式连接。
4. 初始化默认设置必须先完成或与引导过程串行，避免默认记录插入与 MAC 读取竞争。

协调器不拥有 GATT；实际连接仍由 `BleManager` 完成，业务 READY 仍由 `ConnectionSupervisor` 在 notification、`client_hello` 和 `get_current_state` 成功后发布。

`BleAutoConnectGate` 是所有自动连接和重试路径的唯一权威门。它只有在以下条件同时满足时返回允许：

- `useEdgeFocusBoard=true`；
- 已保存有效目标 MAC；
- 蓝牙适配器开启；
- 当前 Android 版本需要的 `BLUETOOTH_SCAN/CONNECT` 与位置权限已经授予；
- 持久化的 `userDisconnected=false`；
- 当前没有 CONNECTING、CONNECTED 或 SCANNING 操作。

协调器首次连接、`ConnectionSupervisor` 退避重试、权限回调、前台恢复和蓝牙开启事件都必须调用同一个 Gate，不得各自复制条件。Gate 拒绝时不调用 Android BLE API；拒绝原因作为可观测状态返回。

### 2. 权限与生命周期入口

- `Application` 冷启动时尝试一次；缺少权限时记录 `WAITING_PERMISSION`，不伪装成连接错误。
- `MainActivity` 权限回调确认 BLE 权限齐全后调用协调器 `retryNow()`。
- APP 回到前台或蓝牙从关闭变为开启时，若仍允许自动连接，调用同一个幂等入口。
- 同时到达的启动、权限和前台事件只允许一个连接尝试；CONNECTING、CONNECTED、SCANNING 时不重复发起。

### 3. 冷启动和断线重试

`ConnectionSupervisor` 不再以 `hasBeenReady` 作为自动重连前提。当前处于 DISCONNECTED/ERROR 且共享 `BleAutoConnectGate` 允许时，按现有 1、2、4、8、16、30 秒退避发起连接。连接请求返回“已发起”不等于成功；后续 GATT ERROR/DISCONNECTED 会开启下一轮退避。进入 READY 后清理错误和重试任务。Gate 因权限、蓝牙、设置或用户断开而拒绝时，取消当前退避任务；相关条件恢复后由协调器重新唤醒。

已知 MAC 优先显式直连，避免主机暂时没有出现在普通扫描结果中。手动扫描仍只展示名称匹配 EdgeFocus 的设备，并且不会偷偷自动选择非目标设备。

### 4. 用户主动断开

`BleManager.disconnect()` 保持清理 GATT 的职责；用户入口调用的主动断开还必须把 `userDisconnected=true` 写入专用 SharedPreferences。该标志跨进程重启和 APK 覆盖安装保留，卸载/清除数据才自然清除。协调器、Gate 和监督器都必须尊重该标志，不因 Activity 重建、进程重启、前后台切换或权限回调重新连接。

以下明确的用户行为先持久化 `userDisconnected=false`，再重新启用自动连接：

- 用户手动开始扫描；
- 用户确认连接某台设备；
- 用户点击明确的“重新连接主机”入口。

切换 `useEdgeFocusBoard=false` 只关闭 Gate，不改写 `userDisconnected`；重新开启该设置时，若用户此前没有主动断开，可立即恢复自动连接。

## 主机设计

### 1. 独立注册状态

主机分别维护：

- D-Bus 本地对象已注册；
- GATT Application 正在注册/已注册；
- Advertisement 正在注册/已注册；
- 下一次重试时间；
- 进程是否正在关闭。

`bluez_ready_` 只有在 GATT Application 与 Advertisement 均注册成功时为 true。GATT 已注册而广播未注册时，`runOnce()` 只重试 Advertisement，不重复注册 Application。

### 2. 广播失败与 Release 恢复

- Advertisement 注册失败：清除 registering/registered，保留已注册 GATT Application，按 2、5、10 秒上限退避重试广播。
- `LEAdvertisement1.Release`：立即标记广告失效并安排重注册；若正在 shutdown，只确认 Release，不重试。
- 手机 `StopNotify`/异常退订：清理 phone connected 和事件订阅状态，并安排一次幂等广告健康检查。若广告仍注册则不重复注册；若 BlueZ 已释放则进入上述恢复。
- 广播恢复不能终止正在运行的 EdgeFocus 会话，也不能清除会话快照或事件 Outbox。

### 3. 防止重复注册

所有注册状态在 `bluez_mutex_` 下更新。任一时刻每种注册最多一个异步调用。每次 Advertisement 注册分配严格递增的 generation，并把该 generation 绑定到异步回调上下文。Release、显式取消、shutdown 或开始更新一代注册时都推进/失效旧 generation。异步回调只有在 generation 仍等于当前期望值且未 shutdown 时才能更新 registering/registered/`bluez_ready_`；旧回调只释放自身资源，不得覆盖新状态或取消恢复任务。

## 状态与时序

### APP 覆盖安装后

1. 旧 APP 进程被系统终止，Android 关闭旧 GATT。
2. 新 APP 进程启动，协调器读取保留的 MAC。
3. 权限和蓝牙可用时显式连接已知 MAC。
4. notification 订阅完成后 `ConnectionSupervisor` 执行 hello 与状态同步。
5. READY 后恢复主机会话、PRECHECK 或空闲状态。

### 主机广播异常时

1. BlueZ 广告注册失败或发出 Release。
2. 主机保留 GATT Application 和业务状态，只标记 Advertisement 不可用。
3. `runOnce()` 按退避重新注册 Advertisement。
4. 广播恢复后，APP 的直连或扫描可再次建立 GATT。

## 错误处理与可观测性

- APP 日志区分：无保存 MAC、等待权限、蓝牙关闭、直连已发起、GATT 失败、协议协商失败、用户主动断开。
- 主机日志区分 Application 注册、Advertisement 注册、Release、重试次数与下一次重试时间。
- UI 不把“等待权限/蓝牙关闭”显示成“主机协议不兼容”。
- 自动连接在后台受 Android 限制时不启动新的前台服务；用户打开 APP 后必须立即恢复尝试。正式会话所需前台服务保持现有生命周期。

## 测试与验收

### APP 自动化

- 已保存 MAC + 权限具备：冷启动发起一次目标连接。
- 无 MAC：不连接任何设备。
- 权限缺失：不连接；授权回调后立即重试。
- 用户主动断开：前后台切换和权限回调都不重连。
- 用户主动断开后进程重启或覆盖安装：持久化 Gate 仍拒绝自动连接。
- `useEdgeFocusBoard=false`、蓝牙关闭或权限缺失：所有自动重试路径均被同一 Gate 阻止。
- 从未 READY 的首次连接失败：仍进入退避重试。
- CONNECTING/CONNECTED/SCANNING：重复触发入口不产生第二次连接。
- 原有协议、PRECHECK、屏保与手环状态测试继续通过。

### 主机自动化

- Application 成功、Advertisement 首次失败：只重试 Advertisement，最终 READY。
- Advertisement Release：安排重注册并恢复 READY。
- Advertisement 注册回调与 Release 竞态：旧 generation 回调不得恢复 READY。
- shutdown 后迟到回调：不重试、不恢复 READY。
- StopNotify 清理 phone connected/Outbox subscribed，且不破坏会话状态。
- 重复 `runOnce()` 不产生并发重复注册。

### 实机验收

1. APP 已连接主机时覆盖安装新 APK，主机服务不重启；打开新 APP 后自动恢复连接。
2. 强制停止 APP 后重新打开，自动恢复连接。
3. APP 未运行时启动主机，随后打开 APP，自动连接。
4. 主机暂时不可达，稍后恢复广播，APP 无需进入设置页即可重连。
5. 用户主动断开后等待至少 60 秒，APP 不应自行连接；用户点击重连后恢复。
6. 首次安装或清除数据后，不自动连接陌生 EdgeFocus，仍要求用户确认设备。

## 部署边界

这项修复需要同时部署新 APK 和新主机程序才能完整覆盖：新 APP 解决冷启动与已知 MAC 自动连接；新主机解决广播失效后无需重启的恢复。任一侧单独更新只能覆盖部分故障。

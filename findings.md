# Focus App 审查发现

## 展示海报架构素材任务（2026-07-16）

- 用户希望尽量完整展示项目，不限于产品级系统图，也接受内部工程说明海报；最终会自行挑选、排版或生成图片。
- 交付应同时提供浏览器预览草图、Mermaid 源图和 Markdown 文案，不调用 AI 图片生成。
- 现有事实源已覆盖主程序三运行域、正式会话、视觉/手机融合、可靠提醒、App 目标架构、A35/M33 风扇异核闭环、G431 云台和 STUSB4500 供电。
- 适合拆成“系统总览主图 + AI 决策链 + 会话/可靠交互 + 异构硬件工程”四个层级，避免把全部模块压进一张不可读的大图；具体组合待用户选择。
- 当前总架构文档已有可复用的三运行域、正式状态机、判定链、展示隔离、部署和异构职责图，但云台状态仍写着 `motion_verified=false/生产未创建`；用户已确认硬件侧现已通过板端成功驱动云台，展示母版必须标注为“硬件链已打通，主程序会话集成待恢复”，不能沿用旧失败结论，也不能提前宣称会话内跟踪已完成。
- M33 风扇是最有展示价值的内部工程素材之一：A35 精确读取 cpu-thermal，RPMsg 1 Hz/3 秒 TTL，M33 100 ms 安全闭环独占 PB10 PWM/PB11 tach，bridge/loader/healthcheck/fallback 开机链经过 reboot 与物理断电验收。
- 异构图必须如实标注：NPU 做 Pose 感知；M33 当前只控制风扇；G431 独立承担云台电机实时环；A35 负责会话、融合、BLE、供电和安全编排。不能画成“所有硬件都由 M33 控制”。
- 云台设计的展示重点应是“进程级硬件服务 + 会话级可降级插件 + G431 确定性执行平面 + 双层 Fault 闭锁”，而不是简单串口控制。用户最新确认的是板端到云台的硬件驱动成功；当前 `config/gimbal-board.json` 仍未写入限位/中心/步长，所以主程序 PRECHECK 自动搜索和会话内跟踪仍应标为待集成/待标定。
- 当前主源码中已存在类型化 SessionRuntime、DistractionPipeline、FusionCore、STAI Pose runtime、Fan/Gimbal/Power 平台模块、BLE App/手环双角色、Demo/Storage 等明确边界，完整母版可以同时给出产品视角与代码导航视角。
- 用户进一步要求云台直接作为已完成功能进入新架构，不使用“硬件链刚通过/待集成”等过渡措辞；新母版按完整的 PRECHECK 自动调整与会话内视觉跟踪能力表达。
- 展示交付不负责海报版式，而是提供可挑选的架构图素材；推荐 C“完整架构图谱”：1 张系统母图 + 12 张专题图，覆盖产品、软件、AI、会话、可靠通信、异构、云台、风扇、供电、演示隔离、故障降级和部署。

## App 设计交付（2026-07-16）

- 已形成目标架构：保留 LOCAL/EDGEFOCUS 双模式，新增进程级唯一会话协调器、可靠 Event Inbox/Session Outbox、连接监督器、手机事实心跳、全局提醒交付、黑屏屏保、双页面 PRECHECK、实际预览和主机代理手环入口。
- 已形成当前主机配套矩阵：现有 schema 2.1 的 hello、事件 ACK、PhoneFact、正式会话、预览、LOCAL 幂等上传、历史、供电和云台直接复用；手环主动搜索/重连明确为 `band_proxy_control_v1` 小扩展，能力不存在时只读兼容，禁止 App 伪装成功。
- 为避免 `boot_id/state_revision` 理想设计阻塞当前主机，App READY 在 schema 2.1 下使用本地 connection epoch + hello 后强制 `get_current_state`；未来 capability 存在时再启用 revision 丢弃乱序。
- App 实施交接入口已写入 App 仓 `_workspace/docs/project_docs`，明确旧计划过期内容和当前主机源码必读路径。

## App 全源码复核：页面 ViewModel（2026-07-16）

- `HomeViewModel` 与 `ControlPanelViewModel` 都在观察同一个 `TimerStateHolder`，但又各自维护 `currentSessionId`、开始时间、已保存标志和 `AppMonitorService` 生命周期；会话落库和监控启停没有唯一所有者，两个页面并存或重建时存在重复保存、覆盖和竞态。
- 这些会话身份字段只存在于 ViewModel 内。共享的本地计时仍在运行时，如果 Activity/ViewModel 重建，新的 ViewModel 无法可靠恢复原会话 ID 与保存状态，可能漏存或以错误身份保存结果。
- `HomeViewModel` 在主机链路中只在 `RUNNING/PAUSED` 变化时捕获会话，未把 `PRECHECK` 纳入身份建立；主机走 `IDLE → PRECHECK → RUNNING` 时可能继续使用 App 临时 ID。`ControlPanelViewModel` 已覆盖 PRECHECK，两个页面行为不一致。
- 本地会话结束后的同步目前只在“结束瞬间恰好已连接主机”时尝试一次；尽管 Room 已有 `getUnsyncedSessions()`、`markSynced()` 与 `boardSessionId`，ViewModel 没有恢复待同步任务，上传成功后也没有闭环更新 `synced`。
- `ControlPanelViewModel` 初始化会调用 `enterControlPanelMode()`，空闲时强制选择主机模式；横屏控制面板当前是正式主机会话入口，而不是本地/主机双模式的另一套完整入口。该事实需与最终页面职责一起保留或明确调整，不能由“两个页面功能类似”推断它们实现等价。
- `BleViewModel` 实现手动扫描、连接、保存 MAC 和模拟连接，但没有自动重连事务；用户主动扫描时还会暂时禁用保存设备的自动目标。导航目前是竖屏 `MainActivity` 与横屏 `ControlPanelActivity` 两套入口。
- `DistractionAppsViewModel` 管理正式应用分类；`TestViewModel` 另有一套调试用分类与模拟状态。后者可保留为测试工具，但不应继续成为正式交互或主机上报语义来源。
- `DashboardViewModel` 已把主机历史拉取后写入同一个 Room，会话记录“统一在 App 展示、主机作为权威持久存储”的基础方向是存在的；但它只在 BLE 状态切到 `CONNECTED` 时触发，分页失败会直接终止且无同步游标/重试状态。
- 会话反馈另用 SharedPreferences 做一个轻量待提交队列，仅对 `source == EDGEFOCUS` 的记录上传；本地模式结果的待同步并未复用同一机制，也没有统一 outbox。
- `syncLastReport()` 与 `syncBoardHistory()` 都会以 REPLACE 方式写 Room；这能按主键合并同一主机会话，但同步职责被放在 Dashboard 页面 ViewModel，用户若不进入/不创建该页面，历史同步的生命周期并不可靠。
- `SettingsViewModel` 当前只支持番茄时长与“使用 EdgeFocus 设备”开关；屏保开关、黑屏等待时间等已确认产品设置尚未进入 Settings 实体、仓库和 ViewModel。

## App 全源码复核：竖屏首页、数据页与设置页（2026-07-16）

- 用户记忆准确：`HomeScreen` 在设置为主机模式且设备未连接时，开始按钮明确禁用，并显示“设备未连接 / 连接设备后可使用 EdgeFocus 模式”；代码没有自动降级成本地会话。
- 首页当前仍展示板端电量，并在无合法值时硬编码回退为 `67%`；这与硬件已删除电量读取不符。目标应只显示外部供电推导出的 HOME/GO 与手动覆盖状态，不再显示虚假百分比。
- 首页右上蓝牙图标按钮的 `onClick` 为空，不能进入连接或状态页；“设置页主动搜索和显示手环状态”也尚未出现在当前 Settings UI。
- 首页没有 PRECHECK 过程 UI、云台自动扫描/手动控制、检查项与失败原因；主机模式开始后的 PRECHECK 只能由底层状态变化间接体现，竖屏专注页与横屏控制面板并不等价。
- `DashboardScreen` 统一展示 Room 中的本地与板端记录，符合产品方向；但 `distractionCount == 0` 就标记“全程专注”，没有区分会话缺少有效视觉/手机证据、提前中止或结果不完整。
- 正式 Dashboard 反馈值是 `too_much / too_little / just_right`；通用 `FeedbackDialog` 仍使用另一套 `too_many / not_enough / perfect / well_detected / missed / other`，两套协议不兼容。当前 Dashboard 没调用该通用 Dialog，因此它更像遗留组件。
- `SettingsScreen` 目前只有默认时长、主机模式开关、主机 BLE 连接入口和分心应用分类；尚无屏保设置、手环状态/搜索、提醒表现设置等目标功能。
- 设置页把主机连接入口写成“添加 EdgeFocus 设备”，目前 BLE 页面只覆盖主机 GATT 连接；手环是由主机代理管理的另一类设备，后续 UI 与协议必须避免把两种搜索混为一谈。
- `BleConnectScreen` 只扫描/保存/连接 EdgeFocus 主机 MAC，确认弹窗声称“下次自动连接”，但底层当前没有完整的断线自动重连事务；UI 承诺强于实现。
- 分心应用页面仍用 `HIGH / LOW / LEARNING / BACKGROUND` 四级配置，并写着“学习类：不提醒”。这不符合已确认的新语义：App 向主机上报娱乐/学习/其他原始类别，学习仅降低手机分支的分心可能性，不能承诺绝不提醒或压掉独立视觉证据。
- `TestScreen` 的 BLE 模拟、板端回复、板端会话和日志均藏在 Debug 七连点入口，正式版默认不可见；可继续用于开发回归，不应承担现场或正式故障恢复。
- 横屏 `ConfigPage` 仍暴露板端 `NORMAL/SILENT` 和“离座超时自动暂停”配置。板载屏幕/声音已移除后，NORMAL/SILENT 的真实执行对象需要迁移为 App 提醒策略或删除；离座自动暂停则必须与主机会话权威状态机对齐，不能由页面配置另一套本地行为。

## App 全源码复核：横屏控制面板（2026-07-16）

- 横屏是三页 Pager：计时/PRECHECK、会话统计、设备设置；只有横屏实现了 PRECHECK 页面。竖屏专注页当前没有同等入口，因此用户要求的“两处都能看预检”确实是未实现项。
- PRECHECK 虽已请求 `cameraPreview`，页面只展示宽高、来源和序号，没有解码或绘制图像；文案明确写成“拍照式刷新”。因此当前还不能让用户通过 App 实际确认取景画面。
- PRECHECK 已有云台在线/安全/归位/角度/错误状态和上下左右、归中、停止控制；只有主机返回 gimbal 对象时显示。这部分可作为后续协议对齐的 UI 基础，不需要推翻重做。
- PRECHECK 同时提供“继续”和“跳过”两个绕过入口，分别调用 `skipPrecheck` 与 `skipCalibration`；需要以主机状态机明确两者的权限和结果，避免 App 在硬件/视觉检查失败时无条件越过安全门槛。
- App 的前台全屏提醒与提示音只在 `ControlPanelScreen` 中消费 `lastAlert`。竖屏专注页没有同一提醒渲染器；若用户在竖屏前台，会依赖 `AppMonitorService` 的本地通知而不是主机 `push_alert`，与“App 接管屏幕闪烁/弹窗”目标不符。
- 横屏统计仍显示旧 `focusState` 数值映射，其中状态 2 是“疲劳/姿态异常”，与已删除困倦功能冲突；还显示“手机信号等级 Lx”和风险分，继续暴露旧版由手机端/旧 BehaviorEngine 预判等级的语义。
- 横屏设置页完整保留主机提示音量、NORMAL/SILENT、屏幕/手环/声音提醒渠道；板载屏幕和声音删去后，应重分为“App 本机提醒表现”与“主机代理手环策略”，不能继续作为板端输出硬件设置。
- 横屏同样保留虚假电量百分比回退。控制面板的连接状态图标仅展示，未提供主机或手环的主动重连入口。
- 主题层目前只有固定浅色方案，没有黑屏屏保主题或全局提醒视觉层；纯黑屏保应作为独立顶层遮罩/页面处理，同时由 Activity 统一承担防自动锁屏，而不是靠现有 Material 主题切换。

## App 全源码复核：测试契约（2026-07-16）

- 现有测试主要覆盖纯策略函数、JSON 解析、BLE 扫描/写入边界和 Room 迁移；没有覆盖真实会话编排、Activity/ViewModel 重建、断线重连、可靠事件 ACK、待同步补传或竖横屏共用状态，因此核心风险不会被当前测试发现。
- `AppMonitorForegroundPolicyTest` 明确把“有 Usage Access 但前台包名为空”固定为后台强干扰和提醒；`AppMonitorBleUsagePolicyTest` 又把它上报为 active/using/level 3。这正是黑屏屏保或系统锁屏被误判的问题，测试需要随新语义改写。
- `PhoneUsagePolicyTest` 与 `buildPhoneSync` 测试继续固化手机端 `distraction_level`，并把 learning/level 0 当作不计分；新协议应测试原始 `active + category + timestamp/sequence/freshness`，最终等级只由主机融合层产生。
- `HomeStartActionPolicyTest` 规定主机 PRECHECK 时再次按开始按钮会调用 `SKIP_PRECHECK`；这解释了竖屏为何没有预检页面却仍能继续，但也固化了跳过而非展示/控制预检的旧交互。
- `BoardJsonParserTest` 明确保护缺失/非法电池回退 67、板端屏幕/声音路由、困倦相关旧 `focus_state` 外围结构；这些测试需在硬件与功能收束时删除或改成外部供电/HOME-GO、App 提醒、手环代理的新契约。
- Room 迁移测试有效验证了旧会话迁移后默认 `source=LOCAL`、`synced=false` 及新增手机计数字段；后续增加 outbox/屏保设置时应继续采用显式迁移，而不是 destructive migration。
- BLE 连接、扫描过滤、模拟仅限 Debug、请求大小和分块重组测试仍有价值；但分块测试没有总块数/总字节上限、超时清理和乱序/重复攻击边界。

## App 全源码复核：仓内旧设计文档（2026-07-16）

- `_workspace/docs/project_docs` 记录了双模式、统一记录、三档反馈、MAC 绑定、PRECHECK 和本地结果上传的早期设计意图，能解释当前代码为何已有一半基础设施；用户关于“原本就是这么设计”的记忆有源码和文档双重依据。
- 这些文档也明显过期：仍描述开发板屏幕、板端振动/LED/声音、用 `phone_sync` 上传整场本地会话、BOARD 反馈走 `reminder_ack` 等旧方案，不能直接作为当前实施规格。
- 旧计划已经要求首页蓝牙图标跳转连接页；当前连接页和导航路由已存在，但首页按钮仍为空，说明这是一次未完成的接线而不是新需求。
- 旧计划把 EdgeFocus 前台长时间返回 null 归因于 UsageEvents，并建议缓存上次包名；新屏保需求使该补丁不够完整：必须显式区分 App 纯黑屏保、系统锁屏/非交互、权限缺失、采样空洞和真正离开 App。
- `todo-plan` 对本地模式“结束时仅在 BLE 已连接就上传”的描述正是当前实现；现在已确认应升级为 Room/Outbox 驱动的断线待同步，不能把旧计划当成完成标准。

## 最终板端检查（2026-07-14）

- `focus_app.service` 健康检查持续正常，展示 HTTP 和 BlueZ GATT Server 均完成注册。
- 当前现场未发现名为 `EdgefocusBand` 的手环，因此日志中为预期的周期性扫描/重试，不是主服务故障。
- 板端系统时间仍显示为 2024-03-01，而开发主机为 2026-07-14。运行时 TTL/重试使用 monotonic clock，不受影响；但正式报告墙钟时间和日志时间在现场前仍需通过系统时间同步修正。
- OpenCV SDK 配置会警告缺少未使用的 `libnumeric.a` imported target；本次所有目标仍链接成功，主程序 NPU/HTTP 健康检查通过。

## 本机 MVP 手机输入边界补充（2026-07-13）

- 本机子项目不实现手机状态采集、通信或真实应用分类代码。
- 提供手动手机状态模拟器，用于选择 `INACTIVE`、`LEARNING`、`ENTERTAINMENT`、`GENERAL_OR_UNKNOWN`，并可设置时间戳、可信度与新鲜度。
- 模拟输入仅用于验证视觉证据与手机上下文的融合行为；五天关键路径继续以视觉识别、时序稳定性、离线回放和评测为主。

## 本机实施环境核对（2026-07-13）

- Python 3.12.10；OpenCV、Ultralytics、PyTorch 2.6 CUDA、torchvision、scikit-learn、NumPy 与 ONNX Runtime 可用，CUDA 可用。
- MediaPipe 当前未安装；脸部/head-pose 必须保持 adapter 边界，避免单一依赖安装失败阻塞回放、Pose 与融合主链路。

## 新子项目设计输入（2026-07-13）

- 用户批准采用审查建议，产品收束为“视觉证据 + 手机上报状态 -> 分神等级 -> 提醒”。
- 明确保留候选：人在/不在、是否趴下；左右转头需重做并验证 pose 模型可分性。
- 明确删除：困倦、闭眼疲劳、建议休息等旧功能。
- 期望交付形态：独立子项目，本机可验证，稳定后上硬件测试，最终替换原 `vision/behavior/policy` 中的业务判定部分。
- 多模型是允许的实现手段，不应成为稳定数据契约；融合核心只接收带时间、质量、置信度和来源的观察量。
- 首要待澄清语义：“趴下”必须与正常低头写字/阅读区分，否则现有 `head_down_score` 的误报会被带入新设计。
- 用户明确偏好多模型/学习型证据架构，不接受继续以手写 pose 比例阈值作为主判定：pose 关键点及其时间序列应输入独立训练的状态分类器；左右转应考虑单独的脸部检测、landmark 或 head-pose 模型。

### 架构修正

- “Python 参考实现后移植 C++”与“直接 C++”是工程语言路线；“pose 阈值/关键点分类器/脸部模型”是模型架构路线，两者不能混成同一个选项。
- 推荐模型架构调整为独立专家：
  1. person detector/tracker：主用户锁定与 PRESENT/NOT_PRESENT；
  2. pose backbone：只输出归一化 keypoints、visibility、bbox 与时序变化；
  3. learned pose-state classifier：在 1–3 秒关键点窗口上学习 NORMAL_STUDY / PRONE / AMBIGUOUS，不使用单个鼻肩阈值；
  4. face/head-pose expert：输出 head-away 连续证据、方向与质量，负责左右转头；
  5. 可选 appearance prone expert：若趴桌导致关键点消失、关键点分类器不可分，再对主用户/桌面 ROI 增加轻量图像分类器；
  6. fusion core：只消费标准证据，不依赖具体模型实现。
- 现有三张图片不足以训练关键点分类器；第一阶段采集必须同时保存视频、逐帧关键点和场景/episode 标签，以便比较“规则 baseline”与“学习型分类器”。

### 边缘算力与脸/视线研究核对

- 用户补充：项目参加边缘 AI 视觉赛道，实际算力小于 2 TOPS。
- ST 官方 STM32MP257 数据手册给出的 NPU 理论性能为 1.2 TOPS INT8（800 MHz，900 MHz overdrive 为 1.35 TOPS）：https://www.st.com/resource/en/datasheet/stm32mp257d.pdf 。设计预算必须以板上实测 latency/DDR/CPU 占用为准，不能按 TOPS 简单相加。
- ST 官方 model zoo 已加入 Face Detection 与 Face Landmarks，并把 Pose Estimation 列为 STM32MP257F-EV1 支持用例；modelzoo-services 提供训练、量化、评估和硬件 benchmark 工作流：https://github.com/STMicroelectronics/stm32ai-modelzoo 与 https://github.com/STMicroelectronics/stm32ai-modelzoo-services 。这使“脸部 landmark/head-pose 专家”具备官方工具链候选，但具体模型仍需在 MP257 上验证算子兼容和延迟。
- Google MediaPipe Face Landmarker 可作为主机参考 extractor，支持 video/live-stream timestamp；其 live 模式允许丢帧，因此新融合器必须按时间和 freshness 工作，不能假设逐帧输出：https://ai.google.dev/edge/api/mediapipe/python/mp/tasks/vision/FaceLandmarker 。MediaPipe 本身不直接等于板端方案。
- CVPRW 2022 的 PAFE 使用约 15 小时、15 名学生、1,100 次状态 probing，以及眼动/情绪/头动等 49 类面部视频特征，报告 AUROC 约 0.67，并指出光照、脸/眼可见性和头动会影响性能：https://openaccess.thecvf.com/content/CVPR2022W/CVPM/papers/Lee_Predicting_Mind-Wandering_With_Facial_Videos_in_Online_Lectures_CVPRW_2022_paper.pdf 。因此不能承诺从脸直接稳定识别内部“沉思/发呆”。
- 竞赛可采用更可信表述：在端侧融合 `gaze-away/head-away + prone + phone context + temporal consistency`，输出“可观测分神风险”，并提供 UNKNOWN、置信度和可解释证据。
- 算力控制建议：person/pose 低频全帧或跟踪失效时运行；face/landmark 仅在主用户 ROI 且脸可见时运行；关键点时序分类器与融合器极轻量；可选图像 PRONE 专家按不确定性触发，而非全时并行。
- 用户批准“person/tracker + pose keypoints + learned pose-state + face/head/gaze + optional appearance prone + temporal fusion”的多专家架构，并选择先在本机完成 Python 核心。
- 新约束：距离线下展示约 5 天。MVP 必须优先确保可运行回放、稳定输出、解释证据和量化结果；硬件移植、完整训练集、复杂标注 UI 与直接 mind-wandering 模型不进入这五天关键路径。
- 用户确认“趴下”定义采用严格方案 A：头部或上半身接近/贴在桌面并持续一定时间；正常低头写字、阅读是必须通过的负样本。该定义意味着仅靠单个鼻肩比例大概率不足，需要桌面/身体几何关系或专用分类证据。
- 用户确认“人不在”采用独立上下文语义：`NOT_PRESENT` 不等于分神，不产生提醒；融合核心应把 `presence/applicability` 与 `distraction_level` 分开，上层再决定暂停、离位统计或会话结束。
- 用户确认手机输入采用原始事实方案 A：`active`、前台类别（学习/娱乐/未知）、时间戳、可信度/新鲜度；手机端不直接决定 0–3 等级。新核心必须分别计算视觉证据和手机证据，再融合，禁止手机类别倍率覆盖整个视觉风险。
- 用户确认三级分神采用动态方案 A：等级由证据强度、持续时间、多源一致性决定；长时间分神或语义上严重的分神必须进入更高等级。设计应采用“升级可因强证据加速，降级需要恢复证据与迟滞”，而不是旧版离散事件常数。
- 用户确认模块边界采用 A：核心输出稳定的 `level + reasons + evidence + episode`，不拥有提醒频率、冷却、文案或设备执行。提醒策略作为外部消费者，可独立演进和测试。

### 现有本机视觉样本

- 路径：`C:/Users/Aze/Desktop/self_developed/_workspace/picture_test`，共 3 张：`低头玩手机.png`、`睡觉.jpg`、`离座.png`。
- `离座.png` 可作为 NOT_PRESENT 正样本；`低头玩手机.png` 是 PRESENT 且 NOT_PRONE 的困难负样本；`睡觉.jpg` 接近 PRONE 正样本。
- 两张设备视角图片呈约 90°安装方向，`睡觉.jpg` 则是不同分辨率/视角/光照的近距离画面；必须把相机 orientation 与 source domain 纳入输入元数据，不能让模型暗中猜旋转。
- 三张静态图无法验证持续时间、抖动、升级/降级、无帧空洞或左右转头可分性；最小可靠评测仍需要按场景录制短视频或连续帧。
- 当前缺失场景：正常正坐、写字、阅读、轻微低头、左转、右转、短暂转头、趴桌过程、遮挡、多人、不同光照和相机重连。
- 用户接受在第一阶段补采每个关键场景 10–30 秒短视频。子项目应提供最小采集/标注/回放/指标工具，指标至少覆盖 episode 误报漏报、检测延迟、等级切换次数、无效/未知占比和模型耗时。
- 用户确认左右转头采用统一业务语义 `HEAD_AWAY`；左右方向/yaw 仅作为诊断证据。业务等级依赖偏转强度、持续时间与融合，而不依赖方向枚举。
- 用户修订手机类别语义：学习类不是完全中性，而是弱分神证据，并用于解释“看手机但用途为学习”的视觉相似场景；娱乐类为强证据，未知/普通使用为中等证据。关键约束：学习类别只能降低/限制手机分支贡献，不能乘除或压低独立成立的 PRONE、HEAD_AWAY 视觉证据。
- 用户确认异常/不足证据采用 A：输出独立 `UNKNOWN/INSUFFICIENT_EVIDENCE`；已有等级短时保持后按时间衰减，禁止把未知当成 FOCUSED 或 DISTRACTED。模型健康、信号新鲜度和有效观察覆盖率必须进入输出。
- 用户确认误提醒成本高于检测延迟/短时漏报。验收应以 episode 和产品指标为主：正常学习每小时误提醒数、困难负样本误报率、等级抖动次数；逐帧准确率仅作诊断。
- 用户确认单用户策略 A：会话初始化锁定主用户/座位 ROI，其他路人忽略；主目标短暂丢失为 UNKNOWN，持续丢失才转 NOT_PRESENT。第一阶段不做多人分别判定。

## 结论摘要与优先级

### P0：不先处理就不应调阈值

1. 当前真实 pose 路径没有 yaw，左右转头走神分支不可达；低头特征的标定统计又不可分，不能靠换一个 0.70 数字修好。
2. 真实模型失败会静默进入亮度启发式 CPU fallback；模型交付未纳入 install，参考 service 还强制 mock，必须先确认板端实际运行路径。
3. 行为风险与三级提醒逻辑错误：类别固定映射、视觉二级不可达、手机倍率污染整体视觉风险。
4. `SystemStateStore` 整对象读改写会在多线程间丢更新，属于与 AI 无关但会造成随机状态回退的基础稳定性问题。

### P1：重设计必须同时解决

5. 低置信度按帧数而非时间；连续事件允许跨无帧空洞；状态无新帧时永久保留。
6. VisionResult 新旧双轨、模型能力与字段宣称不一致，eyes/hands/phone 等仅 mock 可达。
7. LOW_CONFIDENCE 被记作用户走神，且能抢占手机/离位状态；状态迟滞与提醒等级的时间语义不一致。
8. 模块线程无异常隔离，队列丢帧统计失真，健康监控无法发现真实故障。

### 已确定的产品决策

- 删除“困倦 / SLEEPY_OR_SLUMPED”；等待用户确认 `eyes`、`posture`、`head_down` 等视觉字段的最终去留后统一改造。

## 已确认上下文

- 应用 Git 仓库：`code/focus_app`
- 当前分支：`main`
- 审查开始时工作树干净
- 视觉相关目录：`vision/`、`camera/`、`models/`、`config/models/`
- 行为相关目录：`behavior/`、`policy/`
- 用户已决定删除“困倦”状态；后续清理范围至少包括 `SLEEPY_OR_SLUMPED`、eyes closed/slumped 事件路径、固定风险 80、相关提醒文案与测试。是否连同 `eyes`/`posture` 视觉字段删除，等字段清单确认。
- Git 历史只有一个初始化提交，因此无法从提交演化中判断哪些阈值来自实验、哪些来自 MVP 占位。
- `VisionResult` 同时保留两套语义重叠的输出：带 `valid/confidence` 的通用字段，以及 `presence_conf/head_down_score/head_state` 三个 V1 字段。需要核对行为层实际消费哪一套，避免新旧协议分叉。
- `VisionResult::isUsable()`、`isEyesClosed()`、`isPhoneDetected()` 内直接写死 0.5/0.5/0.6 阈值；`BehaviorConfig` 还包含另一组时长/比例阈值，配置来源目前没有数据标定说明。
- `BehaviorConfig.h` 的中文注释存在明显编码损坏；不一定影响运行，但会妨碍维护者正确理解参数语义。
- 行为状态枚举混合用户状态（FOCUSED/LOOKING_AWAY 等）、内部质量状态（LOW_CONFIDENCE）和手机使用类别（LEARNING/PLAYING），后续需要核对“三级走神等级”到底由状态、风险分还是策略命令表达。

## 待验证假设

- MVP 阶段的阈值和权重可能缺少数据标定依据
- 单帧视觉输出可能未经充分的质量门控、时序滤波或不确定性传播就进入行为层
- 行为三级输出可能混合“瞬时强度”和“持续时长”，导致边界抖动或升级/降级不对称

## 已证实问题

## 2026-07-14 主程序收束阶段的新发现

- 合理顺序是“先架构、再融合、后演示增强”：演示页需要稳定的只读快照契约，若先改页面会继续绑定旧 `SystemSnapshot/BehaviorContext`。
- 已验证的 C++ 新核心边界清晰：YOLO11n Pose 后处理/主目标选择/58 维节点特征/时序 MLP/Presence-Prone/手机原始类别融合，最终输出 applicability、level、reasons 和 episode。
- 主程序旧 `BehaviorEngine` 同时承担手机同步、旧状态机、固定风险评分、会话统计、存储和推送，不能把新核心直接塞入该类；应拆成“事实输入—判定核心—产品策略—会话统计”四个边界。
- `SystemStateStore` 当前是跨模块大对象快照，并存在整对象读改写覆盖问题；目标设计应使用单写者领域状态或按字段/事件更新，演示端只读聚合快照。
- App 当前上报包含旧 `distraction_level` 语义，而新产品契约只接受原始手机事实；兼容层可解析旧载荷，但必须忽略其决策等级。
- 演示页当前的强制 override 会绕过真实链路，不适合作为正式展示机制。目标模式应区分 `LIVE_CAMERA`、`RECORDED_INPUT`、`EVIDENCE_REPLAY` 三种来源，并在界面显著标记来源与推理真实性。
- 板端实测约 4 FPS/191 ms；网页刷新频率可以提高观感，但不能创造真实推理帧。应将视频预览与推理结果解耦：视频可按相机帧率平滑显示，证据覆盖层按 NPU 推理帧更新并显示新鲜度。
- 用户明确云台应当是会话级外挂插件：PRECHECK 阶段自动搜索/手动控制，FOCUSING 阶段 OpenCV 跟踪，PAUSED/STOPPED 阶段保持或安全停止。
- 当前 `GimbalMonitor` 是进程级常驻 Module，启用时持续轮询并只做一次固定 CENTER/DEG 初始化；还没有与会话阶段绑定，也没有自动搜索、跟踪所有权或手动/自动控制仲裁。
- 当前 `AppCommandService` 已有云台状态与手动命令入口，可保留协议外观，但内部应改为向 `GimbalSessionPlugin` 申请短时手动控制权，避免与自动跟踪同时发串口命令。
- 云台跟踪不能创建第二个相机采集实例。推荐由统一 CameraSource 发布预览帧；NPU 主目标框周期性校正 OpenCV tracker，tracker 在 NPU 帧之间提供高频中心误差，串口控制器做死区、限速和安全角度约束。
- 用户明确删除板载屏幕与板载声音，提醒输出改为 App 事件。现有 `ReminderPolicy` 已经存在 phone route/push_alert 雏形，但仍耦合 `DeviceManager` 和 board_actions；目标应改为可靠的 AppNotificationOutbox + ACK/重试/去重。
- 用户确认手环仍由板端蓝牙直控，App 也走蓝牙。目标架构应有统一 `BluetoothGateway` 管理同一适配器上的两种角色：Phone GATT Server（命令/状态/提醒）和 Band GATT Client（提醒执行）。
- 当前 `DeviceManager` 同时包含 ScreenController、SoundController 和 BleBandController；删除板载屏幕/声音后不应继续保留这个混合抽象，应收束为独立 `BandNotificationSink`，并由 NotificationRouter 只路由 APP 与 BAND。
- 用户补充主流程以会话为中心。当前 main.cpp 在进程启动时常驻启动 CameraPipeline、VisionEngine、BehaviorEngine、DeviceManager、GimbalMonitor、ReminderPolicy，SessionController 只修改共享状态；这与目标语义相反，也是空闲耗能、生命周期竞态和状态泄漏的来源。
- 推荐两层生命周期：`ProcessServices` 常驻接收 App 命令并维护存储/蓝牙；`SessionOrchestrator` 为每次会话创建 `SessionRuntime`，按 PRECHECK/FOCUSING/PAUSED/ENDING 有序切换 session services 与 extensions。
- “会话外挂”可正式定义为 `ISessionFeature`：prepare(SessionContext)、enterPhase(SessionPhase)、stop(StopReason)、health()。云台是可降级 extension；推理、融合、提醒和记录是 SessionRuntime 的核心 service，不应全部称为可选插件。
- CMake Release 会定义 `NDEBUG`；仓库旧测试大量把有副作用的业务调用放在 `assert(...)` 内，Release 下调用会被完全移除。`test_ble_gatt` 的 Release 段错误属于该测试结构风险，不能直接判为本轮业务回归；新测试已改为始终求值的断言。
- 新会话骨架和迁入核心已在编译机通过完整编译，证明新命名空间、C++17、nlohmann 模型加载和原工程依赖可以共存。

### 视觉链路

1. **真实模型的“无人”置信度映射错误。** `StaiMpuPoseAdapter` 在无检测时写入 `person_confidence = 0.85`，但 `InferenceScheduler::buildResult()` 给 `Presence::AWAY` 使用的是未赋值的 `presence_conf`（默认 0），造成“很确定无人”在结果协议里变成置信度 0；同一结果的 `face.confidence` 却是 0.85，字段互相矛盾。
2. **当前真实姿态模型没有产生 yaw。** `StaiMpuPoseAdapter` 只计算 pitch，`HeadPose::yaw` 保持默认 0；但“LOOKING_AWAY”唯一视觉条件是连续 `abs(yaw) > 30°`，因此现有真实模型路径永远无法识别左右转头走神。
3. **旧版 pitch 计算存在单位错误。** `(0.20 - (shoulder_y - nose_y)) * 140` 将像素差与 0.20 直接相减，通常立刻被夹到 -35°；该值会把姿态持续标为 `LEANING_FORWARD`。与同函数里已按 bbox 高度归一化的 `head_down_score` 不一致。
4. **CPU fallback 不是人体/姿态模型，却参与产品判定。** 它用中央区域亮度差猜测人物、用两个像素点亮度差猜测低头，并输出 `head_state`；在 AUTO/REAL 模型失败时会静默接管。这会把场景纹理、光照变化当成人或低头，是明显的不稳定来源。
5. **坏画面仍先执行模型。** 调度器完成质量评估后，无论 DARK/BRIGHT/BLURRY 都运行适配器，最后才在 `buildResult` 丢弃推理输出；这增加负载和延迟波动，也扩大底层模型在异常输入上的故障面。
6. **多人检测信息被丢弃。** 适配器设置 `multiple_people`，但 `buildResult` 仍输出普通 `FaceState::DETECTED`，没有形成 PARTIAL/MULTIPLE/不确定性信号。

### 行为链路

7. **帧率会改变低置信度结论。** 30 秒窗口按“帧数比例”而不是时间占比计算 `low_confidence_ratio`；推理帧率、丢帧或功耗档位变化都会改变相同现实场景的判定。
8. **连续时长允许跨越无观测空洞。** `getContinuousDuration()` 只比较首尾时间戳，不限制相邻帧间隔；两帧之间即使长时间没有数据，也会被视为连续证据。无新帧时状态又会无限保留，因为窗口只在新帧加入时清理。
9. **低置信度被当作走神，并抢占其他状态。** `LOW_CONFIDENCE` 被 `isDistractionState()` 计入走神次数，且状态优先级高于 AWAY、手机使用等。这会把摄像头/照明/模型故障统计成用户走神，并能覆盖仍然有效的手机信号状态。
10. **多个配置参数未被使用或语义错接。** `head_down_grace_ms` 未使用，低头确认实际用了 `slumped_confirm_ms`；`phone_confirm_ms` 未使用，手机状态直接生效。这使注释/配置与运行行为不一致。
11. **风险分是离散常数，不随证据强度或持续时间变化。** AWAY=100、SLEEPY=80、LOOKING_AWAY=40、PHONE=25，再按手机类别倍乘；组合采用 max，无法表达多种弱证据叠加，也没有置信度、不确定性或个体基线。
12. **视觉事件能力与协议宣称不一致。** 行为层会检测闭眼、趴伏、左右转头和手机，但当前唯一真实模型仅能给人物关键点和推导出的低头/前倾；eyes/hands/phone 始终 invalid，yaw 永远为 0。大量行为分支在真实部署中不可达。
13. **三级提醒与风险常数形成了几乎固定的类别映射。** 阈值为 1/45/80；LOOKING_AWAY 固定 40，只能一级，SLEEPY 固定 80、AWAY 固定 100，只能三级。视觉单信号没有产生二级的路径，也无法随持续时间逐级升级。
14. **手机倍率错误地缩放了整体视觉风险。** `RiskScorer` 先对所有事件取最大风险，再只要手机 active 就对这个整体分数应用 learning/using/playing 倍率。结果是“困倦 80 + 学习类手机”被减半到 40（三级降为一级），而“转头 40 + 普通手机”被放大到 60。手机上下文不应重写独立视觉证据的严重度。
15. **稳定状态与提醒等级使用不同时间语义。** FSM 回到 FOCUSED 需要 3 秒迟滞，但风险分会随当前帧事件立即归零；这 3 秒内对外状态仍是走神，`command_level`/提醒却已清空，输出互相矛盾。
16. **模型配置文件大部分未真正生效。** `preprocess.json`、`postprocess.json`、`runtime.yaml`、labels 和 checksum 没有被加载或校验；注册表用字符串查找而非 JSON 解析，只以 `model_path` 非空判定包可用。配置中的 keypoint threshold 0.35 与代码硬编码 0.25 也不一致。
17. **模型路径依赖进程当前工作目录。** 默认注册表与模型均使用相对路径；若 systemd/service 的 WorkingDirectory 不同，真实模型会加载失败并静默降级到亮度启发式 fallback。
18. **队列丢帧统计永远无法反映实际丢帧。** `drop_oldest=true` 时 `push()` 淘汰旧元素后仍返回 true，Camera/Inference 只在 false 时累加 dropped 计数；两个日志指标会长期为 0，掩盖性能不稳定。
19. **模块线程没有异常隔离。** 任一 `runOnce()` 抛出异常会越过线程入口并触发 `std::terminate`，整个进程退出；健康监控只每 10 秒打印固定“OK”，不检查线程、相机、模型或队列健康。
20. **启动失败清理不完整。** 某模块 `init()` 返回 false 时，先前已初始化模块不会执行 `shutdown()`；当前多个关键模块又总是返回 true，即使注册表/模型/摄像头不可用，导致“已启动”与实际能力不一致。
21. **图像内存假设未显式验证。** CameraPipeline 直接把 `cv::Mat.data` 当作连续缓冲区复制，未检查 `isContinuous()`/row stride；`CameraFrame::valid()` 也只检查字节数而不校验 format 与 channels 匹配。正常 VideoCapture 常常连续，但该假设属于潜在输入稳定性风险。
22. **提醒节流与等级演化脱节。** `last_level_` 被写入但不参与策略；相同瞬时风险每 5 秒可重复发出，升级也要受同一冷却限制。用户确认只把下一次间隔拉到 15 秒，发送一次后立即恢复 5 秒，不能表达 episode、升级优先或逐级干预。

### 测试与可观测性缺口

23. **策略测试绕过了上游可达性。** ReminderPolicy 测试直接注入任意 20/50/90 风险，能证明阈值函数，却不能证明视觉/行为链路能合理地产生这些分值；因此“视觉二级不可达”和手机倍率污染视觉风险均未被覆盖。
24. **FSM 测试固化了错接参数。** 低头测试按 10 秒触发并注释为 grace，但实际使用的是 `slumped_confirm_ms=10000`，没有断言 `head_down_grace_ms=12000` 的配置语义。
25. **缺少真实时序异常测试。** 未看到针对可变 FPS、长时间无帧、乱序/重复时间戳、模型降级、多人、相机重连以及队列丢弃的行为稳定性测试。
26. **构建配置默认关闭真实视觉能力。** OpenCV 和 STAI_MPU 默认均 OFF；主机测试主要覆盖 mock/fallback，真实 NPU 测试只在两者都 ON 时生成，因而日常 CI 很难发现真实后处理回归。
27. **现有“场景标定”数据本身否定了当前阈值。** `vision_config.json` 中 normal 的 head_down p50/p80=0.61/0.77，真实 head_down 的 p50/p80=0.53/1.00；两类中位数方向反转且强重叠，0.70 阈值必然同时产生较多误报与漏报。away 的 presence mean=0.23，却高于 PRESENT 下界 0.20，平均离位样本会被判 PRESENT。
28. **声明的 `keypoints_min_visible=2` 没有进入判定。** Python 与 C++ 都只计算可见比例，未使用该最小数量做 validity gate；少数关键点偶然高置信即可产生 presence/head_state。
29. **模型交付链不完整。** CMake 只安装可执行文件和 `config/`，不安装 `models/edgefocus_pose_v1/*.nb`；注册表又引用 `./models/...`。若部署步骤没有额外复制模型，AUTO/REAL 会启动成功但降级到 fallback。
30. **参考 systemd 服务明确强制 MOCK。** 硬件资料中的 `focus_app.service` 设置 `EDGEFOCUS_VISION_MODE=mock`，因此该部署配置完全不会使用摄像头/NPU。此文件可能是历史工件，需要上板核对实际 unit；若仍在用，当前“视觉识别表现”不是实模表现。
31. **共享状态存在典型的丢更新竞态。** `SystemStateStore` 的 `updateDeviceStatus`/`updateSessionIfCurrent` 接收整份旧快照并整体覆盖；Camera、DeviceManager、Gimbal、Behavior、网络等线程各自修改一个字段时，后写者会把其他线程刚更新的字段恢复为旧值。session id 检查只能防跨 session，不能防同一 session 内的并发丢更新。
32. **手机信号缓冲只有一个槽位。** `setPhoneSignal` 直接覆盖 pending 值；若 active/inactive 或类别变化在 BehaviorEngine 消费前连续到达，中间事件丢失。EventDetector 又会把最后 active 状态保持到显式 clear，丢掉 clear 时可出现长时间“手机仍在用”。

## 定向复现结果

- 按 `RiskScorer` 原公式：LOOKING_AWAY=40、SLEEPY=80；SLEEPY + learning phone 反而=40，LOOKING_AWAY + using phone=60，LOOKING_AWAY + playing phone=80。已验证手机倍率污染整体视觉分。
- 相同 30 秒中均有 12 秒低置信度：若好帧 10fps/坏帧 1fps，帧占比只有 6.25%；若好帧 1fps/坏帧 10fps，帧占比为 86.96%。现实时间占比都为 40%，但当前算法给出完全相反结论。
- 仅在 0ms 与 10000ms 各有一帧相同异常时，当前连续时长函数会返回 10000ms，证明两次观测之间的空洞被错误计为持续证据。
- bbox 高 250px 时，鼻肩距离 50px 得到 `head_down_score=0.333/NORMAL`，但旧 pitch 公式得到 -35°（夹紧下限）；距离 10px 时 score=0.867/DOWN，pitch 仍为 -35°。两套输出无法相互校验。

## 视觉字段生产/消费清单（待用户确认）

| 字段 | 当前真实模型产出 | 当前生产行为 | 行为层消费 | 初步处置 |
|---|---|---|---|---|
| `timestamp_ms` | 是 | 相机 steady clock | 所有时间窗口/FSM | 保留，但增加序列号与新鲜度 |
| `frame_quality` | 是（启发式） | 全画面亮度/对比度/边缘 | 质量门控、低置信度、预检 | 保留并重做 ROI/标定 |
| `presence` | 是（派生） | person + 关键点比例 | AWAY、低置信度、预检 | 保留，但修正语义和置信度 |
| `face` | 部分/伪字段 | 没有 face 模型；用 head_pose validity 代替 DETECTED | 仅预检 NOT_FOUND | 倾向删除或改为 `subject_visibility` |
| `head_pose` | 部分 | 仅错误的 pitch；yaw/roll 恒 0 | yaw 走神、pitch 预检 | 不能原样保留；若需要左右转头则换可靠模型/几何 |
| `eyes` | 否 | 永远 invalid（mock 除外） | 仅“困倦” | 随困倦删除，除非未来另有明确需求 |
| `posture` | 伪字段 | 由错误 pitch 推出，仅 UPRIGHT/LEANING_FORWARD | 仅“困倦/趴伏” | 随困倦删除或等独立需求后重建 |
| `hands` | 否 | 永远 invalid（mock 除外） | 生产行为层完全不消费 | 倾向删除旧版占位 |
| `phone` | 否 | 永远 invalid（mock 除外） | 生产行为层不消费；手机状态来自 BLE | 倾向删除视觉占位；若以后需要视觉手机检测再作为独立模型加入 |
| `presence_conf` (V1) | 是 | person_conf × 5 点可见率 | 生产行为不直接消费 | 与 `presence.confidence` 合并，删除双轨 |
| `head_down_score` (V1) | 是 | 鼻肩距离/bbox 高 | 生产行为不直接消费 | 若保留“低头”观察量，可作为连续特征；否则删除 |
| `head_state` (V1) | 是 | score > 0.70 | 低头持续后进入 SLEEPY_OR_SLUMPED | 不应继续作为困倦；由用户决定是否保留“低头走神” |
| `reason_code` | 是 | 主原因单枚举 | LOW_CONFIDENCE 判断 | 保留诊断用途，但改为多原因/质量 flags 更稳 |
| `debug_note` | 是 | adapter 文本 | 行为不消费 | 仅日志，可移出稳定数据契约 |

### 困倦删除的直接依赖

- 行为：`FocusState::SLEEPY_OR_SLUMPED`、`FocusEvent::is_sleepy_or_slumped`、EventDetector 的 head-down/eyes/posture 分支、FSM 优先级、RiskScorer 固定 80。
- 配置：`sleepy_confirm_ms`、`slumped_confirm_ms`；`head_down_grace_ms` 是否删除取决于是否还要“低头走神”。
- 展示/演示：StatusPage 三处映射、DemoHttpServer 的 `sleepy` override、MockScenario::EYES_CLOSED；相关测试与提醒文案同步清理。

## 验证限制

- 当前 Windows 宿主没有 C/C++ 编译器，仓库旧 CMakeCache 绑定迁移前路径；无法重建完整测试集。
- 旧产物中只有 `test_device_manager` 与 `test_ble_gatt` 可运行，二者均通过，但不覆盖视觉/行为核心。其余 8 个 CTest 项未运行，不能视为通过或失败。
# 2026-07-14 预录演示与通信审查发现

## 2026-07-15 架构基线盘点

- 架构交接最终采用“两层入口”：根目录 `AGENTS.md` 负责一分钟接手，`docs/architecture/current-architecture.md` 负责完整当前态；历史 specs 继续作为决策依据而非重复事实源。
- 新文档已通过源码符号抽查、相对链接存在性、偶数 Markdown fence 和 `git diff --check` 校验。当前工作树原有大量未提交改动，架构文档没有覆盖或回滚其中任何内容。

## 2026-07-15 风扇示例初查

- `_workspace/fan_develop` 目前只有 Linux/Python 原型与 systemd 服务，不是 M33 工程。它通过 `/sys/class/pwm/pwmchip0/pwm0` 驱动 PB10，周期 40,000 ns（25 kHz）；外部驱动反相，因此较小 `duty_cycle` 表示更高转速。
- 原型每 5 秒读取 `cpu-thermal`，按 45/55/65/75°C 五档设置 PWM；温度读取或控制异常时写 8,000 ns，意图进入高转速安全态。
- PB11 转速反馈依赖 Linux `gpiomon gpiochip1 line 11`，每次阻塞采样 1 秒并按每转 2 脉冲换算 RPM；注释指出必须先禁用 `can@402d0000` 才能占用该引脚。
- 当前示例适合验证板级引脚与风扇极性，但直接迁移到 M33 前必须确认：PB10/PB11 的资源归属与 pinctrl、M33 可访问的温度源、Linux/M33 谁拥有 PWM 和 GPIO、remoteproc 启停方式，以及 A35 与 M33 之间的健康/配置通道。
- ST 官方资料确认 STM32MP257 是双 Cortex-A35（最高 1.5 GHz）+ Cortex-M33（最高 400 MHz）的异构架构；M33 可以独立运行 bare-metal/RTOS，并由 Linux/OP-TEE remoteproc 加载，通过 RPMsg/IPCC 与 A35 通信。
- STM32MP2 的外设归属受 RIF 资源隔离约束，不能假设 Linux 已能控制的 PWM/GPIO 可同时由 M33 写寄存器。若迁移风扇，必须在设备树/启动链中把所需定时器、GPIO、内存和中断明确分配给 M33，并禁止 Linux PWM/GPIO 驱动并发占用。
- OpenSTLinux 默认可能由 `st-m33firmware-load.service` 管理演示固件；正式接入前需要盘点当前板端 remoteproc state、firmware、设备树 compatible 和启动服务，避免覆盖一个已在使用的 M33 workload。
- 现有部署脚本已经使用 SSH key 访问 `root@192.168.10.174`，可以在不改板端状态的前提下做只读资源盘点；旧 `develop_example` 中的明文口令不应复制进新实现。
- 2026-07-15 本轮尝试只读 SSH 盘点 `192.168.10.174` 时连接在约 24 秒后超时，未获得任何板端数据，也未改变板端状态。先继续依据本地示例和 ST 官方资料完成方案；板端 remoteproc/RIF/pinmux 事实必须在实施前补验。
- ST 官方 TIM 文档说明 STM32MP25 的定时器可按最终产品需求分配到不同执行上下文；若仍归 Linux，可用 `st,stm32mp25-pwm`；若交给 M33，则应在板级设备树/RIF 中完成唯一归属，而不是让 Linux sysfs 与 M33 HAL 共同访问。
- MP257 内置高精度低功耗结温 DTS，但目前没有足够证据确认这块自研板的当前 A35-TD 配置允许 M33直接拥有或共享 DTS。第一版 M33 风扇方案不应把“能直接读 DTS”当作前提。
- ST 在 2026 年的 external-device-tree 结构中提供 `ca35tdcid-ostl-m33-examples`，说明当前板若为常见 A35-TD/OpenSTLinux 路线，可以保持 A35 主域并把 M33作为协处理器；无需切换整个产品到 M33-TD flavor。
- 工作区未找到自研板的 DTS/DTSI 或现成 STM32CubeMP2 M33 工程，PB10 对应的具体 TIM/通道仍未知；仅凭 `pwmchip0/pwm0` 不能可靠反推出 M33 HAL 实例。该事实必须从正在运行的设备树或 CubeMX 工程补齐。
- “使用另外一个核”应分开理解：两个 A35 通常由 Linux SMP 调度器共同使用，不需要把风扇手工放到第二个 A35；M33 是独立协处理器，需要单独固件、remoteproc/RPMsg 和资源分配。风扇控制更适合后者，但不是为了节省显著 A35 算力，而是为了实时性和故障安全隔离。
- 推荐分两阶段迁移：第一阶段 M33 独占 PWM/转速反馈，A35 小型 bridge 每秒发送 Linux `cpu-thermal` 温度；M33 负责曲线、迟滞、测速和心跳超时全速。第二阶段仅在确认 DTS 可安全归 M33 后，再考虑把温度采集也迁入 M33。
- 风扇不是会话功能：它应作为开机常驻平台服务独立于 `SessionOrchestrator`、视觉 NPU 和 Demo；主程序最多由 `HealthMonitor` 只读消费温度/RPM/故障状态，不拥有 PWM，也不因会话停止而停风扇。
- 用户批准控制增强：70°C 连续 3 秒进入自适应增压，每 5 秒提高约 5% 转速；10 秒趋势下降至少 0.5°C 后保持；温度再次持续上升则恢复爬升；80°C 立即最高安全转速；62°C 稳定 10 秒后限速回落到普通曲线。
- 2026-07-15 实施盘点时，板端 `192.168.10.174` 再次出现 SSH 全程无输出并在 34 秒超时；本轮没有取得新的板端事实，也没有改变板端状态。
- 轻量探测确认板端 ICMP 可达，但单次延迟约 681 ms；并行的 TCP/编译机检查受 10 秒总超时中止，不能据此判断 22 端口或编译机状态。后续改用单独、短命令与更宽总超时，不再把慢网络检查放在同一个并行调用中。
- 最小 SSH 命令随后成功，说明此前长命令是某个 sysfs/systemd 读取或组合命令阻塞，不是板子离线。改成相互独立的短 SSH 后盘点稳定。
- 两个 Cortex-A35 均已在线：`/sys/devices/system/cpu/online = 0-1`。Linux 已在使用双核，M33迁移与“启用第二个 A35”是两件不同的事。
- M33 对应 `remoteproc0`，当前状态 `running`，固件为 `OpenAMP_TTY_echo_CM33_NonSecure.elf`；另有 `remoteproc1` 对应 M0。风扇固件将替换正在运行的 M33 echo 示例，必须先确认它没有被其他服务依赖。
- 当前温区至少有 `thermal_zone0/1`；zone0 类型是 `soc-thermal`，实测约 44.48°C。原设计默认 `cpu-thermal` 与板端事实不符，bridge 应允许板级明确配置 `soc-thermal`，但仍不得任意选择第一个温区。
- Linux `pwmchip0` 指向 `40270000.timer` 的 PWM provider，只有 1 个通道。这首次把 PB10 原型映射收窄到地址 `0x40270000` 的定时器，但仍需从设备树/参考手册确认实例、通道和 PB10 AF。
- 板端两个温区均存在：`soc-thermal` 约 44.38°C，`cpu-thermal` 约 45.31°C；原规格默认 `cpu-thermal` 在此板实际可用，无需修改默认值，但 bridge 仍应显式按 type 选择。
- `pwmchip0/pwm0` 当前已导出；provider 的确切 DT 路径是 `/soc@0/bus@42080000/timer@40270000/pwm`。这支持继续复用现有 25 kHz Linux 基线做迁移前对照。
- `st-m33firmware-load.service` 为 active/exited oneshot，启动脚本 `/sbin/st-m33firmware-load-default.sh start`；当前 RPMsg 暴露 `/dev/ttyRPMSG0` 和 rpmsg-tty/control/name-service 设备。风扇固件可从官方 echo 的启动链和 OpenAMP 资源表迁移，但必须更换 endpoint/firmware 而非与 echo 并存。
- 板型为 `ALIENTEK STM32MP257 Evaluation Board`，compatible 为 `st,stm32mp257d-atk` / `st,stm32mp257`；CAN 节点为 `/soc@0/bus@42080000/can@402d0000`。
- 编译机 `192.168.10.6` 可达且有 CMake，但 PATH 中没有 `arm-none-eabi-gcc` 和 `west`。M33固件不能直接沿用当前 A35 Yocto SDK；还需定位 STM32CubeIDE/CubeMP2 自带工具链或安装独立 Arm GNU Toolchain。
- M33默认服务在 `sysinit.target` 阶段调用 `/sbin/st-m33firmware-load-default.sh`；脚本只识别 `stm32mp257d-atk`，从 `/usr/local/Cube-M33-examples/**/default.stm32mp257d-atk` 找默认项目，并通过项目内 `fw_cortex_m33.sh` stop/start。当前 ELF 位于 `/lib/firmware/OpenAMP_TTY_echo_CM33_NonSecure.elf`。
- `timer@40270000` compatible=`st,stm32mp25-timers`、status=`okay`；其 `pwm` 子节点 compatible=`st,stm32mp25-pwm`、status=`okay`，有 default/sleep pinctrl。板端没有 `dtc`/`fdtget` 命令输出，后续应读取默认项目和原始 BSP/DTS，而不是在板端反编译。
- `can@402d0000` compatible=`bosch,m_can` 但 status=`fail`，与风扇原型所说“禁用 CAN 后 PB11可作 tach”一致；仍需确认 fail 是显式板级状态还是启动失败，以及 PB11当前 line ownership。
- 本轮在编译机 `/opt`/`/home/hu` 未找到 STM32CubeMP2、OpenAMP 或 arm-none-eabi 目录/安装包；本机只有 STM32CubeProgrammer 和 G0 固件包，没有 MP2 firmware package 或 M33 GCC。M33固件构建依赖尚未具备，需要引入官方 STM32CubeMP2 源包和 Arm GNU bare-metal 工具链。
- 首次用带 `-printf` 的板端 `find` 没有列出 Cube-M33-examples 文件，可能是 BusyBox find 不支持该格式或目录层级/链接不同；下一次改用纯 `find -print` 与 `ls`，不复用该写法。
- 当前默认 M33项目完整路径是 `/usr/local/Cube-M33-examples/STM32MP257D-ATK/Demonstrations/OpenAMP_TTY_echo`，板端只包含启动/测试脚本和预编译 ELF，不包含源工程。
- `fw_cortex_m33.sh` 读取 `remoteproc0/fw_format`：TEE 时要求签名 bin，Linux remoteproc 时仅允许 non-secure ELF；脚本会停止已有 firmware、复制 ELF 到 `/lib/firmware`、写 firmware 名称并 start。风扇部署需要复用这一分支逻辑，但不能继续用交互式 rpmsg-tty echo 作为业务协议。
- 默认 echo 测试只是向 `/dev/ttyRPMSG0` 写 512/1024/2048 字节文本；这说明现有 endpoint 是 rpmsg-tty，不是风扇设计的固定二进制服务。正式 bridge 应使用专用 endpoint/char 或清晰的帧边界，不能假定 TTY 单次 write 等于一帧。
- ST 当前 STM32CubeMP2 维护版为 v1.3.1（配套 OpenSTLinux 6.2.1），包含 HAL/CMSIS、OpenAMP v2022.10_20260511、ResourceManager v1.3.1 和 STM32CubeIDE 工程；本项目应匹配该生态版本，不从旧 MP1/M4 示例拼装。
- ST 官方建议 A35-TD 的 M33开发从对应板的 OpenAMP_TTY_echo 工程派生；Developer Package提供完整源代码，Starter Package只有预编译 firmware。当前板端属于后者，无法直接在板上恢复源工程。
- 官方资料强调运行 M33示例需要匹配的 `ca35tdcid-ostl-m33-examples` OP-TEE/RIF 配置，并且当前 MP2固件默认涉及签名/TEE格式。自研 ATK 镜像实际 `fw_cortex_m33.sh` 当前走 ELF 还是 TEE，需读取 `remoteproc0/fw_format` 后决定产物和签名流程。
- 板端 `remoteproc0/fw_format=ELF`，当前可直接加载 non-secure ELF，无需第一阶段处理 TEE签名 bin；这与 ATK 默认脚本路径一致。
- 当前 Linux 风扇基线为 period=40,000 ns、duty=36,000 ns、enable=1、polarity=normal，driver=`stm32-pwm`。这些值可作为迁移前波形和低速档验收基准。
- `gpiodetect` 确认 PB 属于 `gpiochip1`，但筛选的 `gpioinfo` 没显示 PB10/PB11行；下一步需读取完整 chip 输出或内核 pinctrl debug，而不能把“未显示”误判为未占用。
- STM32CubeMP2 官方产品页提供免费软件包，当前配套版本应使用 v1.3.1；源码获取优先走 ST 官方 package/Developer Package，而不是非官方镜像。
- ST 产品页的 `Get Software` 受登录、出口管制申请和 VPN 检查约束，无法由当前非交互工具直接下载；如果本地/厂商 BSP 没有源包，需要用户通过 ST 账户取得 `STM32Cube_FW_MP2_V1.3.1`，或提供 ALIENTEK 配套 M33源码包。
- 在本机 Downloads/Desktop/D盘浅层未找到 STM32CubeMP2/OpenAMP_TTY_echo 源包；并行的编译机其他目录搜索以退出码 1结束且结果未返回，下一步不重复该组合搜索。该依赖不阻塞先实现纯控制核心和 Linux bridge。
- 本机当前没有 `cl`、`g++` 或 `clang++`；编译机 `192.168.10.6` 有 g++ 13.3，可用于不依赖板端 SDK 的纯 C++17 主机测试。
- 风扇控制核心与协议已在编译机以全部警告视为错误通过。控制核心采用最近三帧中值温度，因而阈值语义是“滤波后达到阈值”，不会因单个突刺升档或进入增压。
- 协议 HELLO 的 boot id 不能只作诊断：Linux bridge 重启会让 sequence 从头开始。M33 现以 boot id 变化作为新序号域边界，先回 BOOT_SAFE，再以两帧新温度恢复，避免永久拒收重启后的合法样本。
- 当前 `/dev/ttyRPMSG0` echo 固件会原样返回 bridge HELLO；单凭收到 HELLO 会产生假握手。bridge 现要求对端显式携带 `CapabilityM33Controller`，echo 回来的 `CapabilityLinuxBridge` 不会解锁温度发送。
- 用户提供的 `STM32Cube_ATK_FW_MP2_V1.0.0` 是完整 ATK Cube 包；`package.xml` 实际标记 FW.MP2.1.1.0 + Patch 1.1.1。包内存在与板端默认项目同名的 `STM32MP257D-ATK/Applications/CM33_OpenAMP_DEMO/OpenAMP_TTY_echo` 完整源码、OpenAMP/resource table、CubeIDE 工程与预构建产物。
- 同包还包含 `OpenAMP_TTY_echo_timer`、`OpenAMP_TTY_echo_fdcan` 和 FreeRTOS 变体。timer 示例使用 TIM8_CH2/PZ1，只能复用资源申请与 HAL 模式，不能直接当作 PB10 映射；FDCAN 示例明确 PB11 为 FDCAN1_RX、AF7。
- STM32CubeIDE 1.17.0 已安装在 `C:/ST/STM32CubeIDE_1.17.0`，自带 GNU Tools for STM32 12.3.rel1，`arm-none-eabi-gcc/g++` 可直接运行；`stm32cubeidec.exe` 支持 headless import/build。
- Cube M33 包不含 ATK Linux DTS，但现有 `hardware_bringup` 已保存并验证 v17 风扇 DTB，SHA256 `9a640465...e99e5`：PB10 为 25 kHz `pwmchip0/pwm0`，PB11 为 GPIOB line 11 tach，`can@402d0000` 被禁用以释放 PB11。
- 用户补充：v17 设备树已释放 PB10/PB11，但 Linux 侧 `fan-temp-control.service` 当前仍调用 Python 示例并拥有这两个资源。M33 切换前必须先停止该服务并保存回滚；固件构建期间不停止。
- 厂商开发手册适用于 CM33/OpenAMP/remoteproc 与编译链接基线，不代表自研底板的引脚和电气配置。运行 v17 DT/pinctrl 明确 PB10/AF7 是 `TIM17_CH1N`、pull-down、push-pull、low slew；此前记录的普通 `TIM17_CH1` 结论错误。
- Linux `pwm0 unexport` 不会解除 `stm32-pwm/40270000.timer:pwm` 和父 `stm32-timers/40270000.timer` 的驱动绑定；部署脚本已加入 child→parent unbind 与 parent→child rollback。
- zero-hardware OpenAMP smoke 与 RCC/GPIOB10/TIM17 ResourceManager smoke 均能稳定建立 `/dev/ttyRPMSG0`；说明厂商链接配置和资源请求路径可用。
- 旧版普通 CH1 完整 PWM 初始化在 Linux timer/PWM 完整 unbind 后仍使 A35 失去响应，并约 32 秒后被系统硬件 watchdog 复位；因此 Linux 双占不是唯一根因。
- 复位后板端自动加载默认 `OpenAMP_TTY_echo_CM33_NonSecure.elf`，Linux timer/PWM 均恢复绑定，`fan-temp-control.service` 为 inactive，板端当前处于安全默认态。
- 首次 CH1N 修正版实测不再导致 A35 复位：remoteproc 能进入 running，SSH/串口持续存活；但 2 秒内没有 `/dev/ttyRPMSG0`，部署脚本停止 M33 并成功恢复 Linux timer/PWM。
- 该结果把问题从“危险的普通 CH1/互连挂死”收窄为“板级初始化早于 OpenAMP，无法观察具体停点”。下一版改为 OpenAMP/VIRT_UART 先行，并在资源、时钟、TIM、GPIO、CH1N、控制核心六阶段发送标记。
- 通信先行诊断版成功让 Linux 创建 `rpmsg-tty`，但读取 10 秒没有收到任何阶段标记；系统未复位，脚本按预期恢复默认 echo。
- ATK `VIRT_UART_Transmit` 直接调用 `rpmsg_send`，没有等待 endpoint destination ready、重试或缓存。阶段标记发送失败不会阻塞，但当前实现忽略返回值，因此“无标记”不能证明 M33卡在阶段 0。
- 下一轮应由 Linux 先向 tty 发送显式 `EFAN_PROBE`，M33 RX callback 设置 transport-ready 门闩；主循环收到门闩后才执行板级阶段初始化。这样既建立 destination 地址，也避免依赖无人打开时的 tty RX缓存。
- `EFAN_PROBE` 握手已上板生效；最新轨迹为阶段 0（通信）OK、阶段 1（RIF/ResourceManager）OK、阶段 2（TIM17 时钟频率读取）OK、阶段 3（TIM17 初始化）仅收到 BEGIN，未收到 OK/FAIL。第一处高置信停点已收敛到阶段 3 内部，而非 OpenAMP、资源申请或时钟频率查询。
- 阶段 3 超时后脚本成功停止测试 M33、回绑 Linux timer/PWM 驱动并恢复默认 `OpenAMP_TTY_echo_CM33_NonSecure.elf`；整机未复位，`focus_app.service` 未受影响。
- 将原阶段 3 拆成时钟状态读取、时钟使能/回读、HAL PWM 初始化和 master synchronization 后再次单次上板，取得 `0 OK → 1 OK → 2 OK → 3 OK → 4 OK → 5 OK → 6 BEGIN`。这证明 TIM17 RCC 读取/写入和 `HAL_TIM_PWM_Init` 均可完成，当前第一停点是 `HAL_TIMEx_MasterConfigSynchronization`。
- ST 官方 OP-TEE `clk-stm32mp25.c` 表明独立 RCC RIF 资源编号从 64 起，TIM17 门控不属于该独立编号段；它依赖 TIM17 的 RIFSC 所有权。当前不应凭猜测再申请 `RESMGR_RESOURCE_RIF_RCC(15)`。
- 阶段 6 超时同样未触发 A35 watchdog，脚本成功停止测试固件、回绑 Linux timer/PWM 并恢复默认 echo/RPMsg。
- CMSIS 设备头的 `IS_TIM_MASTER_INSTANCE` 明确不包含 TIM17；`HAL_TIMEx_MasterConfigSynchronization` 入口会因此触发参数断言并进入错误处理循环。TIM17 的独立 PWM 不使用 TRGO 或主从同步，该调用来自 TIM8 厂商示例的错误迁移，应该删除而不是绕过断言。
- 删除无效 master-config 后的首次完整续跑越过了原停点，但本地部署客户端在 14 秒被执行器强制终止，未保留 RPMsg 阶段输出，也未能执行脚本回滚；随后板端失联并由约 32 秒硬件 watchdog 重启。启动后自动恢复默认 echo、RPMsg 和 Linux timer/PWM 驱动。
- 因该次缺少最后阶段标记，不能把重启直接归因于 GPIO、channel config 或 PWMN start 中任一项。下一步必须加入编译期 `stop-after-stage`，先只跑 GPIO，再只跑 channel config，禁止再次把三种硬件写入合并测试。
- `stop-after-stage=6` 的阶梯测试完整到达 stage 6 OK，PB10 的 AF7/pull-down/low-speed GPIO 配置不会导致系统挂死；脚本在 3 秒诊断窗口后主动停止 M33并恢复默认 echo/驱动。
- `stop-after-stage=7` 的第二阶梯完整到达 stage 7 OK，`HAL_TIM_PWM_ConfigChannel(TIM17, CH1)` 同样安全；因此剩余危险边界唯一收敛为 stage 8 的 `HAL_TIMEx_PWMN_Start` 内部序列。
- 将 PWMN 启动拆成 `CC1NE → MOE → CEN` 后三步均已通过；关键外设写入需要 `DSB + 1 ms` 后再读回，直接写后立即判断会得到尚未生效的旧值。
- TIM17 显式复位、Break/Break2 禁用，BIF/B2IF 均为 0；stage 8（实际 PWM 输出）连续运行 40 秒无复位。
- 完整固件原先在 stage 9 的 `HAL_GetUIDw0()` 停住。该 UID 寄存器不可由当前 M33 non-secure 上下文直接读取，A35 随后因 32 秒系统 watchdog 整板复位。
- boot_id 已改为 `HAL tick + SysTick VAL + TIM17 CNT` 组成的启动 nonce，完全移除 UID 读取；应用初始化、CCR 写入和完整 100 ms 控制循环连续运行 60 秒，remoteproc 与 RPMsg 均保持 running。

- 仓库没有 `AGENTS.md`；后续 agent 当前只能从历史规格、计划和源码自行恢复上下文，因此需要新增一个仓库级唯一入口。
- 已有架构依据分散在三类文件：`docs/superpowers/specs/2026-07-14-focus-app-session-architecture.md`（会话中心目标架构）、`docs/superpowers/specs/2026-07-14-cross-device-demo-and-bluetooth-contract.md`（展示与蓝牙契约）、`docs/integration/app-band-protocol-v2.1.md`（跨设备协议）。新文档应引用它们，而不是复制成第四套互相漂移的规范。
- Git 最近落地顺序为会话架构文档 `75f175d` → SessionRuntime/核心迁移 `5ca243b` → 主程序融合 `53be1f3` → 展示空闲态修正 `9bc2b8e`；架构说明必须以这些已落地代码和当前未提交的板端演示增强为准。
- `rg` 在当前 Windows 沙箱仍被系统拒绝执行；本轮文件与符号盘点改用 Git/PowerShell 原生检索，避免重复尝试同一失败路径。
- 已批准的会话规格定义了稳定顶层边界：`ProcessServices`、唯一生命周期所有者 `SessionOrchestrator`、每会话隔离 `SessionRuntime`、唯一判定者 `DistractionCore`、只读 `SessionSnapshot`，以及可降级的会话插件。该结构应作为总架构主干。
- 跨设备/展示规格补充了两个互斥运行域：正式 `SessionRuntime` 与非正式 `DemoRuntime`；正式会话拥有相机/NPU最高优先级，演示不得写正式统计或投递提醒。
- 规格与当前实现已有可见漂移，架构基线必须标注“当前事实”：展示页现在是三个显式入口（现场摄像头、预录 NPU、纯前端演示），纯前端使用板端真实预跑的 pre-log；r04/r05 已因正常写字误报趴下而删除；两类预录均按剩余编号片段连续轮换，而不是单一循环。
- 会话规格中云台是未来可选插件，但当前构建/服务环境强制禁用；新文档应同时表达“架构插槽存在”和“当前发布不可启用”，防止 agent 看到旧类就误接回生产入口。
- 当前源码是“已收束但仍保留兼容投影的混合架构”，不能把目标规格原样描述成现状：`main.cpp` 中 `CameraPipeline`、`ReminderPolicy`、`DeviceManager`、SQLite、BLE、Health 仍作为进程级 `Module` 常驻；每次正式会话的 `SessionRuntime` 当前只挂载两个 required feature：`LegacySessionProjectionFeature` 与 `DistractionPipelineFeature`。
- `SessionOrchestrator` 是正式会话唯一状态机所有者，串行化 `IDLE/PRECHECK/FOCUSING/PAUSED/ENDING`，只接受 `LiveCamera`，并通过 before/after hook 与 `BoardDemoRuntime` 互斥争用相机/NPU。
- `CameraPipeline` 虽是常驻线程，但通过兼容 `SystemStateStore.session.active` 按需开关相机；IDLE 释放相机，PAUSED 约 1 FPS。也就是说“常驻模块”不等于“常驻占用相机/NPU”，文档必须把线程生命周期和硬件资源生命周期分开画。
- 当前相机启停仍由 `LegacySessionProjectionFeature` 把新会话阶段投影到旧 `SystemStateStore` 驱动；这是有意的迁移适配层，而非新的领域契约。新 agent 不应继续向该层增加业务状态。
- `DistractionPipelineFeature` 是正式会话中模型和时序状态的所有者：prepare 时创建 STAI Pose、Portable MLP、PhoneFact session 和 Fusion session；FOCUSING 才生成正式 assessment，PRECHECK/PAUSED 只更新视觉/快速恢复证据；stop 释放模型、窗口与队列。
- 正式判定数据面为 `CameraFrame -> STAI Pose -> subject/keypoints -> OnlineProneClassifier -> PhoneFactBuffer -> DistractionAssessmentEngine -> SessionSnapshotPublisher`。`updateEvidence()` 在同一锁内原子更新 visual/phone/assessment，HTTP/App 应读它而不是拼接多份旧状态。
- `DistractionPipelineFeature::publishCompatibility()` 仍把新 assessment 派生为旧 `BehaviorContext`，供进程级 `ReminderPolicy` 等迁移中的消费者使用。这是当前最重要的技术债边界：判定真值在新核心，提醒执行仍经过兼容投影。
- `BleGattServer` 是进程级 Phone GATT Server，内部组合 `AppCommandService` 与 `AppEventOutbox`；正式会话命令进入 `SessionOrchestrator`，手机事实进入 `PhoneFactBuffer`，状态读取 `SessionSnapshotPublisher`。手环由 `DeviceManager -> BleBandController` 作为板端 GATT Client 侧执行。
- `BoardDemoRuntime` 拥有独立 FrameQueue、PhoneFactBuffer、SnapshotPublisher 和 DistractionPipelineFeature，不复用正式 session 状态；正式会话通过 Orchestrator hook 先占用资源并停止 Demo。其三态为 LIVE_CAMERA 等待窗、RECORDED_NPU、FRONTEND_REPLAY。
- CMake 的生产目标已经排除旧 `VisionEngine/BehaviorEngine`，但仍编译 Gimbal/Screen/Sound 源文件，`DeviceManager` 也仍持有 Screen/Sound controller；当前板端依靠主入口不注册 Gimbal 且 systemd 环境强制 `EDGEFOCUS_DISABLE_LCD/SOUND/GIMBAL=1` 保持不可达。文档必须把“源码保留/已编译”和“生产入口启用”区分开。
- 部署链为 Windows 工作区 → `192.168.10.6` 交叉编译机（STM32MP2 SDK）→ ARM release tar → `192.168.10.174:/opt/edgefocus`，包含板端旧版备份、启动健康检查和失败回滚；大视频按 SHA-256 独立同步，不进入源码压缩包。
- 当前提醒链仍是迁移态：新 Fusion level 被映射为旧 risk score（0/25/60/90），进程级 `ReminderPolicy` 再按 risk 阈值还原 `CommandLevel`，经 `NotificationRouter` 分成 App 路由和板端动作。文档应把这标为兼容桥，禁止新功能绕回 risk score。
- App 提醒通过 `SystemStateStore::pushEvent -> BleGattServer -> AppEventOutbox -> BlueZ notify`；手环提醒通过 `ReminderPolicy -> DeviceManager -> BleBandController` 的独立异步队列、TTL 和重连重试。App 与手环不是同一 GATT 角色。
- `DeviceManager` 当前仍能执行 SCREEN/SOUND/BAND 三类 legacy channel，但板端发布配置把 screen/sound route 指向 phone，且 systemd 禁用板载输出；对外产品语义应写成 APP + BAND，不能让 agent 看到 enum 后误以为板载屏/声仍是产品功能。
- Demo 的 `RECORDED_NPU` 复用同一 `DistractionPipelineFeature` 类型但拥有独立实例；每个 pNN 片段 EOF 后切下一片并 resetContinuity。`FRONTEND_REPLAY` 不运行 NPU，页面按 rNN 视频时间读取同名 pre-log；当前素材为 10 组（r04/r05 已删除）。

- 可用素材位于 `code/distraction_core/dist/DistractionCapture/data/captures/`，P01/P02/P03 均同时保留 raw WebM 与 normalized AVI。
- P01 已有 normal、prone、phone_use、not_present，可组成单个完整演示循环；此前已知 P02 的看桌面片段为脏数据，因此不选 P02。
- 最稳定的回退不是回放最终 assessment，而是按视频 `position_ms` 回放视觉证据，再与当前页面手机事实进入同一 FusionCore；这样回退来源清楚且手机交互仍然有效。
- 摄像头模式与预录模式必须使用不同的手机事实权限：LIVE 只读 BLE/App 上报，RECORDED 允许 demo-manual，跨来源事实不得串入另一会话。

## 2026-07-14 预录资源生命周期回归

- 二次进入只有姿态层、没有底图的直接原因在浏览器：退出时移除 `<img src>` 不保证触发 `load/error`，`frameBusy` 可能永久保持 true，导致后续不再请求 JPEG。
- 退出预录时必须同时复位 `frameBusy`、移除 JPEG `src`、暂停并卸载 `<video src>`；验证状态为 `imageSrc=null`、`replaySrc=null`、`networkState=0`。
- 自动 NPU 超时回退与人工退出存在竞态；自动回退现在在持有生命周期锁后重新确认仍是 `RECORDED_NPU`，旧监视结果不能取得新模式所有权。
- 六段素材均为 H.264/yuv420p、1280×720、15 FPS、约 29～31 秒。运行时只扫描 `pNN.mp4` 并按进入次数顺序轮换，旧 `p01_showcase.mp4` 不会混入编号片段。
- 媒体已从源码/编译归档中拆出；部署按本地/板端 SHA-256 对比，仅原子上传发生变化的片段。
- 板端空闲线程 16，预录运行时 25，退出后回到 16；演示状态为 LIVE/IDLE、preview=false，证明演示解码和推理工作线程不在后台运行。
- 页面切换瞬间可能有一个已经在途的 JPEG 请求在服务端清空预览后得到 503；资源随后完全卸载，不构成持续后台请求或功能故障。
- 板端时区已设为 `Asia/Shanghai`，系统时间和 RTC 已同步；2026-07-14 21:51 实测时间正确，采用手动时钟、NTP 关闭。

## 2026-07-14 正常链路预日志与纯前端回退

- 回退视觉状态不得人工划段或手填。完整 `no_npu/预录.mp4` 已在板端按正常 YOLO11n Pose + Temporal MLP + Fusion 链路运行一次，原样保存 1318 个实时样本，再仅按媒体时间机械拆分为 `r01.trace.json`～`r12.trace.json`。
- 手机事实是唯一人工时序输入：302 秒娱乐、322 秒其他、342 秒学习、352 秒未使用；视觉字段、趴下时序结果、融合输出和证据关键点均来自该次真实 NPU 运行。
- 纯前端模式加载当前 `rNN.mp4` 的同名 `.trace.json`，按 `<video>.currentTime` 选择最近样本；不运行后台 NPU，也不开放手机按钮。
- NPU 预录输入缺失时已实测自动进入 `FRONTEND_REPLAY`，原因 `recorded_video_missing`；撤销故障变量后服务恢复 `LIVE_CAMERA / IDLE`。
- 板端 HTTP 的 2 秒发送超时会在较慢局域网中截断 MP4，造成再次进入时只有关键点而无底图。发送超时提高到 15 秒后，r01 的 3,889,893 字节完整下载，客户端与板端 SHA-256 一致。
- Playwright 二次进入回归：视频 `readyState=4`、1280×720、持续播放，页面控制台 0 错误；视觉证据叠加和近 30 秒人在/趴下/手机/综合等级四轨均存在。
- 原实现的两条演示路径都循环当前文件：前端 `<video loop>` 固定 rNN，NPU 解码 EOF 固定回到同一 pNN。现改为分别连续轮换 rNN/trace 与 pNN；NPU 换片时重置时序连续性。
- 人工复核确认 r04/r05 是不适合作为展示证据的误报样本：正常写字时驼背/低头过深，被当前模型判为趴下。两段视频及对应预日志已从本机和板端移除，播放列表保留编号空缺并自动从 r03 跳到 r06。
## 2026-07-14 App / 手环通信审查

- App GATT 推送当前是易失的：`SystemStateStore::pushEvent()` 只有一个即时回调，手机未订阅通知时 `BleGattServer` 直接丢弃，没有 outbox、重发或送达 ACK。
- `ReminderPolicy` 在真正发出 BLE 通知前就把所有 phone route 写入 `last_delivered_channels_mask`，因此“已送达”状态可能是假阳性。
- `push_alert` 没有接入已有 `ReminderSnapshot.event_id/episode_id/state/delivered_to/acknowledged` 契约；`reminder_ack` 也不校验事件、会话或当前提醒，旧 ACK 可能改变新提醒的冷却。
- `phone_sync` 新链路默认 `fresh_for_ms=1000`，对 BLE 心跳过紧；短暂调度或链路抖动会让手机事实闪断。建议 App 状态改变立即上报、每 2 秒心跳，板端有效期 5 秒。
- 手环 `sendAlert()` 在断连时只记录 `skipped: disconnected`，日志虽写“queued diagnostically”但没有真正队列；写失败同样不重试。
- 手环写入是同步 5 秒 DBus 调用，发生故障时会阻塞 DeviceManager 动作处理；连接检测周期约 3 秒，重新发现阶段可更长。
- 当前测试覆盖命令成功路径、分片和部分路由，但没有覆盖 App 断连重连补发、事件去重/ACK、手机心跳过期边界、手环断连重试与过期丢弃。

## 2026-07-15 M33 BSP 与 PB10/PB11 实施事实

- 用户提供的 ATK 包目录名为 `STM32Cube_ATK_FW_MP2_V1.0.0`，但包元数据实际为 `FW.MP2.1.1.0` + `FW.MP2.1.1.1` patch；完整包含 Drivers、Middlewares、Projects 和 Utilities。
- 可复用的完整基线工程是 `Projects/STM32MP257D-ATK/Applications/CM33_OpenAMP_DEMO/OpenAMP_TTY_echo`；CubeIDE 工程配置位于 `STM32CubeIDE/CM33/NonSecure`，配置名为 `CA35TDCID_m33_ns_sign`，会生成 ELF、stripped ELF 和 OP-TEE signed bin。
- 本机 CubeIDE 1.17.0 与 GNU Tools for STM32 12.3.rel1 可用；`.cproject` 同时配置 MCU GCC 与 MCU G++，因此可在 ATK 工程内编译现有无异常、无动态分配的 C++ 控制核心。
- ATK `OpenAMP_TTY_echo_timer` 仅证明 HAL/ResourceManager/OpenAMP 的组合方式，其示例是 TIM8_CH2/PZ1，不能直接作为 PB10 映射。当前板端已验证 DT 事实为 PB10=TIM17_CH1/AF7、25 kHz、外部驱动反相。
- BSP 定义 `STM32MP25_RIFSC_TIM17_ID=15`、`GPIO_AF7_TIM17=7`；PB10/PB11 分别应申请 `RESMGR_RESOURCE_RIF_GPIOB` 的 pin 10/11。PB11 在 FDCAN1 示例中是 AF7 RX，因此 Linux FDCAN1 必须保持禁用后才能作为 tach GPIO。
- `hardware_bringup` 的 v17 DTB SHA-256 为 `9A6404656A6ED354F22D16D1CB1EA800EF064BBB876A0274FEEE08EB426E99E5`，已验证 Linux 原型通过 `/sys/class/pwm/pwmchip0/pwm0` 控制 PB10，PB11 使用 GPIOB line 11；M33 接管前不仅要停服务，还要解除 PWM export/驱动 pinctrl 占用并验证 RIF 请求成功。
- Linux 原型服务名为 `fan-temp-control.service`，执行 `/usr/local/bin/fan_temp_control.py`。用户确认该服务停用后风扇仍会按硬件默认态正常转，因此切换窗口不依赖 Linux 服务维持供风。
- 当前原型 tach 默认 `2 pulses/rev`；固件先保留该可配置默认值，最终 RPM 准确性仍需按实物风扇规格或转速对照校准。
- 用户提供的《ATK-DLMP257B异核通信快速开发手册》再次确认：M33 使用 `remoteproc0`，Linux remoteproc 模式加载 non-secure ELF，首个 VIRT_UART 暴露为 `/dev/ttyRPMSG0`；外设交给 M33 前必须先从 Linux 设备树/驱动侧释放。
- 厂商 `Utilities/scripts/fw_cortex_m33.sh` 的实际顺序为复制 ELF 到 `/lib/firmware`、写入 `remoteproc0/firmware`、再写 `start`；当前 `deploy_m33.py` 按相同顺序实施，并额外串行停止旧 bridge、旧 M33、Linux 风扇服务和 PWM export，避免双写 PB10/PB11。
- CubeIDE 1.17 随附的 patched GNU Make 在把 C++ 加入厂商 C-only 工程后可复现 `0xc0000005`；改用同一 GCC 12.3 工具链配合独立 CMake/Ninja 后构建稳定，不改变厂商 startup、linker script、HAL、OpenAMP 或 resource table。
- 最终 stripped ELF 为 ARM ELF32 little-endian、hard-float EABI、入口 `0x80a01f41`；大小 text=29,716、data=352、bss=3,424 bytes，总计 33,492 bytes。
- 后续已恢复 `192.168.10.174:22` 连接并完成受控上板；以上“尚未部署”是中间状态，不再代表当前板端。

## 2026-07-15 M33 风扇最终手动验证事实

- 分阶段标记确认 TIM17_CH1N 的 CC1NE、MOE、CEN 与 CCR 写入均正常；完整 100 ms 控制循环稳定运行。
- 先前整板 watchdog 的直接根因是 non-secure M33 调用 `HAL_GetUIDw0()` 导致总线挂起；以本地运行计数 nonce 代替后，应用初始化与持续运行均不再复位。
- Linux bridge 正常退出会发送 `ShutdownNotice`。原逻辑把 `transport_ready_` 清除后，新 HELLO 虽能解码却无法回 HELLO；有效 Linux HELLO 现在同时恢复 transport 与 protocol，并由主机测试覆盖。
- bridge stop 6 秒再 start 的板上实测顺序为：初始 `BOOT_SAFE` → 约 1 秒 `FAIL_SAFE` → 第二帧温度后约 2 秒 `CONTROLLED`；未重启 A35 或 M33。
- 后续 52 秒观测保持 `protocol_ready=true`、`CONTROLLED/NORMAL`、`last_error=""`。Linux timer/PWM 驱动未重新绑定，`focus_app.service` 保持 active。
- 当前只完成手动运行验证：bridge unit 是 static，旧 `fan-temp-control.service` disabled，M33/bridge 未设开机自启动；PB11 tach 仍禁用。

## 2026-07-15 M33 风扇极性、测速与服务化复核

- TIM17_CH1N compare 已按 `period_ticks-logical_ticks` 修正；8,000 ns→CCR 32，36,000 ns→CCR 4，方向与 Linux 原型一致。
- PB11 M33 EXTI 已启用。2 PPR 下 36,000/30,000/22,000/14,000/8,000 ns 实测约 600/1,770/3,060/4,440/5,760 RPM，严格单调；34,000 ns 常温档在多次启动间约 660～1,140 RPM，用户确认常温档允许偏低。
- 1/2/4 PPR 满速候选约 11,520/5,760/2,880 RPM，只有 2 PPR 落入额定 6000 RPM 的 ±25% 窗口；仍保留风扇接口波形独立测量门禁。
- Linux fallback 从全新 export 和已配置状态各通过，并连续两次幂等执行；40,000/8,000/normal/enable=1 读回一致，约 5,880～6,120 RPM。
- M33 与 Linux 均停止、timer/PWM unbound 30 秒时记录 7,009 个 PB11 上升沿，硬件默认自转路径客观存在。
- loader 的 tty 等待改为先确认旧节点消失、再等待新节点最多 15 秒；解决 udev/RPMsg 重建超过 6 秒的误失败。
- 共享 `/run/edgefocus` 不再由 bridge 停止时删除；transition 在 stop 后重建目录，bridge unit 使用 `RuntimeDirectoryPreserve=yes`。
- 四个 systemd unit 和校准文件已安装但未 enable；校准门禁失败会触发 Linux fallback。厂商 echo loader 保持 enabled。
- 用户在已有单调转速、额定满速、两次幂等 fallback 和 30 秒硬件默认自转证据基础上明确豁免外部波形截图；校准记录使用独立 `waveform_verification_waived` 字段，`waveform_verified` 继续保持 false。
- 首次开机验收发现 bridge 的 `ConditionPathExists=/dev/ttyRPMSG0` 会在 M33 loader 创建节点前被 systemd 提前判定并跳过；移除该 condition，改由 `Requires/After=edgefocus-m33-fan` 和 bridge 自身重连负责启动时序。
- 修复 condition 后再次冷启动，`After=dev-ttyRPMSG0.device` 又因该 rpmsg-tty 节点未注册为 systemd device unit 而把 multi-user 拖延约一分钟并取消 bridge job；loader 已亲自等待和打开 TTY，因此移除该伪 device-unit 排序依赖。
- 再次启动发现 transition 的通用 `start` 会执行 `systemctl stop edgefocus-fan-bridge`，从 M33 unit 内调用时会取消 systemd 已排队但尚未执行的 bridge start job。现改成仅手动切换显式传 `--stop-bridge`；生产 unit 由 `Before=edgefocus-fan-bridge` 保证旧 bridge 不会并发。
- 修复后两次完整 systemd reboot 均自动启动 M33、唯一 bridge、健康门和 `focus_app`；健康门分别取得当前 boot 的新状态，Linux timer/PWM 保持 unbound，fallback 未误触发。物理断电重上电未执行。
- 常温 34,000 ns 在多次启动和稳定期观察到约 660～1,140 RPM；按用户低噪声偏好把 band 0 健康窗设为 600～1500 RPM，整体安全窗仍为 300～7500 RPM。
- 用户执行物理断电重上电后，M33 stage 9、唯一 bridge、健康门和 focus_app 全部自动恢复；boot id `567037614887`，健康门启动时 840 RPM，稳定后约 690 RPM，Linux timer/PWM 保持 unbound。
- 同次断电确认板端 RTC 后备不保持：RTC 回到 2000-01-01、Linux 初始时间为 2024-02-28。本轮已从 Windows 主机同步到 2026-07-15 并 `hwclock -w`；这不证明下一次断电能保持，应由 App/联网同步兜底。
# 2026-07-15 云台与异构调度边界（设计盘点中）

- 用户明确冻结 M33 功能开发：M33 保持现有独立风扇安全控制，不再承担云台或充电控制。
- 云台控制链确定为 STM32MP257 A35/Linux → UART4 → STM32G431 → 电机驱动；G431 负责电机实时控制，A35 只发送有界的高层目标和安全命令。
- 用户提供的 G431 文档规定：`G431_ENABLE_IN` 高有效、硬件下拉默认禁能；低→高会触发电角度对齐，必须等待 `BOOT_ALIGN,OK` 后才能运动；正常停机顺序是 UART `STOP` 成功后再拉低使能。
- 文档中的 `BOOT0_CTRL` 高电平只决定复位时进入 ROM Bootloader，低电平决定启动用户固件；单独改变 BOOT0 不会使正在运行的 G431 重启。因此用户所述 MP257 PD4 的“重启”连接语义仍需结合底板原理图或现有设备树确认。
- 用户说明 MP257 PD7 为 `G431_ENABLE_IN`；PD4 与 G431 主控重启相关。任何 GPIO 写入前必须确认 PD4 实际连接的是 NRST、复位电源控制，还是 BOOT0 控制。
- 硬件侧随后确认 MP257 PD4 只连接 `BOOT0_CTRL_G431`，不连接 NRST，单独改变 PD4 确实不会重启 G431。正常产品流程不得暴露“软件重启 G431”；固件升级进入/退出 Bootloader 需要人工 Reset 或重新给 G431 上电。
- Markdown 示例的正常运行路径不依赖 NRST：BOOT0 保持低，Enable 先低 100 ms 再拉高，等待 `BOOT_ALIGN,OK`；NRST 仅是外部测试主机的可选连线。
- `G431_FAULT_OUT` 在 G431 板侧为 PC12 开漏低有效，但 MP257 侧 GPIO 映射尚未提供。该输入映射是安全状态机接入硬件故障快速撤销 Enable 的剩余关键事实。
- 硬件侧确认 `G431_FAULT_OUT` 接 MP257 PD6。最新网表也确认该网络连接 U1.B78 与 G431 U17.54；板端 PD6 当前未被占用，可作为低有效故障输入。
- 最新网表确认 STUSB4500QTR 位于 I2C4：MP257 PD10=`I2C4_SDA`、PD11=`I2C4_SCL`，同总线还连接 MP2762 充电芯片。板端 pinctrl 已将 PD10/PD11 配成 I2C4 AF6，Linux 映射为 `/dev/i2c-1`（`i2c@40150000`）。
- STUSB4500 的双选地址电阻实际装配不是最初按网表公共端猜测的 0x28；对官方允许的四个候选地址做只读 Device_ID 探测后，仅 `0x2A` 应答，Device_ID=0x25。
- 在当前未接外部电源时，STUSB4500 `PORT_STATUS_1(0x0E)=0x00`（unattached），`TYPEC_MONITORING_STATUS_1(0x10)=0x04`（vSafe0V），与实物状态一致。
- ST 官方 UM2650 定义 `0x10 bit3 VBUS_READY`：1 表示 VBUS 已连接且处于 vSafe5V 或协商电压；`0x0E bit0 ATTACH` 可作连接交叉确认。生产检测应读取静态状态寄存器，避免无目的读取会清事件的 `0x0D/0x0F`。
- 现有 `DeviceStatus` 仍含虚假的 `battery_percent=67`、电压、charging 字段；App/会话/演示状态多处对外返回这些字段。用户明确硬件已删除电量读取，因此应删除电量显示与伪造回退，只保留 `external_power_present` 和 Home/Go 档位。
- 现有 `DeviceManager::normalizePowerStatus()` 和 App 设置路径会反复用 `battery_charging` 覆盖 `power_profile`，导致用户手动切换实际上无法保持；新设计必须拆分“供电事实、自动默认档位、用户手动覆盖”。
- 用户选择供电策略 A：手动 Home/Go 覆盖仅在当前供电阶段有效；检测到外部供电插入或拔出后清除覆盖，并按“有外部电源=Home、无外部电源=Go”重新自动选择。
- 用户选择 Go 模式方案 A：仅降低应用层工作频率，不在首版修改 Linux CPU governor/DVFS。建议降频对象为视觉/NPU 推理采样、云台目标更新和非关键状态刷新；PD6 故障、STUSB 供电检测、BLE 心跳与 G431 电机内部控制频率保持不变。
- 当前 `PowerProfile` 对性能的实际作用几乎为空：只有 `DeviceManager` 空闲睡眠由 Home 50 ms 变为 Go 150 ms；视觉/NPU、相机和主要会话链未消费该档位。
- 用户选择云台首轮方案 A（分阶段 bring-up）：先验收 GPIO/UART、自动对齐、PD6 故障闭锁、STOP 和受限小角度手动运动，稳定后再接人脸跟踪。
- 用户强调正式 PRECHECK 也必须经过同一安全门：先握手/对齐/Fault/STOP，再允许受限试转与用户手动调节；只有 Ready 后才能执行找人或后续跟踪。
- 用户批准接入方案 2：进程级 `GimbalHardwareService`/`PowerSourceService` + 会话级 `GimbalSessionFeature` + 统一 `WorkloadProfilePolicy`；方案 3 的 Linux 内核驱动化仅列入进入国赛后的升级计划。
- 用户细化 PRECHECK：云台在限位内覆盖上下左右一次；运动过程中一旦识别到可用人物就停在当前位置作为自动调整结果；整轮未找到则回默认/中心位置，随后允许用户手动调整。
- 示例没有给出可直接复用的机械绝对限位，只使用 0~360°环绕和速度限制。因此本产品首轮不能照抄全角度目标；应以当前/默认中心为基准配置保守扫描偏移，先用小角度 bring-up 实测后再扩大，且所有 App 手动命令也必须经过同一软件限位夹取。
- 趴下时序 MLP 已按真实 `window_ms=2000` 对输入特征插值重采样，因此适度改变推理 FPS 不会改变窗口时长；但 `maximum_gap_ms=500` 会在相邻有效特征超过 500 ms 时清空连续性。Go 模式不应降到 2 FPS 临界值，建议首版目标约 3 FPS，Home 保持板端可持续约 4 FPS。
- G431 UART 协议为 115200 8N1，提供 `PING/ANGLE/STAT/HOLD/PITCH/YAW/SET/TRACK/STOP/FAULT` 等命令，并可能异步输出 `BOOT_ALIGN`。
- 现有 `GimbalController` 默认打开 `/dev/ttySTM1`，当前轮询和预检命令仍带旧协议假设；现有代码没有管理 PD4/PD7，也没有把 `BOOT_ALIGN` 作为显式安全门。
- 现有 `GimbalMonitor` 是进程常驻轮询模块，预检会发送旧式 `CENTER X`、`DEG Y 0`；尚未实现可靠的会话级控制权、自动跟踪或 GPIO/UART 联合状态机。
- G431 单向诊断证明 MP257→G431 方向至少在 Linux UART 发送计数上完整：`PING\r\n`、`HOLD 1\r\n`、`STOP\r\n` 共 20 字节，ttySTM1 tx 从 31 精确增加到 51；反向仍为 rx=0。因此是否真正接收并执行命令只能由 STOP 后卸力这一现场现象确认，软件输出不得宣称“控制成功”。
- 后续一次性 `PITCH 5` 序列又使 ttySTM1 tx 精确增加 29 字节至 80，仍为 rx=0、无 Fault。若现场确认有电机声或角度变化，即可把故障高置信度收敛到 G431 TX→R78→PG15 返回链；若完全无响应，则 MP257 TX 的内核计数仍不足以证明信号经过 R77 到达 G431 PC11。
- 现场最终确认 `PITCH 5` 无电机声且无角度动作，属于上述“完全无响应”分支。硬件排查应同时覆盖 MP257 PG14→R77→G431 PC11 的命令输入链、G431 UART 初始化/引脚复用/115200 8N1/CRLF 解析，以及 G431 PC10→R78→MP257 PG15 的回传链；不能只检查 RX 返回方向。
- 当前 Wi-Fi 故障与云台 UART 分离：RTL8723DU 此前反复 USB 重枚举，本轮最终由 `wpa_state=INTERFACE_DISABLED` 且板端无 IPv4 地址确认掉线。用户指出优先检查网卡本体和天线松动；不要把该现象归因于云台诊断。

## 2026-07-16 App 唯一交互渠道初步盘点

- 主机仓库已有 `AppCommandService`、`BleGattServer`、`SystemStateStore`、`NotificationRouter`、`AppEventOutbox`、`BleBandController` 等边界，可以在不扫描 App 工作区的前提下先审查主机对外契约。
- 当前状态模型已经包含 PRECHECK、提醒、手环写入诊断和云台预检字段，但“字段存在”不等于 App 已有对应页面、操作入口或可靠时序，必须逐项核对命令入口、事件推送、ACK 与恢复语义。
- 手环控制器本身已有连接线程、按名称/MAC 查找和连接维护迹象；尚需确认 App 是否能显式触发扫描/重连、获取进度与失败原因，以及该操作是否会与后台自动重连冲突。
- 主机已经把 `screen_output_route` 和 `sound_output_route` 默认固定为 `phone`，板载屏幕/声音能力会因此上报不可用；提醒事件 `push_alert` 携带 `phone_actions`，其中 `SCREEN`/旧 `LIGHT` 都映射到手机屏幕渠道。因此 App 需要真正执行屏幕闪烁/弹窗，并在展示后回传 `event_ack(stage=presented)`，否则主机只能知道“已推送”，不能知道“用户看见了”。
- 关键提醒和会话结束已有 outbox：协商 `event_ack_v1` 与 `event_dedupe_v1` 后按 0/500/1500/3000 ms 重试，提醒投递 TTL 10 秒、会话结束 60 秒，ACK 支持 `received → presented → user_ack`。App 必须按 `event_id + session_id` 去重并分阶段确认。
- PRECHECK 状态已经嵌入 `get_current_state/session_state`：包含 session phase、相机质量/提示、约 1.2 秒间隔预览图元数据、控制权 owner，以及可选云台状态；App 首页与控制面板应消费同一份状态模型，避免各自维护一套会话判断。
- 手环后台自动连接已存在：启用后按名称/MAC 扫描、连接、断线后约 3 秒重试；当前 App 命令表没有显式“扫描/立即重连/选择目标/连接进度”命令，用户记忆的缺口属实。新增入口应触发后台状态机而不是在 App 侧直接连接手环，因为手环由主机蓝牙控制。
- 既有架构文档明确规定 App 承担显示和声音、PRECHECK 连接/确认手环，并要求 App 提醒支持 ACK/重试/去重；但没有记录“会话期间保持亮屏/前台屏保页”的实现或验收要求，因此该能力目前只能视为待审查项，不能假定已经完成。
- 文档目标是生产路由只保留 APP/BAND，而当前兼容实现仍用 `phone_actions=[SCREEN,SOUND]` 表达 App 内应采取的呈现动作。新 App 契约应区分“投递目标 APP/BAND”和“App 呈现动作 POPUP/SCREEN_FLASH/SOUND/VIBRATE/NOTIFICATION”，避免继续把手机屏幕误写成板端硬件通道。
- 用户确认 App 前台电源语义：无论设置中的屏保开关是否开启，只要 App 位于前台就必须阻止设备自动锁屏；这不是持续亮屏。
- 屏保开启时，达到用户可调的自动黑屏时间后进入纯黑 App 页面，设备和进程保持唤醒，蓝牙/会话继续运行；屏保可在设置页关闭。实现审查必须区分“App 绘制黑色页面/降低亮度”和“系统真正熄屏并触发锁屏”，后者不符合需求。
- 用户选择屏保提醒策略 A：黑屏状态收到提醒时临时展示全屏提醒并执行闪屏；一级数秒后自动回黑，二/三级等待用户确认后回黑。App 在实际显示时回 `presented`，用户确认时再回 `user_ack`；一级自动收起不能伪造 `user_ack`。
- 用户进一步修正：首页与控制面板是两个独立页面，但核心功能等价；横屏控制面板额外显示本次会话统计。两页应共享同一业务控制器、状态源和命令封装，PRECHECK 能力在两页都完整可用，禁止各自复制会话逻辑。
- 手环未连接不阻塞 PRECHECK 或正式会话，作为可选提醒增强显示降级状态。普通 App 设置页与横屏控制面板内的设置页都必须提供主机侧手环“主动搜索/连接”和连接状态入口；两处复用同一设备状态与命令，手环仍由主机 BlueZ 管理而非手机直连。
- 用户选择手环搜索策略 A：主机优先自动重连上次成功手环；目标不可用时扫描同款设备并自动连接信号最强者，成功后记忆目标并持续自动重连。无需在 App 展示通用蓝牙设备列表。
- 主机命令表已确认没有 `band_scan/band_connect/band_reconnect` 一类入口，`get_device_status` 也只返回 `bracelet_connected` 与写入诊断，没有扫描阶段、目标、RSSI、重试时间或错误码；主机侧必须补协议和可观测状态，单改 App 无法完成手环主动搜索功能。
- `client_hello` 当前协商 schema 2.1、event ACK/去重、手机心跳、外部供电、功耗档与可选云台预检；App 建连后必须先 hello 再订阅/消费关键推送。`event_ack` 已正确要求 `event_id + session_id + stage`，旧 `reminder_ack` 仍是无事件标识的全局冷却兼容入口，新 App 不应继续调用。
- `phone_sync` 在 PRECHECK 明确拒绝并返回 `formal_session_not_started`，符合预检不进入融合的边界；正式会话中 App 需按不超过 5 秒 freshness 持续上报原始类别和单调 sequence，不能上传自己的分神等级。
- PRECHECK 预览已有 `get_camera_preview_image` 返回 base64 图像与质量字段；当前建议间隔 1200 ms。App 屏保开关和自动黑屏时间属于 App 本地交互偏好，不应混入主机 `update_preferences`，而通知渠道/音量/会话默认时长仍属于主机同步偏好。
- 已要求 App 子 agent 除功能审查外产出长期复用的架构概览、代码索引、主机协议映射和最小阅读顺序，避免后续 agent 重复加载整个 App 仓库。
- 示例 `GimbalFaceTracking` 会独占打开第二个摄像头；产品代码不能照搬这一点。正式跟踪必须复用主程序唯一 CameraSource 的帧或视觉目标，避免相机争用。
- 异构展示建议形成三级职责：A35/NPU 负责感知、会话编排与通信；M33 只负责独立风扇安全闭环；G431 负责云台电机确定性控制。三者通过硬件所有权和故障边界互相隔离。
- 板端只读检查确认 `/dev/ttySTM1` 存在且当前无人占用，对应设备树节点 `serial@40330000`、驱动 `stm32-usart`；内核日志确认其注册为 `ttySTM1`。现有程序默认 UART 路径与板端实际节点一致。
- 板端 `gpiochip3` 的 PD4、PD7 当前都显示为未被消费者申请的输入；这说明可以后续由专用硬件抽象层接管，但不能据此推断有效电平或 PD4 的复位电气语义。
- 当前 `focus_app.service` 和运行中进程均明确设置 `EDGEFOCUS_DISABLE_GIMBAL=1`，所以本轮硬件接入前云台链路不会被旧 `GimbalMonitor` 意外启动。
- 当前设备树启用了 `serial@40330000`，内核启动日志无 UART 初始化错误。正式调试仍需先验证 TX/RX 收发和 G431 启动消息，不能只以设备节点存在判定链路成功。
- 现有轮询首先要求 `VER` 返回含 `PROTO=1.*`，随后发送 `PING/STOP/STAT/FAULT/ANGLE`；而新硬件资料突出的是使能上升沿产生的异步 `BOOT_ALIGN,START/OK`。因此直接解除禁用可能把合法异步事件误当作某条同步命令的响应。

## 2026-07-15 实施后事实

- 供电事实与策略必须分层：I2C 采样只能发布 present/absent/unknown/error；手动 Home/Go 覆盖属于策略层，并在下一次确认的物理供电转换时失效。
- Home/Go 不应进入持久化 preferences；否则重启或换供电阶段会恢复过期意图。新版协议使用独立 `set_power_profile`，旧 App 同步升级。
- Windows MSVC 的中文 `/showIncludes` 在本机 Ninja 链中可能无法稳定维护头依赖；修改共享结构体后必须 clean rebuild，不能只相信旧测试二进制。
- 当前云台生产安全依赖三重条件：显式 `EDGEFOCUS_GIMBAL_FEATURE=1`、未设置 disable、且配置 `motion_verified=true` 与全部实测范围完整。任一条件不满足都不创建硬件服务或对 App 宣告能力。
- PRECHECK 的自动搜索和人工控制共享同一个类型化控制器与软件限位；人工命令额外需要当前 session、严格 sequence 和 manual lease，不能绕过状态机直接写 UART。
- 现阶段 Home/Go 只有静态策略/主机时序测试，尚不能声称趴下准确率或板端吞吐等价；需要同一素材的 Home/Go 板端 JSONL 轨迹比较。

## 2026-07-15 PRECHECK 边界补充

- 当前 Fusion `update()` 已只在 FOCUSING 调用，但 `LegacySessionProjectionFeature::prepare()` 会在 PRECHECK 前创建 SQLite row 和 `session.active=true`，PhoneFact/Fusion session 也会提前 begin；所以“没有更新 Fusion”仍不足以满足“预检不属于会话”。
- 当前相机以旧 `SystemStateStore.session.active` 为唯一采集门。若延迟旧 session 投影，必须先让相机改读 typed runtime phase，否则 PRECHECK 会失去画面。
- `SessionSnapshotPublisher` 已从进入 FOCUSING 才累计 elapsed，但其对外 `runtime_active` 曾被映射为 `session.active`；新协议必须区分 runtime/precheck active 与 formal session active。
- PRECHECK 取消时 V2 end 路径可能读取 `SystemStateStore.last_report`；正式 row 延迟后必须防止把上一场报告误当本次结束推送。
- 云台正常产品应取消人工 feature opt-in，但不能取消实测机械限位。推荐保留紧急维修 disable，总是由配置完整性和当次 UART/Fault/对齐状态决定是否进入 PRECHECK 可用态。
- `SessionSnapshotPublisher` 的 elapsed 已正确从 FOCUSING 开始，但 stable JSON 把 `runtime_active` 直接输出为 `session.active`；需要新增 `formal_active`，否则 App 仍会把 PRECHECK 当会话。
- Formal start 的 required 顺序必须让纯内存 Phone/Fusion 先启动、SQLite/旧投影最后提交。这样 SQLite 成功后不会再发生可阻止启动的 required 初始化，避免半会话。
- `CameraPipeline` 在 inactive 分支等待 `SystemStateStore` 条件变量；改读 typed runtime phase 后，orchestrator begin/end 还需要显式唤醒该条件，不能只改判断条件。
- 已实现 `FormalStartContext` 与 feature 两阶段启动：模型/视觉 worker 在 PRECHECK prepare，Phone/Fusion 和 SQLite/旧投影只在 confirm 提交；required 顺序固定为内存状态先、SQLite 最后。
- `SessionSnapshot` 现区分 `runtime_active` 与 `formal_active`；PRECHECK 对外正式 active=false，取消不会写 row/报告，`phone_sync` 返回 `formal_session_not_started`。
- 相机现直接读取 typed runtime phase，并由 orchestrator runtime-change hook 唤醒；因此旧 `SystemStateStore.session` 可在 PRECHECK 保持 inactive。
- 云台入口已移除普通 `EDGEFOCUS_GIMBAL_FEATURE` opt-in。配置完整才创建能力；I/O/PD6/UART/对齐失败只降级云台，主程序继续，硬件服务每 5 秒低频探测，连续健康并重新对齐后按 phase 恢复。
- 当前 `config/gimbal-board.json` 仍为 `motion_verified=false`，且会话内人脸闭环跟踪尚未实现；本地改动不能被描述为已完成机械追踪。
- 板子修复后原始 GPIO 已确认 PD4=0、PD6=1、PD7=0；UART4 无 owner 且重启后 tx/rx 均为 0。正式 unit 仍有紧急禁用变量，因此旧主程序不会抢 UART。
- 板端原独立 bring-up 二进制已缺失；部署脚本没有 build-only 选项，不能直接调用，否则会替换主程序。交叉编译机 `hu@192.168.10.6` 的 SSH、STM32MP2 SDK 和 21G `/tmp` 空间均可用，应复用脚本中的 archive/离线依赖命令手工执行仅编译。
- G431 资料再次交叉核对：正式协议为 115200 8N1、CRLF，MP257 `UART4_TX` 经 R77 接 `G431_UART_RX`，`UART4_RX` 经 R78 接 `G431_UART_TX`，网表交叉方向正确。参考代码同样以 CRLF 发送 `ANGLE/SET XY/...`。
- `g431_board_control_signals.md` 明确 Enable 低时 G431 应进入 `BOOT_WAIT_ENABLE` 安全等待；正常启动则必须制造 PD7 低→高边沿并等待 `BOOT_ALIGN,OK`。这说明下一项有诊断价值的动作会触发电角度对齐，已经不属于无运动检查；当前不能因为禁能 probe 无回复就无条件拉高 PD7。
- 板端 live pinmux 与当前 DTB 源一致：`serial@40330000` 处于 okay，PG14/PG15 都由该设备占用 AF6；默认组中 PG14 为 push-pull TX、PG15 为 RX input。结合网表 `PG14/MP UART4_TX -> G431_UART_RX`、`G431_UART_TX -> PG15/MP UART4_RX`，Linux 端设备节点、复用和方向没有发现配置错误。
- G431 的 MCU_3.3V 网表连接完整，但 PD6 高仅能证明主控侧上拉/故障线处于非故障，不能单凭软件读值证明 G431 MCU 正在运行。下一步要么测 UART/3.3V 波形，要么在确认机械净空和限流供电后执行会触发自动对齐的 PD7 低→高测试。
- 用户已确认机械允许转动，并转述硬件意见：`VER/PING/STAT/FAULT/ANGLE` 的回复可能要求 Enable=1。现有 `enableAndAlign()` 在 PD7=0 时等待 PING，确实会形成永远无法进入 Enable 的逻辑死锁；启动诊断必须后置到 `BOOT_ALIGN,OK` 之后。
- 首次 PD7 高沿期间用户现场确认云台确实发生动作，因此 MP257→PD7、G431 供电和至少部分电机固件已工作；问题进一步收窄为 G431 未发送启动事件、G431 TX/R77/MP257 RX 物理链路，或固件 UART 输出条件与文档不一致。
- 全工作区盘点未发现 G431 的 `.ioc/.hex/.bin/.elf` 或固件源码，只有控制说明和 A35 示例；因此当前无法从本机核实板上 G431 镜像是否包含 PC10/PC11 UART 协议，需由硬件/固件侧提供镜像/源码或直接测量 PC10→R78→PG15 波形。
- 用户提供最终 G431 主控协议：115200 8N1/CRLF；上电保持 BOOT0=0、Enable=0 等待 300–500 ms，禁能态 PING 必须返回 `OK,G431`，成功后才允许 Enable 高沿；对齐超时 10 秒。此前“诊断需要 Enable”的口头猜测作废。现有解析器未把 `OK,G431` 识别为健康 PING，且服务缺少 500 ms 稳定等待、误把诊断后置，均需修正；但板上内核 rx=0 仍表明本轮实测没有任何字节进入 MP257。
- Windows 当前串口枚举确认存在 `COM3`，但 `Win32_SerialPort` 未返回名称/PNP 详情。将其作为 SSH 失败时的板端控制台备选；SSH 正常时不打开，避免双控制通道和串口占用冲突。
- COM3 实测对应板端 ttySTM0 Linux console，115200 8N1 可直接进入 root shell。一次性写入约 300 字节会触发 console input overrun 并丢字符；回退控制必须使用短命令、逐字符约 2–5 ms 节流，并逐条等待提示符。
- 2026-07-16：用户确认 App 与主机在正式会话中途断连采用方案 A：主机会话继续；手机事实超过 5 秒新鲜度后失效并降级为纯视觉判定；关键提醒保留在 outbox 中；重连后 App 重新执行能力协商，按当前 `session_id` 拉取并恢复会话快照，禁止重复启动会话。
- 2026-07-16：App 源码审查拆成两份独立交付：`app_source_audit` 只负责核实现有功能、缺口和风险；新建 `app_arch_index` 只负责长期可复用的架构概览、依赖方向、关键调用链和最小阅读索引。两者均只读，不修改 App 工作区。
- 2026-07-16：`app_source_audit` 首次报告称 `TimerStateHolder` 未声明单例，但独立架构索引与主 Agent 对源码的最小复核确认当前 `TimerStateHolder.kt:90` 明确存在 `@Singleton`；“共享会话状态非单例”结论作废，不纳入改造范围。仍成立的最高优先级缺口是：App 没有 `client_hello/event_ack`、事件 ID 去重及 `received/presented/user_ack` 生命周期；没有前台防自动锁屏和可配置纯黑屏保。
- 当前 PRECHECK 只在横屏页提供，相机 Base64 已解析但未渲染，质量分未展示；竖屏页没有 PRECHECK、提醒展示及相机证据，故两个独立页面尚未达到核心功能等价。
- 当前普通设置页的“添加设备”是手机扫描并连接 EdgeFocus 主机，并非委托主机搜索手环；两套设置页均缺少板端手环搜索/连接命令和完整状态。现有手机扫描主机流程不含 RSSI 自动优选，主动扫描还会关闭已存设备自动连接，不能复用为目标手环策略。
- `BleForegroundService` 虽已声明但没有实际启动调用；现有高优先级通知来自手机本地 UsageStats 检测，并非板端 `push_alert` 在后台/锁屏时的系统通知投递，不能视为目标提醒链已完成。
- `app_arch_index` 已完成独立架构交付：当前 App 是单 module Compose/Hilt/Room 工程；真实主链为 `Activity/Compose → ViewModel → TimerStateHolder → BleManager → BoardJsonParser/BleChunkAssembler`，BLE 会话状态不经过 Room repository；竖屏与横屏是独立 Activity/页面/ViewModel，通过单例 `TimerStateHolder` 共享板端会话状态，横屏另带统计、PRECHECK 和主机 preferences。
- 用户选择 App 收束方案 A：沿用现有 Compose/BLE 基础设施，以单例会话协调器作为唯一会话所有者，并拆出协议客户端、提醒协调器、屏幕守卫和主机手环代理；在实施前先完成 App 与主机的字段、状态和调用时机对齐。
- 已派出独立只读 `host_app_timing_audit` Agent，限定只审查双端 BLE 交互阶段、触发条件、频率/TTL、幂等/ACK、断连恢复和竞态，不做 UI 审查或源码修改。
- 用户最终确认必须分模式定义权威边界：本地模式由 App 自行计时、管理运行并生成会话结果，主机只作为结果的权威持久化存储，App 负责可靠缓存和待提交；主机模式由主机管理运行状态与会话事实，App 负责控制交互和展示。两种模式的最终记录统一归档在主机，App 不建立第二个权威会话库，以避免数据分散、ID 冲突、重复上传和覆盖。
- 因此 `local countdown` 是本地模式正式运行链，`upload_local_session` 是本地模式正式提交链，均不能删除；需要补齐稳定 `session_id`、幂等 upsert、App 缓存状态、断连补交和主机确认。主机模式应以主机报告/记录为准，不能再由两个页面重复生成或重复保存同一结果。
- 本地模式是无法使用主机运行能力时的正式补充：未连接主机也必须允许开始、计时和结束；会话结果先持久缓存于 App，后续连接主机时自动上传。连接主机时用户仍可任选本地模式或主机模式，而不是强制切换主机模式。
- 现状审查表明原代码确有本地/主机双分支与 `upload_local_session`，所以产品方向并非新增；缺口仅在可靠性闭环：App 当前结束时若未连接就没有 durable pending-sync/outbox、自动补传、主机确认后清除缓存和可见同步状态。
- 最小源码复核确认“会话开始后模式冻结”已经存在：`TimerStateHolder` 仅在 `TimerState.IDLE` 时依据设置/连接更新 `sessionMode`（约 118–159 行），活动本地会话中的 BLE 重连不会迁移为 `EDGEFOCUS`；主机推送处理也会在当前模式不是 `EDGEFOCUS` 时直接忽略。该行为应保留并补测试，而非重写。
- 用户指出并经源码确认，普通设置页已有“使用 EdgeFocus 设备”开关（`SettingsScreen.kt:115–120` / `Settings.useEdgeFocusBoard`），现有首页启动策略据此选择本地或主机模式；不再新增每次开始前的模式选择器。开关选择主机模式但主机未连接时，首页 `canStart` 会禁用开始按钮（`HomeScreen.kt:78–81`），顶部显示“设备未连接”（123 行），按钮下显示“连接设备后可使用 EdgeFocus 模式”（286–293 行）；ViewModel 的直接返回只是第二层防御。现状符合原设计，不增加自动降级弹窗。
- 主 Agent 全量复核范围盘点：App 受控构建/主源码/资源/测试共 103 个文件、约 10,933 行、466 KB；规模适合分层完整阅读。工作树已有用户改动，涉及 `TimerStateHolder`、`BoardJsonParser`、两个页面及两个 ViewModel、图标资源和 parser 测试；本轮严格只读并以当前工作树为准，不能覆盖或假设这些改动属于本轮。
- 基础层完整阅读确认原“主机权威存储 + App 缓存”意图已经进入 schema：`Session` 包含 `source`、`synced` 和 `boardSessionId`，`SessionDao/Repository` 已有 `getUnsyncedSessions()` 与 `markSynced()`，迁移 5→6/6→7 分别添加 source/synced。缺的是调用这些 API 的自动同步协调器，而不是重新设计数据库概念。
- `Settings` 当前仅有默认时长、是否使用主机、主机 MAC；屏保开关/超时尚无 schema。两个 Activity 均未设置 keep-screen-on；Manifest 只有权限声明。`MainActivity` 启动时会强制引导 Usage Access，且 BLE/通知权限在 App 启动一次性申请。
- `AppEventLog` 是单例内存 `StateFlow<List>`，每次 `it + event` 无上限增长、无持久化；适合开发诊断，不适合作为可靠事件 outbox。
- 小型策略文件显示旧手机语义仍混杂：`PhoneUsage` 同时携带 category/usageState/distractionLevel，`PhoneUsagePolicy` 仍依赖 App 决策等级；本地模式手机分神计数仅在 LOCAL+RUNNING 增加。主机模式的新协议应忽略/删除 App 侧等级决策，但本地模式仍需保留本地提醒策略，两者不能一刀切。
- 扫描策略明确区分正常自动连接和用户主动扫描：主动扫描会关闭 stored-target auto-connect，设备列表仅按名称过滤、无 RSSI；这属于“手机找主机”链，与“主机找手环”不是同一功能。
- `AppCategorizer` 用 UsageEvents + 24h fallback 找前台应用，锁屏/非交互时返回 null；内建类别为 entertainment/social/learning/other。其缓存与 30 分钟 fallback 抑制逻辑需要结合 `AppMonitorService` 再判断是否会把黑屏误报成手机使用。
- `BleManager` 是单例原生 GATT transport + 协议 facade 的混合体：负责扫描、连接、MTU/CCCD、请求 ID、全局命令互斥、5 秒响应超时、入站分包、simulation 和全部业务命令。所有命令由一个 `Mutex` 串行，长响应/相机预览会阻塞状态、ACK 或心跳命令，后续可靠协议需至少让事件 ACK/phone heartbeat 不被低优先级大响应长期排队。
- App 连接态仅在 CCCD 写成功后变为 CONNECTED，断连会失败所有 pending response 并清 chunk；但没有自动重连调度，也没有 `SUBSCRIBED → client_hello → current_state → READY` 恢复事务。`BleForegroundService` 是否补足需后续全文核对。
- Wire 命令接口确实缺少 `client_hello`、`event_ack`、手环状态/重连命令；`reminderAck(alertId)` 完全忽略参数。`PushAlert` 未解析 event/session/TTL；`buildPhoneSync` 未带 session_id、sequence、timestamp，仍带由 App 决策的 distraction_level；`buildSessionControl` 也没有 operation/client request ID。
- `BoardSessionState` 未解析主机 runtime sequence/boot_id，三类快照无法防止晚到覆盖。`OutputCapability.bandAvailable` 只是能力布尔值，不是手环连接状态。
- Parser 仍有明显旧硬件占位默认：battery 缺失/非法时硬填 67；`OutputCapability` 与 `BoardPreferences` 默认 `screen_output_route/sound_output_route="board"`；模拟 preferences 也写 board route。当前产品已删除板载屏幕/声音，默认应为 phone/unknown 且电量已删除，不能让缺字段被伪装成 67% 或板端输出。
- Camera preview 已完整解析 base64/尺寸/quality，但 transport 只有显式请求；没有可见期调度。PRECHECK guidance 的 camera.guidance 若为对象会被存成 JSON 字符串，页面需要结构化或友好格式化，不能直接展示原 JSON。
- 入站 chunk assembler 支持 response/push、乱序、重复覆盖和 15 秒过期，但无 chunk 数量/总字节上限；可信板端环境风险较低，仍应避免异常包造成内存增长。出站完全不分包并受 MTU-3 硬限制。
- `TimerStateHolder` 前 820 行确认其虽然是单例，但职责远超“状态持有”：同时拥有两种模式状态机、BLE 连接恢复、push consumer、板端命令、倒计时插值、完成 fallback、相机/云台、提醒清除和本地记录上传。架构 A 应沿用其单一会话所有者地位，但把协议恢复、提醒呈现与持久补传拆成明确协作者，而不是再新增第二套页面状态机。
- 本地模式基础语义符合用户描述：`startLocalSession` 固定 LOCAL 并独立倒计时；连接变化只在 IDLE 重新选择模式。`syncSessionToBoard` 目前在未连接时直接 return，连接时也吞掉成功/失败结果，无法驱动 Room `markSynced` 或重试。
- 主机模式断连行为与目标冲突且分布在多个入口：pause/resume 在未连接时直接执行本地状态变化；stop 未连接时 `completeSession()`；结束报告超时/命令失败也本地完成；倒计时到零后的 fallback 在断连、状态请求失败或主机 idle 且无报告时均本地完成。这些不是“显示插值”，会让 App 生成主机模式的权威结束结论。
- `startEdgeFocusSession` 在 App 先写临时 session ID，随后若主机返回 ID再覆盖；当响应缺时间时会直接把主机模式当 RUNNING 并使用本地目标时间。新恢复事务必须把主机 session ID/phase 作为权威，App 临时值只作为 operation/client request ID。
- 横屏 `enterControlPanelMode()` 在 IDLE/FINISHED 强制把模式改为 EDGEFOCUS，离开时再按普通设置恢复。这与“连接主机后仍可选择本地或主机模式”及两页面核心功能等价是否一致，需要结合页面全文确认；暂不下产品结论。
- LOCAL start 只重置计数和会话字段，没有清除上次主机模式的 precheck/camera/gimbal/alert/error/board flags 等字段；虽然部分页面按 mode 隐藏，仍可能产生跨模式脏状态。两种模式应有明确的 session-state 初始化边界。
- 提醒当前只有单个 `lastAlert` 覆盖槽；任何新 alert 会覆盖未确认 alert，`clearAlert` 先本地删除再发无 ID legacy ACK，无法支撑 outbox 重放、优先级和三级确认。
- `TimerStateHolder` 后半段仍维护两套部分重叠的状态解析路径：`applyBoardSessionState`、`checkBoardSession` 的 legacy active 分支、`calibrateFromBoard`；字段 fallback 会保留旧值，且无 revision 门控，易造成晚到状态和跨会话字段残留。应收束为一个 reducer/reconcile 入口。
- 主机模式本地倒计时逻辑明确是 UI 插值，但代码在 `!bleManager.isConnected()` 时到零直接 `completeSession()`，再次确认显示插值越权。PAUSED 虽继续 2 秒轮询，连接 collector 重连时只为 RUNNING 重启同步，解释了 PAUSED/PRECHECK 恢复缺口。
- `BleForegroundService` 目前是未接入的半成品：无生产启动方，`ACTION_CONNECT` 只 stopScan 并不连接；service 销毁会主动断开全局单例 BLE，可能破坏会话；不能直接作为新后台保活基础，应重设生命周期或移除。
- `AppMonitorService` 同时承担本地模式提醒、主机模式 phone fact、AppUsage 持久化和 Android 通知。它每秒检测，但 `lastBleReportedKey` 使相同状态只上报一次，确定与主机 5 秒 freshness 冲突；结果只检查 transport success，不解析 `accepted`。
- 黑屏/锁屏与手机事实存在严重语义冲突：`AppCategorizer` 在设备非 interactive 或锁屏时返回 null，`AppMonitorService` 却把 null 映射为 `desktop_or_background`、`isInUse=true`、level 3，并每 10 秒提示返回 App。若实现纯黑屏保而保持 Activity 前台，self package 又被映射为 learning/isInUse=true。新 ScreenGuard 与 phone fact 采集必须明确“屏保黑屏/系统锁屏/桌面/其他 App”四种不同事实，不能复用当前 null=强干扰规则。
- 主机模式当前仍运行 App 自己基于应用等级的本地系统通知，因此可能与主机融合后的 `push_alert` 双重提醒；目标应是 LOCAL 由 App 本地策略提醒，EDGEFOCUS 只上报原始手机事实并呈现主机融合提醒。
- 通知代码注释声称使用递增 ID保证重复振动，但 `notificationCounter` 实际未使用；多数通知仍复用固定 ID，Android 可能只更新旧通知。主机 push_alert 当前也完全不进入 NotificationManager。
- `AppMonitorService` 由页面传入 session ID并以 START_REDELIVER_INTENT 重启，但没有验证恢复的 session 是否仍有效；两个 ViewModel 可分别启动/停止。其生命周期应由共享会话协调器按模式/phase统一管理。
- `host_app_timing_audit` 已完成双端时序核对。主机端 `upload_local_session` 已按 `session_id` 幂等保存，但 App 仅在本地会话结束当刻且 BLE 已连接时上传，没有本地待同步 outbox；本地模式结束瞬间断连会导致记录无法进入主机统一会话库。
- 正式主机模式的 P0 时序错位：App 未实现 `client_hello/event_ack`，可靠 `push_alert/push_session_end` 永久落入 legacy；`phone_sync` 只在类别变化时发送而主机事实 5 秒过期；PRECHECK/PAUSED 重连不完整；主机模式断连后 App 某些路径会本地 `completeSession()`；自动到时仍绕过 `SessionOrchestrator` 并只发非可靠 `push_timer_complete`。
- 其他关键错位：App 未携带主机 `session_id + sequence` 做手机心跳，且只检查 BLE 成功而不检查业务 `accepted`；主机 `push_state`、控制响应与 2 秒轮询没有统一 `boot_id/state_revision` 防乱序；PAUSED 仍可能上报/接受手机事实；相机预览仅有手动函数而无 PRECHECK 可见期定时拉取；手环尚无 App 代理命令和最强 RSSI 选择。
# 2026-07-16 架构图谱指标口径修正

- 用户确认 STM32MP257 是赛道指定型号之一，因此 TOPS 不构成项目创新点，不进入 A/B/C 主图或海报大字指标。
- “低于 2 TOPS”只是早期选型时的粗略印象，不得作为芯片规格。
- 母文档硬件技术附注可保留 `1.2–1.35 TOPS`，但不得写成实测吞吐；海报改用 `4 个计算域`、`58-D × 16 steps`、`100 ms` 等项目自身指标。
# 2026-07-16 云台恢复新事实

- 硬件侧反馈此前板端无法与 G431 通信的实际原因可能是 STM32MP257 DTB 未启用 UART4；硬件启用后已能从板端驱动云台。
- 新资料目录：`C:/Users/Aze/Desktop/self_developed/_workspace/gimbal_example/STM32_Project`，包含完整 STM32G431 工程、`mp257-current.dtb` 与 `mp257-uart4-pb6-pb7.dtb`。
- 新产品约束：GPIO 控制信号模式有问题时，允许切换到 UART-only；需先从 G431 工程确认 UART-only 能完成哪些 Enable/Fault/对齐/停机语义，不能凭名称假设安全等价。
- `focus_app/AGENTS.md` 仍保留旧的 `motion_verified=false` 和“云台条件创建”说明，完成新验收后必须同步为最终事实。
- G431 固件当前默认 `GIMBAL_UART_ONLY_MODE=1`，协议返回 `VER,PROTO=2.0,...CAPS=UART_ONLY|ARM|YP_CONTINUOUS|FOC_HOLD|TRACK|CIRCLE`。纯串口标准流程为 `PING → ARM → BOOT_ALIGN,OK + OK,ARM → HOLD/运动 → STOP`，单字节 `!` 为紧急软件停机。
- 纯串口模式用串口 `STAT/FAULT` 代替 PD6；硬件联锁模式才使用 PD7 Enable 和 PD6 Fault。现有主机代码把 PD4/PD6/PD7 设为 `LinuxGimbalIo` 的强制依赖，且启动依赖 PD7 上升沿，必须抽象成可配置控制模式。
- 主机默认串口仍是 `/dev/ttySTM1`，新 DTB/硬件资料明确 UART4 为 PB6/PB7 AF4、Linux `/dev/ttySTM3`。
- 现有 `moveCommand()` 发送带两位小数的 `SET YP`，而固件协议 2.0 的 `SET YP` 使用整数度；持续视觉跟踪应使用 0.1° 单位的 `TRACK YP`。
- 主机 `ANGLE` 解析读取 `YAW/Y` 与 `PITCH/P`，固件返回 `P_DEG/Y_DEG`，需补齐字段映射。
- 当前 `GimbalSessionFeature` 在 FOCUSING/PAUSED 仅 `HOLD`，尚无视觉跟踪控制器；`VisualSnapshot` 已提供归一化 `subject_box`、17 关键点、置信度和证据时间戳，可作为跟踪输入。
- 板端已确认 `/dev/ttySTM3` 存在，`serial3` 指向 UART4，设备树节点 `serial@40100000/status=okay`，当前无进程占用。`/boot/stm32mp257d-atk-ddr-2GB.dtb` SHA-256 为 `f03dc969...7752a`，与硬件资料中的 `mp257-uart4-pb6-pb7.dtb` 完全一致；默认启动项使用该 DTB。
- 正式 `focus_app.service` 当前仍设置 `EDGEFOCUS_DISABLE_GIMBAL=1`，板端配置也仍为 `motion_verified=false`，所以现有服务不会抢占 `/dev/ttySTM3`。
- 2026-07-16 无运动探测通过：`PING→OK,G431`；`VER` 明确协议 2.0 和 `UART_ONLY|ARM|YP_CONTINUOUS|FOC_HOLD|TRACK|CIRCLE`；`STAT` 为 `BOOST=0,MOTOR=0,HOLD=0,FAULT=0`；`ANGLE` 返回 `P_DEG=43,Y_DEG=10`；`FAULT ACTIVE=0`；最后 `STOP→OK,STOP`。该门未发送 `ARM` 或运动目标。
- `FAULT` 报告 `UARTERR=2,RXDROP=0,TXDROP=0`，需要在 ARM/运动验收前后比较增量，不能把历史非零计数直接判为当前故障。
- 用户选择产品方案 A：默认使用 G431 协议 2.0 的 UART-only 控制；GPIO+UART 只作为显式配置的增强模式，增强模式失败时不静默降级为 UART-only，避免暗中削弱安全联锁。
- 用户确认测试区域安全后，已通过 COM3/Linux `/dev/ttySTM3` 完成真实运动验证：`ARM` 收到 `BOOT_ALIGN,START`、`BOOT_ALIGN,OK`、`OK,ARM`；`HOLD 1`、`PITCH 10`、`YAW 10`、`TRACK YP 120 80` 均被 G431 接受并产生实际角度变化；`STOP` 后 `BOOST=0,MOTOR=0,HOLD=0,FAULT=0`。
- 实机确认当前 G431 为 `PROTO=2.0`，能力为 `UART_ONLY|ARM|YP_CONTINUOUS|FOC_HOLD|TRACK|CIRCLE`。这要求 Linux 驱动改用 `ARM` 启动、解析 `P_DEG/Y_DEG`，并分别识别 `SET/DEG/TRACK` 应答，不能继续把所有运动应答都假定为 `OK,...`。
- 运动前后 `UARTERR` 从 3 增至 6；停止后两次相隔 3 秒的 `FAULT` 查询又从 6 增至 7，但 `RXDROP=0/TXDROP=0` 且命令应答完整。产品健康判定应观察误码增量、连续应答超时和真实 Fault，而不能把历史 `UARTERR>0` 直接判为不可用。
- `STOP` 后 P 轴从约 9°回落到约 311°，与电机卸力后的重力运动相符；停止状态不得把“角度仍变化”误判为控制失效，但必须以 `BOOST/MOTOR/HOLD=0` 作为安全停机验收条件。
- 本地已完成协议 2.0 驱动改造：默认 `/dev/ttySTM3`/UART-only，显式 GPIO 联锁模式不自动回退；ARM 同时等待 `BOOT_ALIGN,OK` 与 `OK,ARM`，SET/TRACK/HOLD/STOP 使用各自精确应答，UART-only 的 STOP 后继续查询 STAT 并要求 `BOOST=MOTOR=HOLD=FAULT=0`。
- 运动边界已从错误的线性绝对角范围改为 ARM 后参考角的环形最短弧相对窗口。生产配置保持 `motion_verified=false`；另建只供独立验收的 yaw ±15°、pitch ±10° 小窗口配置。
- PRECHECK 已使用参考位相对 waypoint；FOCUSING 视觉跟踪复用现有 `VisualSnapshot`，优先鼻/眼关键点、回退主体框上部中心，包含新帧门、600 ms 新鲜度、死区、EWMA、100 ms 最短周期和 2°单步限幅；PAUSED 仅 HOLD，持续丢人后不扫场。
- 本机完整主程序链接通过；云台专项 6/6、全仓 30/30 测试通过。首次全仓 CTest 唯一失败是目标尚未构建导致 Not Run，补编目标后重跑全套全部通过，并非业务回归。
- 向 `hu@192.168.10.6` 上传 25.4 MB 源码归档的动作被安全审查拒绝，原因是私有源码传到另一主机需要用户对该传输作明确批准。没有绕过审查，也没有部署或替换板端主程序。

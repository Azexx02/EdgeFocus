# Focus App 当前实施计划

## 2026-07-22 云台全周人工控制与受限自动控制

目标：G431 两轴提供 0～359.9°单圈位置控制；主机 AUTO 限制在原点 ±70°且范围外只准小步向内；MANUAL 全周并在从内部向外跨 ±70°时完成一次物理稳定停靠；网页与 APP 通过每条命令的 `fine_tune` 布尔属性选择主机配置的普通/微调步进。

规格：`docs/superpowers/specs/2026-07-22-gimbal-full-range-manual-bounded-auto-design.md`，三轮审查 Approved，用户批准，最终规格提交 `b247b43` 之后包含修订提交 `b50a6cd` 与状态提交。

1. [complete] 盘点 G431、主机自动/人工链路与旧 ±70°约束，确认两轴全周机械能力
2. [complete] 与用户确认 AUTO 范围外向内、MANUAL 单次停靠、边界原位直接运动和普通/微调按钮语义
3. [complete] 编写规格并完成三轮独立审查，闭合环形角度、并发事务、NVM 兼容和板端验收安全
4. [complete] 清理上一轮未提交的临时维护恢复带，形成详细实现切片与测试先行顺序
5. [complete] G431 Production 全周 TRACK/HOLD、Maintenance ±10°隔离、能力标识与 NVM golden 测试
6. [complete] 主机纯 `GimbalMotionPolicy`、普通/微调配置、AUTO 双轴裁决和 MANUAL PENDING/ARMED 状态机
7. [complete] AppCommandService/网页接入 `fine_tune`，保持旧客户端缺省普通步进兼容
8. [complete] 完成本机 G431/Host 构建、专项测试、全量测试及静态检查
9. [in_progress] 验收配置已获用户批准，ARM 主机发布已原子部署并通过 HTTP/BLE 健康检查；等待 G431 新固件刷入后执行分阶段实机运动验收
10. [complete] 输出 `docs/integration/gimbal-manual-boundary-app-fields.md`，完成源代码、测试、固件产物与本地交付证据

### 固定约束

- G431 不区分 AUTO/MANUAL，不新增 TRACK 协议；Production 忽略旧 NVM ±70°窗口，Maintenance 仍 ±10°。
- APP/网页不发送角度；`fine_tune` 仅属于当前命令，缺失/false=普通，true=微调，可任意交替。
- 普通/微调初始主机配置 30°/10°，配置必须大于 0 且小于 180°。
- `BOUNDARY_PENDING` 必须等实测稳定后才能转 `BOUNDARY_ARMED`；快速第二次点击不能提前越界。
- 单串行执行器事务化双轴快照、策略、TRACK ACK、权威目标和边界状态；UART 结果不确定必须 STOP/重同步。
- AUTO 精确 180°差值不得直接下发，先生成朝 0°且小于 180°的中间目标。
- 板端运动前必须存在无占位的 `config/gimbal-full-range-acceptance-v1.json` 并获得批准。

### 本轮错误记录

| 错误 | 处理 |
|---|---|
| 多文件 `apply_patch` 在本环境偶发挂起 | 中止挂起单元并拆成单文件小补丁；未使用覆盖式写入，最终执行 `git diff --check` |
| 2026-07-21 外部执行额度耗尽，上一轮构建/SSH 中断 | 不把临时恢复带标为完成；新轮按最终规格重做并重新测试 |
| PowerShell 使用 `$host` 触发内置只读变量冲突 | 改用 `$repo` 后重新检索，未影响源码 |
| 首轮 MSVC 构建未初始化标准库环境 | 通过 Visual Studio `VsDevCmd.bat` 运行真实构建 |
| G431 host test 旧进程占用 EXE | 终止仅该测试进程后重建，测试通过 |

## 2026-07-16 云台 UART4 恢复、双后端与正式接入

1. [complete] 只读核对硬件提供的 G431 工程、附带 DTB、板端 UART4 设备节点/设备树、现有 Linux 云台实现和未提交改动
2. [complete] 与用户确认 GPIO+UART / UART-only 的产品回退语义和首轮运动验收边界；确定默认 UART-only、GPIO 仅显式增强，并完成安全环境下 ARM/HOLD/角度/TRACK/STOP 实机运动验证
3. [complete] 比较双后端实现方案，形成已批准 A 方案的增量设计规格与实施计划，并完成本地自审
4. [in_progress] 独立 bring-up、协议 2.0、UART-only/GPIO 显式双模式、相对安全窗口、PRECHECK 和视觉跟踪代码及本机测试已完成；等待明确批准向 192.168.10.6 上传源码后做 ARM 构建和板端受限运动测试
5. [pending] 板端独立验收通过后，确认轴方向/安全窗口，更新正式配置并验证主程序 PRECHECK 与会话跟踪接入
6. [pending] 完成 host 测试、ARM 全目标构建、板端独立验收、正式服务灰度部署与故障恢复验证
7. [pending] 更新 AGENTS、当前架构、硬件实测和部署文档，仅提交本任务文件

## 2026-07-16 展示海报架构素材包

1. [complete] 核对主程序、视觉融合、App、手环、云台、供电与 A35/M33 异构架构事实
2. [complete] 与用户确认展示主线；用户批准 A/B/C 三种信息密度全部交付
3. [complete] 编写完整展示设计规格，定义 C0 母图、12 张专题图、A/B 下钻映射、图稿契约、量化口径、文案层级与术语；三轮独立审查已通过
4. [complete] 用户已批准书面规格；生成完整架构母文档、A/B/C 可编辑 Mermaid 图、海报短文案与浏览器预览
5. [complete] 对照源码与实测记录复核正式图稿边界；A=4、B=9、C=C0+12，围栏/图类型/链接/术语/浏览器预览/`git diff --check` 全部通过

## 目标

将已验证的 Presence / Prone / Phone Fusion 核心合入主程序，收束为正式会话运行域，并提供互不污染的板端演示运行域与可靠蓝牙交互。

## 2026-07-15 架构基线与 M33 风扇调速

1. [complete] 盘点当前源码、既有规格、部署入口与运行时边界，确认“实际架构”而非旧规划
2. [complete] 比较架构文档组织方案并确认唯一入口、图表层级和维护规则
3. [complete] 编写主程序架构基线：模块图、会话生命周期、视觉/手机/提醒数据流、演示隔离、蓝牙接口、故障降级与部署
4. [complete] 编写 agent 快速接手指南：阅读顺序、关键文件、禁止假设、常用验证命令与变更检查清单
5. [complete] 对照源码逐项复核文档并完成格式/链接检查
6. [complete] 架构文档完成后审查 `_workspace/fan_develop` 和 STM32MP257 A35/M33 资源边界，形成风扇模块接入设计

## 2026-07-15 M33 风扇控制

1. [complete] 用户批准混合方案 B：M33 独占 PWM/转速反馈，A35 bridge 上报 Linux 温度，通信异常时 M33进入安全高转速
2. [complete] 首版范围收束为风扇、M33自身看门狗和版本化 RPMsg；云台/电源板只预留扩展，不实现控制
3. [complete] 编写并自审设计规格，明确硬件未知项、故障安全、协议、测试和回滚
4. [complete] 用户复核书面规格，并批准新增 `70°C → 62°C` 自适应增压冷却策略
5. [complete] 形成详细实施计划
6. [complete] 只读盘点板端 remoteproc、CPU、PWM、温区、GPIO、设备树与 BSP 工程；确认运行事实并记录 M33 源包/裸机工具链缺口
7. [complete] 主机控制核心、Linux fan bridge、M33 便携任务和 ATK HAL/OpenAMP 板级适配均已完成；CMake/Ninja 已稳定生成 remoteproc 可加载的 non-secure ELF
8. [complete] 分阶段 bring-up 已定位并移除 non-secure UID 访问；TIM17_CH1N/PB10 PWM、完整控制循环、独立温度 bridge 与断链恢复均已上板通过，PB11/自启动留待硬件与生产化阶段

### 分阶段 Bring-up 实施

1. [complete] 调整 M33 启动顺序：OpenAMP/VIRT_UART 先于板级 PWM 初始化
2. [complete] 将板级初始化拆为资源、时钟、TIM、GPIO、CH1N、控制核心六个可报告阶段
3. [complete] 扩展部署脚本：等待 RPMsg、采集阶段标记、超时自动回滚并恢复默认 echo
4. [complete] 本机构建、静态检查并保留 PB11/bridge/autostart 禁用
5. [complete] 板端分阶段诊断完成：stage 7、stage 8 各稳定 40 秒，完整控制循环稳定 60 秒
6. [complete] 独立 Linux 温度 bridge 已部署；正常握手、6 秒断链安全态、重启后两帧恢复、52 秒稳定性与资源所有权均通过，交接文档已更新

## 状态

1. [complete] 正式会话与预录演示的 NPU、快照、手机事实和预览帧隔离
2. [complete] 正式会话资源租约与并发启动竞态保护
3. [complete] 现场摄像头只读展示、视觉证据叠加和近 30 秒四轨时间线
4. [complete] 预录 NPU 输入与显式纯前端回退框架
5. [complete] 云台生产入口硬禁用，板载屏幕/声音不进入主程序
6. [complete] App 能力协商、关键事件 ACK/outbox、手机 2 秒心跳/5 秒 TTL
7. [complete] 手环非阻塞待发队列、TTL、重试和实际送达状态
8. [complete] ARM 全目标交叉编译、原子部署、板端纯逻辑复测和 1920×1080 页面 QA
9. [complete] 完整回退素材通过板端正常 NPU 链路生成预日志，并完成预录/回退同步验收
10. [complete] 六段约 30 秒素材独立增量部署、预录资源按进入/退出启停、二次进入底图恢复和片段顺序轮换
11. [complete] 纯前端回退拆为 12 段视频与 12 份真实预日志；NPU 失败自动降级，视觉证据与近 30 秒四轨时间线同步复现
12. [complete] 预录 NPU 与纯前端演示均改为片段播放列表；删除人工复核误报趴下的 r04/r05 视频及预日志

## 当前板端

## 2026-07-15 M33 风扇校准与自启动

1. [complete] 增加并测试 CH1N 逻辑 duty 到硬件 compare 的反向映射
2. [complete] 恢复 8000 ns 安全值并启用 PB11/EXTI 测速
3. [complete] 构建、手动部署并完成 duty/原始脉冲/RPM 标定
4. [complete] 实现幂等资源切换、M33 loader、bridge、健康检查和 Linux fallback unit
5. [complete] 执行 start/fallback 故障注入和硬件默认自转验证
6. [complete] 记录用户波形豁免，启用开机链并完成两次 systemd reboot 与一次物理断电重上电验收
7. [complete] 更新架构、硬件、部署与 agent 交接文档

- 地址：`192.168.10.174`
- 服务：`focus_app.service` active
- 展示页：`http://192.168.10.174:18080/`
- 当前发布：`/opt/edgefocus/releases/20260714-233500`
- 预录素材目标：`/opt/edgefocus/assets/demo/p01_showcase.mp4`

## 本轮已处理错误

- 编译机新目录下载 SQLite 时发生 TLS 失败；部署脚本已固定复用编译机离线依赖缓存。
- 首次远端 curl JSON 的 PowerShell 转义错误；后续改用 `Invoke-RestMethod` 发送结构化 JSON。
- Playwright 验收脚本两次因采样窗口/清理上下文写法误报；最终改为单一持续浏览器会话和真实 DOM 生命周期断言。
- 2026-07-15：`rg --files` 被 Windows 沙箱拒绝执行；架构盘点改用 `git ls-files`、`Get-ChildItem` 和 `Select-String`，不再重复该失败命令。
- 2026-07-15：首次设计规格 `git diff --cached --check` 报告元数据行尾空格，但 PowerShell 未因原生命令非零码中止且已产生提交；随后删除行尾空格、重新检查并仅修正本次文档提交。
- 2026-07-15：批准实施后再次通过 SSH 只读盘点 `192.168.10.174`，34 秒内仍无任何输出并超时；不重复长 SSH 命令，下一步改用 ICMP/TCP 可达性检查，并先盘点编译机与本地 BSP。
- 2026-07-15：ICMP 与编译机盘点并行执行时被 10 秒总超时中止；只取得板端 ping 成功（约 681 ms），未取得 TCP/编译机结果。后续使用独立短命令和更宽总超时。
- 2026-07-15：并行读取 M33脚本/DT/工具链时，PowerShell 提前展开远端 shell 循环变量导致未闭合引号；改为无循环的独立固定路径命令，避免跨两层 shell 的 `$` 转义。
- 2026-07-15：本机与编译机并行搜索 MP2 source package 时编译机命令退出 1，组合调用未返回远端详情；本机常见目录确认未找到。停止扩大盲搜，先实现不依赖 CubeMP2 的核心与 bridge，并把官方/厂商源包列为 M33 HAL 阶段依赖。
- 2026-07-15：首次按工程根目录读取 `.cproject` 失败；ATK 工程的有效 CubeIDE 元数据实际位于 `STM32CubeIDE/CM33/NonSecure`，后续固定从该层导入和构建。
- 2026-07-15：对 ATK 包使用 PowerShell `-Include` 的设备树搜索产生误导性结果；复核后确认该包没有本板 Linux DTS，仅有无关 ZynqMP dtsi。板端 PB10/PB11 事实改以 `hardware_bringup` 的已验证 v17 DTB、运行记录和板上只读检查为准。
## 2026-07-15 云台 A35→G431 调试与异构架构收束

1. [complete] 只读核对 G431 协议、UART4、PD4/PD6/PD7、STUSB4500/I2C4 与现有云台/供电模式代码，确认真实硬件所有权和安全默认态
2. [complete] 与用户确认供电自动档位和手动覆盖语义、Go 降频范围、云台首轮调试范围和预检验收标准
3. [complete] 比较云台与供电检测接入方案，用户选择“进程级硬件服务 + 会话级插件”；内核驱动路线列入国赛后续计划
4. [complete] 提交云台调试、预检搜索、供电档位及异构调度完整设计，取得用户批准
5. [complete] 按用户书面复核意见移除旧 App 兼容，并重新复核设计规格
6. [complete] 形成分阶段实施计划，冻结无运动、受限运动、供电、PRECHECK、协议、灰度和回滚门槛
7. [complete] 实现纯逻辑协议/状态机、Linux 硬件服务和独立 bring-up 工具，并完成 host/ARM/板端纯逻辑验证
8. [complete] 无运动 probe 已安全执行但 UART RX=0，受限运动保持封锁；STUSB/Home-Go、主程序 PRECHECK、显式云台命令和新版 App 契约已完成主机实现
9. [complete] 完成 host 27/27、ARM 全目标编译和架构/Agent 文档；未部署主程序，云台继续保持 feature/config 双门禁
10. [in_progress] 供电/Home-Go 主程序已原子部署并完成当前“无外部供电”只读验收；待实物插拔供电并采集 Home/Go NPU 定量轨迹，UART RX 与机械参数通过前不执行云台运动

## 2026-07-15 PRECHECK 正式会话边界与云台自动健康门

1. [complete] 用户确认 PRECHECK 不属于正式会话：不计时、不入库、不进入 Phone/Fusion/episode/提醒，只允许运行视觉与云台准备
2. [complete] 用户确认正常产品移除云台人工 feature opt-in，由配置与运行时 UART/Fault/对齐健康决定是否使用；紧急运维禁用保留
3. [complete] 编写并自审边界规格，补齐相机所有权、正式启动原子性、App 字段、取消预检和上一场报告隔离
4. [complete] 增加显式 FormalStart 生命周期，拆分 PRECHECK runtime 与正式 session 计时/持久化
5. [complete] 让相机读取 typed runtime phase；PRECHECK 视觉可用但旧 session 投影保持 inactive
6. [complete] 延迟 PhoneFact/Fusion/SQLite 到 confirm，并在 confirm 清空全部 PRECHECK 连续性
7. [complete] 移除正常产品云台 opt-in，接入配置 + runtime health 自动降级和低频恢复；未修改未实测机械配置
8. [in_progress] 更新 App/架构文档并完成关键 host 测试；待 host 全回归与 ARM 全目标编译，板子维修期间不部署

## 2026-07-15 G431 修复后安全复验与上板

1. [complete] 只读确认板端可达、正式服务/风扇状态、云台服务禁用态、UART owner 与 PD4/PD6/PD7 安全电平
2. [complete] 在 PD7 禁能态复跑无运动探测；TX 正常增加但五条命令仍全部 RX=0，故未通过 G431 固件恢复门槛
3. [complete] ARM clean 全目标编译通过，独立 bring-up 工具经编译机/本机/板端三端哈希校验后恢复；未部署主程序
4. [in_progress] G431 重刷后用独立工具/COM3 复验仍为五项诊断全超时、tx=31/rx=0；主程序保持不动，待确认刷入镜像的 PC10/PC11 UART 配置或修复 G431_TX→R78→PG15 后再做对齐/STOP/小角度运动
5. [pending] 仅用实测值更新 `config/gimbal-board.json`，再移除正式 unit 的紧急禁用变量并部署主程序
6. [pending] 验收 PRECHECK 找人/手动租约、正式会话边界、Fault 中途降级与低频恢复；当前人脸闭环追踪仍单列后续实现

## 2026-07-16 G431 单向 STOP 诊断

1. [complete] 用户选择最安全的方案 A：自动对齐后仅发送 `HOLD 1` 与 `STOP`，由现场观察是否卸力
2. [complete] 形成独立工具三重确认、Fault 监测、禁止角度命令和必定禁能的书面规格
3. [complete] 用户复核并批准书面规格
4. [complete] 实现并完成 host/ARM 独立测试，不修改或部署主程序
5. [complete] 板端完成 STOP 与 PITCH 5 单向诊断；现场确认无卸力、无电机声、无角度动作，转交硬件排查

## 2026-07-16 App 唯一交互渠道收束

1. [completed] 限定在主机仓库内盘点 BLE 命令、事件、ACK/outbox、预检状态、提醒与手环管理契约，并以两个只读子 Agent 分别核验 App 功能现状和建立架构索引
2. [in_progress] 与用户逐项确认 App 的提醒呈现、预检控制权和手环重连产品语义；并由独立只读 Agent 对齐主机/App 在连接、PRECHECK、正式会话、提醒、断连恢复与结束阶段的交互时机
3. [pending] 按用户选定的方案 A 收束共享会话协调器、协议客户端、提醒协调器、屏幕守卫与手环代理设计，并提交完整设计供用户批准
4. [pending] 编写 App 修改与功能审查规格，明确主机/App 双方责任、时序、错误恢复和验收用例
5. [pending] 书面规格复核通过后形成 App 工作区实施交接清单；本阶段不修改 App 源码

### 主 Agent 全量 App 源码复核

1. [in_progress] 盘点 App 受控文件、工作树状态、代码规模并按构建/数据/BLE/会话/服务/UI/测试分组
2. [pending] 主 Agent 完整阅读 Gradle、Manifest、DI、数据模型、Room 与 repository
3. [pending] 主 Agent 完整阅读 BLE transport、wire 协议、会话协调器和后台服务
4. [pending] 主 Agent 完整阅读 ViewModel、导航、全部 Compose 页面及策略文件
5. [pending] 阅读测试并核对原产品语义、现有保护和真实缺口
6. [pending] 输出可供后续 Agent 复用的现有设计事实表、代码索引、有效缺口和禁止误判项；之后才继续设计问答

# Focus App 审查进度

## 2026-07-15 风扇纯控制核心与协议

- 新增独立 `edgefocus_fan_core`，不依赖 Linux、STM32 HAL、会话、视觉或主程序状态。
- 控制核心实现五档曲线、2°C 降档迟滞、3 秒温度 TTL、两帧恢复、异常安全高转、tach/RPM 和三窗堵转确认。
- 自适应增压实现 70°C 连续三帧进入、5 秒/2,000ns 爬升、10 秒降温趋势保持、两窗再升温恢复、80°C 立即最高转速、62°C 稳定十秒后限速退出。
- 新增固定小端二进制协议和 CRC32，覆盖 HELLO/TEMP_SAMPLE/STATUS/FAULT/SHUTDOWN_NOTICE；无动态分配或 JSON 依赖。
- 本机无 C++ 编译器，已在 `192.168.10.6` 用 g++ 13.3、C++17、`-Wall -Wextra -Wpedantic -Werror` 编译；`test_fan_control` 与 `test_fan_protocol` 均通过。
- 下一步实现 Linux bridge；M33 HAL/OpenAMP 层继续等待匹配当前 ATK/OpenSTLinux 的 CubeMP2 源工程和 Arm bare-metal 工具链，不猜测 TIM/AF/RIF。
- Linux bridge 已完成：精确发现 `cpu-thermal`、原始 RPMsg TTY/字符流分帧、M33 capability 握手、1 Hz 温度发送、断链重连和 `/run/edgefocus/fan-status.json` 原子发布；service 模板故意没有 `[Install]`，不能自动启用。
- 新增 `FanControllerApp` 作为 M33 便携任务：先写安全 PWM 再接 OpenAMP，处理握手/温度/tach/状态帧、PWM/控制截止故障和 WWDG 健康门；所有板级寄存器与 HAL 留给匹配 BSP。
- 修复 bridge 进程重启序号域：HELLO 的 boot id 改变会让 M33 重置旧 sequence、进入 BOOT_SAFE，并等待两帧新温度恢复。
- 新增硬件事实盘点 `docs/hardware/stm32mp257-m33-fan-inventory.md`，并同步 `AGENTS.md` 与当前架构；文档明确当前未形成可烧录固件、未部署、未切换 PWM 所有权。
- 当前四组验证均通过：控制核心、二进制协议、Linux bridge 温区/流分帧、M33 便携任务；完整 bridge 也以 `-Werror` 编译通过。
- 增加 `platform/fan/CMakeLists.txt` 独立构建入口；编译机 Release 构建成功，CTest 4/4 通过。首次指定 Ninja 因编译机未安装 Ninja 而配置失败，改用可用的 Unix Makefiles 后通过，文档命令不再硬编码生成器。
- 使用现有 STM32MP2 5.0.3-snapshot A35 SDK 成功交叉编译最终版 `edgefocus-fan-bridge`；未上传板端、未启动服务、未触碰当前 echo M33 firmware 或 Linux PWM。
- 收到 ATK M33 Cube 源包后完成只读核对：OpenAMP echo/resource table、TIM/FDCAN 示例、CubeIDE 工程和 Arm 12.3 工具链均齐全；本机还已有已验证 v17 风扇 DTB 与硬件 bring-up 记录。现在可以开始板级固件派生，无需继续下载软件包。
- 用户确认 Linux `fan-temp-control.service` 是设备树裁剪后建立的临时 PWM/tach 服务。当前保持运行；只有在手动加载 M33 固件前才停止，避免 Linux/M33 双写 PB10/PB11。

## 2026-07-14 板端会话/演示/蓝牙收束完成

- 正式会话与演示运行域已经隔离；正式会话通过资源租约独占 NPU，避免 App 启动与页面切换同时发生时双链抢占。
- 展示页恢复主体框、关键点、证据年龄和 PRES/PRONE/PHONE/LEVEL 近 30 秒时间线；预览帧独立以约 10 FPS 拉取，状态轮询不再限制预览刷新。
- 现场模式保持只读，手机类别只来自 App；预录 NPU 模式允许页面输入手机原始类别；纯前端回退锁定手机按钮并明确标记 NPU BYPASSED。
- App 新增 `client_hello`、`event_ack`、关键事件 outbox、按 event_id 去重重试；PhoneFact 固定 5 秒 TTL，相同重复 sequence 不刷新有效期。
- 手环写入改为 1 in-flight + 1 pending 非阻塞队列，含高等级覆盖、10 秒 TTL、重连补发、500/1500 ms 重试和 1 秒单写超时。
- ARM Release 全部目标交叉编译成功；板端运行通过 `test_session_runtime`、`test_distraction_adapter`、`test_app_event_outbox`、`test_demo_http`。
- 发布 `20260714-202021` 已部署到 `192.168.10.174`，服务 active，HTTP 健康检查通过；1920×1080 Playwright 验证无浏览器错误或警告。
- 当前未放入最终演示视频，故预录按钮按设计禁用。用户提供 `p01_showcase.mp4` 后再执行真实 NPU trace 采集与回退同步冻结。

## 2026-07-14 多片段预录与生命周期修复

- 接收 `p01.mp4`～`p06.mp4` 六段约 30 秒素材，逐个核对编码后以 SHA-256 校验原子同步到板端。
- `BoardDemoRuntime` 支持 `pNN.mp4` 顺序轮换；显式环境变量仍优先并保持单一指定文件。
- 修复退出预录后 `frameBusy` 未复位导致二次进入只有 SVG 关键点、没有 JPEG 底图的问题。
- 退出时浏览器同时卸载图片和视频资源；板端同步 join feed thread、停止推理链、清空预览与证据。
- 修复人工退出与 NPU 超时自动回退之间的生命周期竞态。
- 发布 `20260714-213717` 部署成功；六段媒体第二次部署全部识别为 unchanged，没有重复传输。
- API 两轮回归：两次均 `RECORDED_NPU/RUNNING/preview=true`，JPEG 分别约 68 KB、66 KB；退出均为 `LIVE_CAMERA/IDLE/preview=false`。
- Playwright 单会话两轮回归通过：每轮进入底图 `display=block` 且有实时 JPEG `src`；退出等待 5 秒后图片/视频 `src=null`、视频 `networkState=0`。
- 板端最终状态：`focus_app.service=active`、LIVE/IDLE、空闲线程 16、六段素材齐全、系统时区 `Asia/Shanghai`。

## 2026-07-14 预录演示、证据回退与通信审查

- 用户确认只有 IDLE 可切换预录模式；现场摄像头模式页面手机类别只读，预录模式允许手动选择。
- 用户确认预录模式优先真实 NPU 推理，证据日志是同视频同步回退，并需要独立开关。
- 用户要求云台当前全部禁用，并审查 App/手环交互接口完整性与通信时机。
- 已盘点 P01 素材，准备以 normal/prone/phone_use/not_present 组成单一演示视频。
- 当前处于 brainstorming 设计硬门，尚未修改产品代码。


## 2026-07-14 主程序收束与融合

- 用户确认独立 C++ 视觉/行为子程序已在板端测试通过，开始主程序架构收束、核心融合和演示增强阶段。
- 已启用 brainstorming、planning-with-files 与可视化伴侣；继续执行设计硬门，先交付架构/流程供审核，不改产品代码。
- 完成主程序初步依赖审查：旧 `VisionResult/FocusState/BehaviorEngine/SystemStateStore/DemoHttpServer` 与新核心契约存在语义和职责冲突。
- 已确认建议顺序：目标架构与功能边界 → 新核心替换与回归 → 演示页/备用输入/故障降级增强。
- 下一步：确认功能保留范围，比较渐进式替换、一次性重构和比赛极简版三种方案，生成可交互架构图。
- 用户补充功能范围：云台跟踪和手动控制保留为随会话启动的外挂插件；板载屏幕和声音删除并迁移到 App。
- 已审查现有 `GimbalController/GimbalMonitor/AppCommandService/ReminderPolicy`，确认协议和串口控制器可以复用，但生命周期、自动/手动仲裁、共享相机和 App 通知可靠性需要重新划分。
- 下一步：提交架构图 v2，单独确认云台会话状态机、App 通知边界以及手环是否继续保留为板端直连输出。
- 用户确认手环继续板端蓝牙直控，App 也通过蓝牙交互；同时明确“会话”应是大部分功能的生命周期边界。
- 已将目标架构进一步收束为 `ProcessServices + SessionOrchestrator + SessionRuntime`，并区分会话核心服务与可降级外挂插件。
- 下一步：提交会话中心架构图 v3，确认阶段状态机和各能力在 IDLE/PRECHECK/FOCUSING/PAUSED/ENDING 的启停矩阵。
- 用户批准会话中心架构和 PAUSED 低功耗保持策略；规格与实施计划已提交为 `75f175d`。
- 新增纯 C++ `SessionDomain/ISessionFeature/SessionRuntime/SessionOrchestrator/SessionSnapshotPublisher`，覆盖必选能力失败回滚、可选插件降级、幂等暂停/恢复和跨会话快照隔离。
- 将已板端验证的 `distraction_core` 原样迁入主程序内部 `edgefocus_distraction_core` 静态库；JSON 模型加载改用主程序已有 nlohmann/json，避免新增 json-c 主程序依赖。
- 新增 `PhoneFactBuffer` 和 `DistractionAssessmentEngine`，App 娱乐→其他保持 risk episode，学习/未使用按新核心关闭；跨 session、乱序、重复和 stale 手机事实被拒绝或标记。
- 编译机完整 Release 构建成功；新 session/core/adapter 测试均通过。完整 CTest 15 项中 13 项通过，新增会话测试最初因 Release 关闭 assert 而误报，已修复并复测通过；旧 `test_ble_gatt` 的 Release assert 副作用导致既有段错误，待 Debug/测试重写确认。
- 修复旧 BLE 测试中悬空 push callback 导致的堆破坏；完整 Debug CTest 15/15 通过。
- 主程序生产入口已切断旧 `VisionEngine/BehaviorEngine/FSM/RiskScorer` 注册，接入 STAI Pose、58 维时序 MLP、FusionCore、PhoneFactBuffer 与稳定 SessionSnapshot。
- 板载屏幕/声音路由固定迁移到 App；暂停态相机约 1 FPS 保持，推理只用于 presence 快速恢复；云台封装为可降级会话插件，当前板端配置保持禁用并降级为固定机位。
- 演示 HTTP 已改为工业证据链页面；旧 override 命令不再路由，页面只读 SessionSnapshot，预览来自唯一 CameraPipeline 的共享帧，并分开显示预览 FPS 与 NPU FPS/延迟。
- STM32MP2 Release 交叉构建及安装包验证通过，NB Pose 与时序 MLP 均进入安装目录；已部署到 `192.168.10.174`，旧版本保存在 `/opt/edgefocus/releases/20260714-pre-convergence`。
- 板端真实会话验证通过：PRECHECK→FOCUSING，camera=true，NPU 约 4.6 FPS / 220 ms，共享 JPEG 预览正常；无人画面输出 NOT_PRESENT 且 level=null，四种手机原始类别均被正确接收。
- 演示备用路径当前仅完成显式占位与防混淆标签；RECORDED_INPUT 真实推理输入和 EVIDENCE_REPLAY 证据包仍待实现/冻结。

- 用户批准独立子项目边界与稳定数据契约；本机版本不开发手机状态获取，改用手动状态模拟器，研发重点集中在视觉层。
- 用户批准完整设计并要求压缩排期：第 1 天合并工程骨架、回放、模型基线与采集入口，第 4 天冻结，第 5 天仅赴场/现场准备。
- 设计规格已写入 `docs/superpowers/specs/2026-07-13-distraction-core-design.md` 并以提交 `864b59c` 单独提交；已启动独立规格审查。
- 第一轮规格审查发现 UNKNOWN 契约、最低证据充分性、数值验收和四天 P0 范围四类阻塞项；已全部收紧并以 `350804b` 提交，正在复审。
- 核对主机环境：Python 3.12、OpenCV、Ultralytics、PyTorch CUDA、scikit-learn、NumPy 与 ONNX Runtime 已安装，MediaPipe 未安装；实施设计将保持 face/head-pose adapter 可替换。
- 第二轮审查发现类型化 episode 评测来源不唯一；新增 `evidence_episodes` 及开闭/重叠/空洞规则，以 `05f0414` 提交。最终规格审查结论：无阻塞项，等待用户书面复核。

## 2026-07-13

- 启动独立分神判定子项目的设计阶段；读取 brainstorming 与 planning-with-files 技能，并建立设计硬门：用户批准规格前不实施。
- 复核仓库结构、审查记录、现有 VisionResult/ModelAdapter/行为/FSM/提醒契约；确认当前接口可作为迁移反例，但不适合作为新核心的稳定 API。
- 下一步：一次只确认一个产品语义，首先定义“趴下”与正常低头的边界。
- 产品语义确认 1：趴下采用严格 A 定义；后续数据集与验收必须包含写字、阅读等困难负样本。
- 产品语义确认 2：人不在输出 NOT_PRESENT，不属于分神等级，提醒策略不得对无人状态发提醒。
- 产品语义确认 3：手机端上报原始事实，融合核心是分神等级的唯一决策者。
- 产品语义确认 4：三级等级按强度、持续时间和多源一致性动态演化；严重/长期分神更高。
- 产品语义确认 5：判定核心与提醒策略解耦；核心不直接产生或执行提醒。
- 读取并目视检查 `_workspace/picture_test` 的 3 张样本；完成初步标签映射，发现相机方向/视角域不一致且缺少时序数据。
- 产品语义确认 6：第一阶段补采短视频并建立离线评测工具，不再以静态图主观调阈值。
- 产品语义确认 7：左右转统一为 HEAD_AWAY，方向仅作诊断。
- 产品语义确认 8：学习类手机是弱分神/消歧证据；手机类别不允许污染独立视觉证据。
- 产品语义确认 9：证据不足输出 UNKNOWN，并使用短暂保持 + 时间衰减。
- 产品语义确认 10：误提醒优先级最高，允许通过更长确认时间换稳定性。
- 产品语义确认 11：单主用户/座位 ROI，忽略路人；产品边界澄清完成，进入架构方案比较。
- 用户指出初版方案仍显得 pose/阈值中心；已修正为 person、learned keypoint-state、face/head-pose、可选 appearance prone 多专家架构，pose 仅为特征源。
- 调研官方边缘部署资料与面部视频注意力研究：确认 MP257 约 1.2 TOPS INT8、ST 有 face landmark/pose 工具链；研究结果不足以支持“直接读出发呆”，设计改为可观测分神风险与按需多专家调度。
- 用户批准模型架构与 Python-first 工程路线；新增 5 天展示期限，开始确认子项目接口、容错和最小验收范围。

- 读取总工作区 README，确认应用仓库位置与硬件资料边界。
- 核对应用仓库：`main` 分支，工作树干净，无现有规划文件或 `AGENTS.md`。
- 创建本轮诊断用计划、发现与进度文件。
- 下一步：读取构建入口、视觉/行为核心实现和现有测试，绘制实际数据流。
- 尝试使用 `rg` 建立符号索引时被系统拒绝执行；已记录，后续改用 Git 和 PowerShell 原生检索。
- 完成受审文件索引与核心接口初读；发现视觉结果存在 V1 与通用字段双轨协议、多个硬编码置信度阈值，以及行为状态/等级语义可能混杂。
- 静态追踪视觉调度、质量门控、真实/回退适配器、事件检测、时间窗口、风险评分和状态机；已记录 12 个有代码证据的问题，下一步核对配置来源、相机时间戳、队列语义和三级命令生成链路。
- 完成相机/队列/线程生命周期、模型注册表和三级提醒策略审查；累计记录 22 个问题或高可信风险。下一步用现有测试和定向测试验证关键结论，并检查现有覆盖是否会漏报。
- 审查 CMake 与相关测试，确认现有策略测试绕过上游可达性、FSM 测试固化错接时长、真实 NPU 路径默认不进入主机测试。发现可复用的现有构建目录，准备先运行基线测试。
- 首次基线 CTest 调用因多配置生成器缺少 `-C` 而未执行任何测试；已记录，将选择实际已构建配置重试。
- 旧构建缓存来自仓库迁移前路径，不能安全增量编译；将保留旧产物并创建新的 `_workspace/builds/review_debug`，避免越界写入。
- 新构建配置发现当前宿主环境没有可用的 Visual Studio C/C++ 编译器；已记录，正在检查是否有其他工具链及可直接运行的现有测试二进制。
- `ctest -C Debug` 实际运行了旧的 `test_device_manager` 与 `test_ble_gatt`，两项通过；另外 8 项因二进制缺失/旧绝对路径未运行。宿主也未安装 WSL，故不再尝试本机构建，将用定向公式复现和源码证据完成核心诊断。
- 完成四组源码等价定向复现：手机倍率污染整体风险、低置信度帧率偏置、无观测空洞被计为连续时长、pitch 像素单位错误；均与静态结论一致。
- 继续核对模型随附 Python/config、状态存储和参考部署 unit：标定统计显示阈值不可分，模型未纳入 CMake 安装，参考服务强制 mock，并发现整对象读改写丢更新与单槽手机信号覆盖两项并发风险。
- 用户新增决策：后续删除“困倦”状态；本轮暂不做碎片化代码删除，先完成视觉字段生产/消费矩阵供用户确认其余旧版字段。
- 已完成 VisionResult 14 组字段的生产/消费矩阵，并列出困倦删除的直接依赖；待完成剩余并发审查与优先级汇总后交给用户确认字段去留。
- 并发、时序、资源、错误路径与验证阶段完成；进入最终优先级汇总。产品代码尚未修改。
- 审查完成：形成 P0/P1 优先级、32 项问题/风险、4 组定向复现、视觉字段矩阵与困倦删除依赖。仅新增三份审查记录文件，未修改产品代码。
## 2026-07-14 通信接口审查进展

- 已核对 `BleGattServer`、`AppCommandService`、`PhoneFactBuffer`、`ReminderPolicy`、`DeviceManager`、`BleBandController` 及相关测试。
- 已确认用户对展示模式的修正：现场摄像头是正式会话的只读展示切面；预录 NPU 是独立演示；NPU 回退是纯前端同步动画且不接受手动手机输入。
- 当前仍处于设计确认阶段，未修改产品业务代码。

## 2026-07-14 预日志回退完成与板端验收

- 将 `no_npu/预录.mp4` 通过板端正常 NPU 业务链完整预跑约 360 秒，记录 1318 个真实样本；未人工划分或填写视觉状态。
- 生成并部署 `r01.mp4`～`r12.mp4` 与同名 12 份 trace；纯前端按钮和 NPU 自动失败回退均使用该视频/预日志对。
- 分离 `RECORDED_NPU` 与 `FRONTEND_REPLAY` 的素材选择；页面预日志回放按视频时间同步视觉、手机与融合结果，手机按钮保持锁定。
- 修复 MP4 大响应被 2 秒 socket 超时截断的问题，ARM 全目标重新交叉编译并发布到 `/opt/edgefocus/releases/20260714-225216`。
- 板端实测 `RECORDED_NPU`：p02、4.10 FPS、244 ms、17 个关键点、presence=1、JPEG 证据帧正常。
- 板端故障演练：缺失 NPU 视频自动进入 r01 纯前端回放并加载 98 个预日志样本；演练后临时环境变量已清空。
- 浏览器实测首次进入、退出和二次进入均有完整 1280×720 视频；证据叠加、四轨 30 秒时间线和页面状态同步正常。
- 最终板端状态：`focus_app.service=active`、`LIVE_CAMERA / IDLE`、无活动会话，NPU 预录与预日志回放资源均 ready。
- 修复单片循环：纯前端 `ended` 自动切换下一 rNN 及同名 trace；预录 NPU 在 EOF 自动打开下一 pNN，并调用 `resetContinuity()` 防止跨片污染。
- 删除经人工复核会把正常写字误报为趴下的 r04/r05 视频和 trace；板端剩余 10 组回放素材。
- 板端验证纯前端顺序为 `r01 → r02 → r03 → r06`，控制台无错误；预录 NPU 从 p06 自动切到 p01 后仍为 RUNNING、3.88 FPS、17 个关键点。
- 播放列表修正版发布到 `/opt/edgefocus/releases/20260714-233500`，最终恢复 `LIVE_CAMERA / IDLE`。
# 2026-07-15 主程序架构交接基线

- 新增仓库根入口 `AGENTS.md`，约束后续 agent 的阅读顺序、稳定边界、关键目录、验证命令和部署入口。
- 新增 `docs/architecture/current-architecture.md`，以当前源码为事实基线，覆盖进程模块、正式会话生命周期、视觉/手机/提醒链、Demo 隔离、并发所有权、禁用能力和部署拓扑。
- 文档明确区分目标架构与当前兼容债务：`SessionOrchestrator` 是正式会话唯一所有者；`LegacySessionProjectionFeature` 与 risk-score 往返映射仅是迁移桥，不是新功能扩展点。
- 已逐项核对 `main.cpp`、会话编排、视觉链、App outbox、手环重试队列和 CMake 生产源；三个相对链接全部存在，Markdown 围栏成对，`git diff --check` 无新增格式错误。
- 架构文档阶段完成。下一阶段才开始读取 `_workspace/fan_develop` 并评估 A35/Linux 与 M33 的风扇控制职责边界；此时不修改固件或主程序。

# 2026-07-15 M33 风扇控制规格

- 用户批准混合方案 B，并确认首版先只做风扇；云台和电源板预计后续硬件就绪，但本轮只预留协议扩展位。
- 完成 `docs/superpowers/specs/2026-07-15-m33-fan-control-design.md`：M33独占 PB10 PWM/PB11 tach，Linux bridge 每秒上报 `cpu-thermal`，3 秒失联进入安全高转速。
- 规格保留现有 Linux 原型的 25 kHz 五档曲线，新增 2°C 降档迟滞、温度序号/TTL、tach 堵转确认、版本化 RPMsg、M33 WWDG 和原子状态文件。
- 自审补充两项安全约束：找不到 `cpu-thermal` 时不得回退到任意温区；OpenAMP 初始化不得阻塞 PWM/安全监督任务。
- 设备树、RIF、TIM 通道、PB11/CAN 冲突、remoteproc 和复位期间电气默认状态仍是上板实施门槛，未取得事实前禁止猜测配置。
- 用户新增并批准自适应降温策略：70°C 连续 3 秒进入增压，每 5 秒提高约 5% 转速；10 秒趋势下降至少 0.5°C 后保持；再次升温恢复爬升；80°C 立即最高安全转速；62°C 稳定 10 秒后限速回落到普通曲线。

# 2026-07-15 M33 BSP 适配进展

- 已定位完整 ATK `OpenAMP_TTY_echo`、TIM、GPIO 与 FDCAN 示例，以及 CubeIDE 1.17.0 / GCC 12.3.rel1 工具链；不再需要额外下载软件包。
- 已确认 PB10 的固件映射目标为 TIM17_CH1/AF7，TIM17 RIFSC ID=15；PB11 为 GPIOB pin 11，且其旧 FDCAN1 AF7 冲突已由 v17 DT 禁用。
- 固件工程采用“保留厂商包只读基线、临时分阶段复制工程、应用 EdgeFocus 覆盖层、CubeIDE 无头构建”的方案，避免提交整个 BSP 和 IDE 缓存。
- M33 首次上板的所有权门槛已收紧：停止并禁用 `fan-temp-control.service`，撤销 Linux PWM export/驱动占用，确认 PB10/PB11 无消费者后才启动 M33；失败则先停 M33 再按保存状态回滚 Linux。
- 用户确认停用 Linux 风扇服务不影响风扇硬件默认运转，因此接管窗口具备被动散热安全性。
- 完成 ATK 板级适配：PB10/TIM17_CH1 25 kHz PWM、PB11 EXTI tach、RIF 资源申请、100 ms 控制 tick 与 OpenAMP VIRT_UART 传输已接入便携控制核心。
- 绕过 CubeIDE patched Make 的混合 C/C++ 崩溃，新增使用厂商源码/链接脚本和 CubeIDE GCC 的 CMake/Ninja 可复现构建；完整 57 个目标构建成功。
- 产物已生成到 `_workspace/b/m33fan/artifacts/EdgeFocus_FanController_CM33_NonSecure_stripped.elf`，ELF 头与体积已用 `arm-none-eabi-readelf/size` 复核。
- 新增 `firmware/m33/fan_controller/atk/deploy_m33.py`，支持只读 probe、原子 deploy 和 stop；部署严格检查 `fw_format=ELF` 及启动后的 `state=running`、`/dev/ttyRPMSG0`。
- 阅读并抽查用户提供的 ATK 异核通信手册，确认部署脚本采用的 `remoteproc0`、ELF 与 rpmsg-tty 路径无偏差。
- 板端 SSH 当前不可达，尚未发生任何板端变更；下一步为恢复联网后先 probe，再做 PWM/tach/RPMsg/温控故障注入和回滚演练。

# 2026-07-15 M33 风扇受控上板结果

- 对齐厂商 GCC/linker/libmetal 配置后，OpenAMP smoke 与 ResourceManager smoke 均上板通过。
- 查明 Linux timer/PWM 的真实父子绑定并为 `deploy_m33.py` 加入完整 unbind 和失败回绑。
- 完整 PWM 接管仍导致一次整机 watchdog 复位；通过 COM3 确认没有 boot loop，系统自动恢复默认 echo、RPMsg 和 Linux PWM 驱动。
- 按自研底板运行 DT/pinctrl 修正 PB10：从普通 `TIM17_CH1` 改为互补 `TIM17_CH1N`，匹配 pull-down/low slew，加入 TIM extended HAL 与 master config；本机 CMake/Ninja 构建成功，text/data/bss 总计 31,908 bytes。
- 为避免盲测，修正版暂未再次部署；PB11 tach 保持禁用，bridge 和 M33 风扇自启动均未启用。
- 用户批准后部署 CH1N 修正版：系统未复位，M33 remoteproc running，但 `/dev/ttyRPMSG0` 未建立；脚本自动停止测试固件并恢复 Linux驱动。随后手动恢复默认 echo，`running` 且 RPMsg 正常。
- 已批准并开始实施通信先行的分阶段 bring-up；规格提交为 `e4a9d55`。
- 完成 Linux→M33 的 `EFAN_PROBE` 启动握手，解决 RPMsg 目的端点尚未建立时阶段日志无法送达的问题。
- 单次受控诊断取得阶段轨迹 `0 OK → 1 OK → 2 OK → 3 BEGIN`，确认当前卡点位于 TIM17 初始化阶段 3；测试未触发系统 watchdog 复位。
- 失败回滚再次通过：测试固件停止、Linux timer/PWM 回绑、默认 echo 恢复并重新出现 `/dev/ttyRPMSG0`。下一步只拆分阶段 3，不重复原粒度盲测。
- 完成 0–9 细粒度阶段编号、本机构建与第二次单次诊断；板端轨迹停在 `stage 6 BEGIN`，已确认具体调用为 `HAL_TIMEx_MasterConfigSynchronization`。
- 第二次诊断后安全回滚再次通过，整机未重启。下一步核对 TIM17 是否需要/支持 master config；若该调用对本 PWM 场景非必要，则删除并继续验证 GPIO/CH1N 启动阶段。
- 已由 CMSIS 实例能力宏确认 TIM17 不支持 master config；删除错误迁移的 `HAL_TIMEx_MasterConfigSynchronization`，保留阶段 6 作为显式能力跳过点，准备重新构建并验证后续 GPIO/CH1N 阶段。
- 删除 master-config 后续跑时，部署客户端被本地 14 秒超时终止，板端随后 watchdog 重启并自动恢复默认 echo/驱动；由于阶段流未被回收，当前不能断言具体停点。
- 调整下一步为带 `stop-after-stage` 的阶梯验证：GPIO → channel config → PWMN start，每次都由脚本主动失败回滚，避免再次合并跨越危险边界。
- 阶梯 1 已通过：`stage 6 OK` 后按设计停步并完成自动回滚，排除 GPIO AF7 配置；现将停步点推进到 stage 7，仅测试 channel config。
- 阶梯 2 已通过：`stage 7 OK` 后按设计停步并自动回滚，排除 channel config；下一步拆解 `HAL_TIMEx_PWMN_Start` 的 CCER/BDTR/CR1 操作，不直接整段重试。
- 完成 PWMN 三步拆分上板：CC1NE、MOE、CEN 全部读回成功，stage 8 稳定 40 秒。
- 通过 stage 9 子标记定位整板复位根因到 `HAL_GetUIDw0()`；替换为非安全域可用的启动 nonce 后，应用初始化稳定 40 秒、完整控制循环稳定 60 秒。
- 当前 M33 固件正在板端独占 TIM17/PB10，Linux timer/PWM 已让渡；下一步只部署独立温度 bridge，不替换 focus_app 主程序。
- 独立 A35 bridge 已交叉编译并安装为 `/usr/local/bin/edgefocus-fan-bridge`；systemd unit 保持 static/manual，未替换或耦合 `focus_app.service`。
- 首次断链注入暴露 M33 在 `ShutdownNotice` 后不接受新 HELLO 恢复传输的问题；修复为“有效 Linux HELLO 同时恢复 transport/protocol”，并增加主机回归测试。
- 修复版上板后通过 bridge stop 6 秒再 start：约 1 秒进入 `FAIL_SAFE`，两帧 1 Hz 温度后约 2 秒恢复 `CONTROLLED`，remoteproc 与 A35 均未重启。
- 恢复后连续 52 秒保持 `protocol_ready=true`、`CONTROLLED/NORMAL`、无错误；`focus_app.service` active，Linux timer/PWM 保持 unbound，旧 `fan-temp-control.service` inactive/disabled。
- PB11 tach 继续禁用，因此 fault bit 32 `TachUnavailable` 为预期状态；M33 loader 与 bridge 开机自启动尚未启用。

## 2026-07-15 M33 风扇校准与服务化

- 完成 CH1N 逻辑极性修正、PB11 EXTI 测速、34,000 ns 常温档和 8,000 ns 故障安全档上板验证；主机 3/3 测试通过。
- 完成 36,000/30,000/22,000/14,000/8,000 ns 转速标定，确认 2 PPR 候选唯一，34,000 ns 常温档多次启动约 660～1,140 RPM，满速约 5,760 RPM。
- 新增幂等 transition、M33 loader、bridge 依赖、启动健康门和 Linux PWM fallback；修复重复 sysfs 写入 `EINVAL`、RPMsg 慢重建和共享 runtime 目录被删除三类问题。
- Linux fallback 两次重复执行均通过读回与 PB11 验证；硬件无软件控制 30 秒仍有 7,009 个 tach 上升沿。
- 新服务已安装但保持 disabled，厂商 loader 仍 enabled；等待风扇接口波形留档后批准校准文件，再执行两次冷启动。
- 用户随后明确豁免波形截图并批准启用；实现使用独立 waiver 字段保留事实，开始执行开机服务与两次重启验收。
- 第一次重启暴露 bridge 的 `ConditionPathExists` 早判竞态：M33 成功但 bridge 被跳过；已移除该 condition，冷启动计数从修复后重新开始。
- 第二次重启确认 `After=dev-ttyRPMSG0.device` 会等待不存在的 systemd device unit，拖慢 multi-user 后取消 bridge；已改为只依赖已验证 TTY 的 M33 loader，再次重置冷启动计数。
- 第三次启动定位到 loader 内的 `systemctl stop bridge` 会取消已排队的开机 bridge job；拆成仅手动使用的 `--stop-bridge` 后，生产 unit 不再自取消依赖任务。
- 最终开机链已启用：厂商 loader disabled，M33 loader/bridge/健康门 enabled。修复后连续两次完整 systemd reboot 均通过，当前 M33/bridge/focus_app active、健康门 success、Linux timer/PWM unbound。
- 用户随后完成一次物理断电重上电，开机链再次通过；同时发现 RTC 断电回到 2000-01-01，已重新校时写 RTC 并记录后续 App/网络校时需求。
# 2026-07-15 云台调试设计启动

- 已开始只读盘点 G431 控制文档、示例人脸跟踪程序和主程序现有 `GimbalController/GimbalMonitor/GimbalSessionFeature`。
- 已按用户决定冻结 M33 功能范围：不再为云台或充电开发 M33 固件，保留当前风扇实时安全控制成果。
- 当前仍处于设计与硬件映射确认阶段；尚未写入 PD4/PD7、尚未打开 UART4、尚未发出任何电机运动命令。
- 已通过板端只读检查确认 `ttySTM1 → serial@40330000` 可用、当前无人占用，PD4/PD7 均为未申请输入，且生产服务仍以 `EDGEFOCUS_DISABLE_GIMBAL=1` 运行。
- 已确认旧云台模块不能直接启用：旧预检命令与新 G431 协议不一致，串口同步轮询也缺少对异步 `BOOT_ALIGN` 的安全处理。
- 硬件侧确认 PD4 仅为 BOOT0。设计已删除 MP257 远程复位假设，正常启动改为完全遵循示例的 PD7 低→高自动对齐流程；下一项待确认是 `G431_FAULT_OUT` 的 MP257 侧 GPIO。
- 已用最新网表和板端 pinctrl 交叉确认：PD6=G431 Fault、PD10/PD11=I2C4、Linux `/dev/i2c-1`；M33 范围不变。
- 已通过只读寄存器验证 STUSB4500 在线地址为 0x2A，当前状态为未接外部 VBUS。未扫描整条总线、未写寄存器或 NVM。
- 已定位现有 Home/Go 代码的覆盖问题和虚假 67% 电量回退；下一步先确认手动覆盖生命周期，再提交完整方案比较。
- 用户已确认 Home/Go 手动覆盖采用“保持到下一次插拔”语义；正在核对两个档位实际需要调节的工作频率。
- 用户已确认 Go 首版只做应用层降频，不调整 Linux CPU 调频策略；安全检测和通信频率保持一致。
- 用户已确认云台首轮和正式预检都采用分阶段安全验收；已形成三条接入路线，准备提交方案选择。
- 用户选择方案 2，并将内核驱动方案列入国赛后升级；PRECHECK 自动搜索行为已细化，正在提交完整设计审批。
- 完整设计已获用户批准并写入 `docs/superpowers/specs/2026-07-15-gimbal-power-heterogeneous-control-design.md`；当前进行独立规格审查，尚未开始产品代码或硬件动作。
- 用户补充 Fault 后需要偶尔检查云台是否恢复；规格采用“PD7 保持低、低频被动读取 PD6/PING/FAULT/STAT、连续健康后标记 recoverable”的安全恢复探测，禁止后台自动运动和无限重试。
- 两轮独立规格审查已补齐：PRECHECK 2 秒单步/30 秒总截止与 generation 取消、Fault 恢复显式 `gimbal_realign()`、STUSB 四态真值表/持续 UNKNOWN 超时、PD6 R75 上拉实测门、旧 App 能力兼容和 Home/Go 量化回归指标；等待最终复审。
- 三轮规格审查的全部阻塞项已修复；最终将 PRECHECK 冻结为最多 9 个目标（最后一个为中心）、27 秒停止排队、30 秒硬截止，避免“无人回中”成为截止后的第十次动作。规格格式与 UTF-8 检查通过，现交用户审阅，尚未开始实现。
- 用户在书面复核中取消旧 App 兼容；规格已改为板端与新版 App 同步升级，彻底删除 `battery_*` 字段、旧版本分支、哨兵响应和旧客户端验收，正在进行针对性复审。
- 取消旧 App 兼容的针对性复审已通过：同时删除旧 `power_source` 别名，将样本年龄统一为 `external_power_sample_age_ms`；协议只保留 `external_power_*` 与 Home/Go。
- 用户批准最终规格；已生成 `docs/superpowers/plans/2026-07-15-gimbal-power-control-implementation.md`，共 12 个阶段，明确先纯逻辑、再独立 bring-up、最后主程序灰度。当前没有执行 GPIO/UART/I2C 写、云台动作或板端部署。
- 实施阶段 0 已完成只读基线冻结：三项正式服务 active，云台仍由 `EDGEFOCUS_DISABLE_GIMBAL=1` 硬禁用，ttySTM1 无占用，PD4/PD6/PD7 均未申请，STUSB `0x2A` 读回 `ID=0x25/0x0E=0x00/0x10=0x04`。结果写入 `docs/hardware/g431-stusb-board-inventory.md`，没有硬件写操作。
- 本机完整 CMake 因现有 SQLite/JSON FetchContent 在 124 秒内未完成而超时；没有重复下载，改用 Visual Studio BuildTools 直接编译无第三方依赖的 gimbal host tests。首轮硬件服务测试发现禁能态 Fault 会让只读 probe 提前失败，已限定为“probe 继续读取并记录 Fault，运动/恢复仍立即中止”。
- gimbal/power 的 6 个新增目标已通过正式 CMake host 测试和 STM32MP2 SDK AArch64 交叉编译；同一批 ARM 测试在板端 6/6 通过。`gimbal_bringup` 独立部署到 `/opt/edgefocus/tools/gimbal-bringup-20260715`，未替换主程序。
- 首轮无运动 probe 保持 PD4=0、PD7=0，实测 PD6=1；`VER/PING/STAT/FAULT/ANGLE` 全部无返回，内核计数 `tx=36/rx=0`。正式服务、M33 风扇和 bridge 始终 active，因此停止受限运动阶段并把原始事实写入 `docs/hardware/g431-bringup-results-2026-07-15.md`。
- 板端物理断电后系统时间再次落后；调试前已按本机时间校正并执行 `hwclock -w`，当前为 2026-07-15 CST。
- ARM 工具更新时首次 SHA 校验命令中的远端 `$(...)` 被 PowerShell 提前解释，上传文件停留在 `.uploading`，未替换旧工具；随后改为本机计算固定 SHA 文本、板端 `sha256sum -c` 后原子 `mv`，校验通过。

## 2026-07-15 Home/Go、供电与云台会话实现

- 删除产品协议与状态中的伪电量、充电、电压和旧 `power_source`；新增 STUSB4500 四态供电事实、样本年龄、Home/Go 有效档位、来源、覆盖标记和 epoch。
- `set_power_profile` 成为唯一 Home/Go 手动入口；`update_preferences` 不再接受或持久化该字段。App 协议与迁移交接文档已同步更新。
- 正式视觉 worker 接入 `VisionCadencePolicy`：PRECHECK/Home 250 ms、Go 333 ms、PAUSED 1000 ms，并在调度前丢弃积压帧只处理最新帧。
- 新增类型化云台硬件接口、经配置门禁的 PRECHECK 控制器、optional `GimbalSessionFeature`、session/sequence/manual-lease 命令和快照字段；当前未实测配置会拒绝创建服务及 `gimbal_precheck_v1` 能力。
- 主机完整 MSVC clean build 和 CTest 27/27 通过；同时修正旧提醒测试，把 App 推送排队与手机 event ACK 后的真实送达明确分开。
- STM32MP2 SDK AArch64 全目标（含 OpenCV、STAI、Demo HTTP、`focus_app` 与所有 ARM 测试目标）交叉编译通过；本轮只做编译契约检查，没有部署或替换板端服务。
- 当前仍未部署新主程序。云台 UART RX=0 与机械参数未通过，运动、对齐、Fault 注入继续封锁；Home/Go 的板端 NPU 定量轨迹尚未采集。
- 随后通过原子发布/回滚入口部署 `/opt/edgefocus/releases/20260715-221512`；发布包现包含 `config/gimbal-board.json`，部署与回滚都同步 config。
- 上板后 `focus_app`、M33 风扇和 bridge 均 active；`motion_verified=false`，`/dev/ttySTM1` 无占用，日志无 STUSB/Gimbal/致命错误。当前 STUSB 读回 `0x0E=0x00/0x10=0x04`，符合未接外部电源。
- 8 个 ARM 供电/节奏/云台逻辑测试在板端全部通过；预录 NPU 实测 p02 为 3.33 FPS、242 ms、17 关键点，退出后恢复 `LIVE_CAMERA/IDLE` 且 preview=false。
- 仍需用户配合完成一次外部供电插入/拔出，验证 Home/Go 自动切换、手动覆盖清除和同素材定量轨迹；当前不能声称该物理转换验收完成。

## 2026-07-15 PRECHECK 严格边界与云台自动健康门

- 用户明确 PRECHECK 不属于正式会话；视觉模型可运行，但正式计时、SQLite、PhoneFact、Fusion、episode、提醒和报告均从 confirm→FOCUSING 才开始。
- 用户说明旧云台硬禁用只是开发期措施；正常产品将移除 feature opt-in，由实测配置与当次 UART/Fault/对齐健康决定是否使用，紧急维修 disable 继续保留。
- 已写入并自审 `docs/superpowers/specs/2026-07-15-precheck-formal-session-boundary-design.md`；板子正在更换疑似不回弹的 G431 复位开关，本轮只修改和验证本地代码。
- 已完成本地两阶段会话实现：PRECHECK 只运行视觉/云台准备，confirm 才建立 Phone/Fusion、SQLite、旧 session 投影和正式计时；相机迁到 typed runtime。
- App V2 已区分 `runtime_active`/正式 `active`，拒绝 PRECHECK 手机事实，并阻断“取消预检推送上一场报告”。新增数据库边界与 BLE 协议回归测试均通过。
- 云台改为配置 + 运行健康自动门禁；初始化/中途故障不阻止主程序，PRECHECK 与后续跟踪能力共同降级，5 秒低频恢复探测后重新对齐。当前未实测配置未改，板端未连接、未部署、未运动。
- 本机已完成真正 clean 的 Release 全目标构建，CTest 28/28 通过，`git diff --check` 无错误；ARM 全目标编译与板端验证待硬件返回后继续。
- 板子返还后的首次连通性检查失败：`192.168.10.174` 两次 ICMP 均超时，组合的 TCP 22 检查在 15 秒总超时内未返回。未执行 SSH、GPIO/UART 访问或任何运动；下一步改用独立短 TCP/ARP 检查，不重复同一组合命令。
- 独立 3 秒 TCP 检查确认 22 端口开放，本机 WLAN 为 `192.168.10.186/24`。随后并发发起三条只读 SSH 时连接被板端关闭，疑似轻量 SSH 服务并发限制；下一步改为单连接串行只读盘点。
- 单连接 SSH 已成功：`focus_app`、M33 fan、fan bridge 均 active，正式 unit 仍含紧急 `EDGEFOCUS_DISABLE_GIMBAL=1`；板端刚启动约 2 分钟，时间回退到 2024-02-28。`/dev/ttySTM1` 无 owner，UART4 计数 tx=0/rx=0。
- 首次 GPIO 只读命令沿用旧 libgpiod 位置参数，板端新版工具将 `gpiochip3` 误当 line name 并退出；没有申请或修改 GPIO。后续改用 `gpioinfo/gpioget -c gpiochip3`。
- 已将板端时间校正为 2026-07-15 23:19 CST 并写入硬件时钟。新版 gpiod 文本输出均显示 inactive，但 `--numeric` 原始值确认 PD4=0、PD6=1、PD7=0，符合 BOOT0 低、Fault 未触发、Enable 低的安全态。
- 文档记录的 `/opt/edgefocus/tools/gimbal-bringup-20260715/gimbal_bringup` 当前不存在，`find /opt/edgefocus/tools` 也未找到同名二进制；未用正式主程序代替诊断。下一步先 ARM 编译并只恢复独立工具。
- ARM 源码归档已生成并上传编译机。首次内联远端编译命令因 PowerShell 提前展开 `$root` 导致嵌套引号 EOF，0.4 秒即退出，尚未配置或编译；不重复内联变量方案，改为上传固定路径的 build-only 脚本。
- 固定 build-only 脚本已在编译机 `/tmp/edgefocus-g431-recheck-2320` 完成新的 clean AArch64 全目标构建，耗时约 147 秒并成功退出；`focus_app`、全部 ARM 测试及独立 `gimbal_bringup` 均已生成。仅有既存 OpenCV SDK `libnumeric.a` 导入警告，未影响链接；本轮没有替换板端主程序。
- 新 `gimbal_bringup` 已确认是 AArch64 ELF，大小 4,843,800 bytes；编译机与本机 SHA-256 均为 `0081205325341cf9d07d9c48cd93b3066f6dd597c3284bb051e6b92ef284df54`。下一步仅安装该独立工具并在板端复核同一哈希。
- 独立工具上传到板端临时路径后再次确认为同一 AArch64 ELF 与同一 SHA-256；此时 `focus_app.service` 仍 active，`/dev/ttySTM1` 无 owner。尚未执行探测或运动。
- 工具已安装为 `/opt/edgefocus/tools/gimbal-recheck-20260715-2320/gimbal_bringup`，板端哈希校验通过且 usage 正常打印。安装命令末尾用于接受预期退出码 64 的 PowerShell `$?` 被提前展开成 `True`，因此 shell 最终报 `bad number`；该错误发生在安装、哈希和 usage 输出之后，不影响文件或硬件状态，后续不再使用该写法。
- 探测前最后安全门再次通过：三项服务 active，正式 unit 仍有 `EDGEFOCUS_DISABLE_GIMBAL=1`，板级配置仍 `motion_verified=false` 且机械参数全为空，ttySTM1 无 owner，PD4/PD6/PD7=`0/1/0`。可以执行禁能无运动 probe。
- G431 修复后的禁能无运动 probe 已执行一轮：PD4/PD7 始终保持低，`VER/PING/STAT/FAULT/ANGLE` 五条全部超时，UART transcript 为空；内核 ttySTM1 计数变为 tx=31/rx=0。探测退出后 PD4/PD6/PD7 仍为 `0/1/0`，ttySTM1 无 owner，三项正式服务均 active。由于 UART RX 门槛仍未通过，明确停止对齐与运动阶段。
- 本地控制说明、G431 参考程序与最新网表已复核：波特率、8N1、CRLF 和 TX/RX 交叉连接均与当前实现一致。资料建议的下一步是 PD7 低→高触发自动电角度对齐并等待 `BOOT_ALIGN,OK`，该动作可能产生机械运动，因此保持封锁，先继续确认 MP257 pinctrl/供电证据。
- 板端 live pinmux 与当前 DTB 源已交叉确认：ttySTM1 对应 `serial@40330000`，PG14/PG15 均为 AF6，PG14 配置为推挽 TX、PG15 为 RX 输入；与最新网表的交叉连接方向一致。软件侧未发现设备节点或 TX/RX 复用颠倒。
- 用户已确认机械净空并允许云台转动，同时转述硬件意见：诊断回复可能要求 Enable=1。已把已批准规格澄清为“UART/PD6 前置检查 → PD7 低满 100 ms → 高沿触发 BOOT_ALIGN → OK 后执行 PING/FAULT/STAT/ANGLE”，失败立即 PD7=0；正在修正现有 `enableAndAlign()` 的禁能态 PING 死锁。
- `enableAndAlign()` 已按澄清后的硬件顺序修改：禁能态只检查 UART 已打开与 PD6 高，保持 PD7 低 100 ms 后拉高并等待 `BOOT_ALIGN,OK`，再执行 `VER/PING/STAT/FAULT/ANGLE`；后置诊断任一步失败都会锁存 Fault 并立即禁能。硬件服务测试已同步改为断言 Enable 高沿发生在首条 PING 之前，尚待构建验证。
- 修改后的 `test_gimbal_hardware_service` 与依赖已在 host Release 成功重编译。首次用猜测的 CTest 名 `gimbal_hardware_service` 过滤时显示无匹配测试，未执行任何测试；下一步先列出真实名称，再运行二进制/正确 CTest 项。
- 查明真实 CTest 名为 `test_gimbal_hardware_service`，定向测试 1/1 通过。下一步只同步本次三个实现/测试文件到刚完成的 clean ARM 构建树，重编译独立工具和对应 ARM 测试，不重新部署主程序。
- 三个修改文件已同步到编译机，ARM `gimbal_bringup` 与 `test_gimbal_hardware_service` 均重编译成功。命令随后误在 x86_64 编译机直接执行 AArch64 测试二进制，按预期报 `Exec format error` 并使组合命令退出 1；构建产物有效，测试将上传到 ARM 板端执行，不再在编译机运行。
- 新 ARM 工具与测试确认均为 AArch64 ELF；编译机/本机哈希分别为工具 `a3c88a985f0ac49617b6b4f7da203308f6285518dd9d11775c6404db2ee5f37d`、测试 `b8a99052d77e8bdd9447b64159e42fb3d1329b8372bc14f99f09dc6c473b8bb6`。下一步上传板端校验并先运行纯 fake-I/O 测试。
- 新工具/测试上传板端后哈希再次一致；ARM 板端 fake-I/O `test_gimbal_hardware_service` 通过。运动前最后状态仍是三项服务 active、正式主程序紧急禁用云台、ttySTM1 无 owner、PD4/PD6/PD7=`0/1/0`。
- 修正工具已原子替换到独立目录，板端 SHA-256 为 `a3c88a985f0ac49617b6b4f7da203308f6285518dd9d11775c6404db2ee5f37d`，未替换正式主程序。
- 用户确认允许转动后执行一次受控 `align`：PD7 低 100 ms 后进入 Enable/等待 `BOOT_ALIGN,OK`，但 3 秒内 UART transcript 仍为空，工具以 `align_timeout` 进入内部 Fault 并立即禁能；未进入后置诊断或任何目标角度命令。退出后实测 PD4/PD6/PD7=`0/1/0`、ttySTM1 tx=31/rx=0、无 owner，三项服务均 active。需要用户确认现场是否曾发生机械动作，以区分“Enable 到达但固件/UART无响应”和“Enable/供电/复位链未生效”。
- 用户现场确认首次高沿期间云台发生动作，排除“PD7 完全未到达/云台完全无电”。下一步增加仅限独立 bring-up 的 `enabled-probe`：高态短时监听并主动发送五条诊断，禁止目标运动命令，结束必禁能；正式 Ready 门槛仍要求 `BOOT_ALIGN,OK`。
- 独立 `enabled-probe` 已实现：需要双重运动确认参数，高态被动监听后仅发送五条诊断；故障/失败会禁能，工具退出也会 STOP/禁能。正式 `enableAndAlign()` 与 Ready 门槛未放宽。Host Release 工具/硬件服务测试已重编译，定向 CTest 1/1 通过。
- 四个变更文件已同步编译机，ARM 工具/测试重编译成功；新哈希为工具 `4b930bb86101560761919fd3605561e4ab441bfec8063d791d05e38c234cb1c5`、测试 `e9f15bb2e9bf8a4ad1f62e9866b56add721bd20fbd53114f46ee8c97f3b994c7`。
- 编译机→本机哈希一致。上传板端的组合 scp 在本地约 29 秒超时退出 124，但板端随后只读检查确认两个文件大小完整且 SHA-256 均与源端一致；因此不重复上传，继续板端测试和原子安装。
- 板端 `enabled-probe` 工具哈希与 fake-I/O 测试通过；运动前 PD4/PD6/PD7=`0/1/0`、ttySTM1 无 owner、三项服务 active。
- 实际使能诊断完成：高态先被动监听约 1 秒，再发送 `VER/PING/STAT/FAULT/ANGLE`，五条仍全部超时且 transcript 为空；ttySTM1 计数从 tx=31/rx=0 增至 tx=62/rx=0，证明 Linux 已实际发送第二轮 31 字节但没有收到一个字节。工具已锁存诊断失败并禁能，退出后 PD4/PD6/PD7=`0/1/0`、无 owner、三项服务 active。结合现场确认云台有动作，下一步不再重试软件运动，需查 G431 UART 固件输出和 G431_TX→R78→PG15 物理链路。
- 全工作区未找到 G431 固件工程或镜像，无法在软件侧继续核实板上 UART 初始化/协议版本。当前最短硬件闭环是同时观察 G431 PC10/U17.52（TX）、R78 两端和 MP257 PG15，并确认 MP257 命令在 R77→G431 PC11/U17.53 到达；测量前正式云台保持紧急禁用。
- 用户提供最终协议，撤销“使能后才回复”假设。正在恢复严格启动顺序：安全低态稳定 500 ms、最多三次 PING 并接受 `OK,G431`、成功后低态 100 ms 再 Enable、高沿后最多 10 秒等待 `BOOT_ALIGN,OK`；同时删除绕过 PING 的 `enabled-probe`。正式主程序仍未部署。
- 严格协议实现已完成：`OK,G431` 识别、500 ms 稳定期、3×500 ms PING 门槛、PING 成功后 100 ms 低态、高沿与 10 秒对齐超时；PRECHECK 三个调用点同步为 10 秒，`enabled-probe` 已从服务和工具删除。新增回归确认 PING 必须先于 Enable，三次无回复绝不产生高沿。
- Host Release 的协议、硬件服务、PRECHECK、bring-up 工具和 `focus_app` 均编译通过；云台定向 CTest 3/3 通过。仅有既存 MSVC `getenv` 警告，与本次无关。
- 9 个协议相关文件已同步编译机；ARM bring-up、三项云台测试和 `focus_app` 均重编译成功。新工具 SHA-256 为 `5c3cb3724c25217df3fbb29c4d81856c08f3e62ce154a9fad19d1c1437315a5b`；本轮仍只准备独立工具/测试，不部署主程序。
- ARM 工具/三项测试已下载本机且哈希与编译机一致。首次向板端上传最终协议工具时 TCP 22 在 5 秒内超时，scp 未开始传输；没有替换板端文件或触发云台。下一步先做一次短 TCP 可达性复核，不盲目重复上传。
- 板端网络出现瞬时抖动：TCP22 一度超时，随后 2 次 ping 丢 1 次并恢复 SSH。改为逐文件串行上传后，最终协议工具和三项 ARM 测试在板端哈希全部一致；协议、硬件服务、PRECHECK 测试 3/3 通过，独立工具已原子替换为 SHA-256 `5c3cb3724c25217df3fbb29c4d81856c08f3e62ce154a9fad19d1c1437315a5b`。三项服务 active、ttySTM1 无 owner、PD4/PD6/PD7=`0/1/0`，正式主程序未替换。
- 准备执行最终严格门槛实测时，SSH 在连接阶段超时，因此工具没有启动、GPIO/UART 均未申请；随后 3 次 ping 仅返回本机 `Destination host unreachable`/超时，确认板端 Wi-Fi 已再次掉线。停止重试，等待网络恢复后只需补跑一次门槛验证。
- 2026-07-16：用户确认 SSH 恢复，并提供 COM3 作为后备控制台。SSH 串行预检通过：三项服务 active、正式 unit 仍紧急禁用云台、独立工具哈希为最终版 `5c3cb...15a5b`、ttySTM1 无 owner、PD4/PD6/PD7=`0/1/0`、UART4 当前 tx=62/rx=0。可以补跑严格 PING 门槛验证；若 SSH 再失败才切 COM3。
- 最终严格门槛已实测：工具先保持 Enable=0 等待 500 ms，再发送 3 次 `PING\r\n`；三次均无任何 UART 字节，按设计在约 3.2 秒内以 timeout 退出，没有进入 Enable/对齐。UART4 tx 从 62 精确增加到 80（3×6 bytes），rx 保持 0；退出后 PD4/PD6/PD7=`0/1/0`、ttySTM1 无 owner、三项服务 active。证明软件协议与安全门已正确，剩余阻塞明确为 G431 UART 输出/物理 RX 链。
- 本机串口列表确认 `COM3` 存在；未打开端口。后续仅在 SSH 不可达时用其作为板端控制台回退，避免与正常 SSH 调试并行。
- 用户重刷 G431 固件并要求只调独立工具、不动主程序。首次 SSH 预检在连接阶段 8 秒超时，未执行任何板端命令；按用户授权改走 COM3 控制台，不重复同一 SSH 尝试。
- COM3 已确认是 `root@ATK-DLMP257` 板端控制台（115200 8N1）。首次把整条长预检命令一次性写入时，控制台报告 `ttySTM0 input overrun`，命令中的工具路径被丢字符成无效路径；仅执行了部分只读命令，没有启动云台工具或申请 UART4/GPIO。板端似乎刚重启约 163 秒，时间回退到 2024，UART4 tx/rx=0，PD4/PD6/PD7=`0/1/0`，三项服务 active。后续改为短命令逐字符节流发送，避免重复串口过载。
- 改用 COM3 逐字符 3 ms 节流后预检可靠：独立工具存在、ttySTM1 无 owner、PD4/PD6/PD7=`0/1/0`、重启后 UART4 tx/rx=0。仅运行独立禁能 `probe`，五项 `VER/PING/STAT/FAULT/ANGLE` 在新刷固件上仍全部超时，transcript 为空；结束后 UART4 tx=31/rx=0，GPIO 仍 `0/1/0`、无 owner。没有拉高 Enable、没有对齐或运动，也没有修改/部署主程序。
- 首轮测试搜索只找到会话级 fake hardware；对全仓递归搜索因包含构建产物在 15 秒超时，未改文件。后续固定使用 `git ls-files` 限定受控源码，不再扫描 build 目录。
- 2026-07-16：用户批准 G431 单向 STOP 诊断规格后，新增仅属于独立 `gimbal_bringup` 的 `oneway-stop` 路径；三重显式参数、命令白名单 `PING/HOLD 1/STOP`、PD6 轮询和 RAII 必定禁能均已落地，正式 `GimbalHardwareService` 的回包门槛及 `focus_app` 链接内容未放宽。
- 本机新诊断与原硬件服务回归 2/2 通过；AArch64 工具及 fake-I/O 测试交叉编译成功，测试在板端通过。板端文件 SHA-256 分别为工具 `28cc0ec266ea84d499da74bc88df2c410e5a8620496a5b22e9c0c65e3607f5fa`、测试 `fbdb9c23ca4ae315ca83b617cd5936a699257545cd0660f5a42d22f71798b0a5`，只安装到独立工具目录，未替换主程序。
- 唯一一次 `oneway-stop` 实机序列已完成：前置状态为 ttySTM1 无 owner、PD4/PD6/PD7=`0/1/0`；实际发送 `PING/HOLD 1/STOP`，PD6 未触发、UART transcript 为空。UART4 计数由重刷后 probe 的 tx=31/rx=0 精确变为 tx=51/rx=0（新增 20 字节与三帧完全一致）；退出后无 owner、GPIO 恢复 `0/1/0`。仍待用户现场确认 STOP 后是否明显卸力。
- 用户进一步批准绝对 `PITCH 5` 的一次性听声/目视测试并确认机械净空。独立工具新增四重确认入口 `oneway-pitch5`，本机新诊断及原硬件服务回归 2/2 通过；AArch64 工具 SHA-256 为 `f11f9771d4a1933218f5f7dba9068820b5f288adf021412fb9ad750bffdb407e`。板端 USB Wi-Fi 变为 `INTERFACE_DISABLED`，因此通过 COM3 传输 108225 字节压缩包并在解压前后校验哈希，未修改主程序。
- 唯一一次 `oneway-pitch5` 已执行：实际发送 `PING/HOLD 1/PITCH 5/STOP`，PD6 未触发、UART transcript 仍为空；ttySTM1 tx 从 51 精确增加到 80（新增 29 字节与四帧完全一致），rx 仍为 0。结束后 ttySTM1 无 owner、PD4/PD6/PD7=`0/1/0`。等待用户报告电机声或角度变化后给出单向链结论。
- 用户现场确认 `PITCH 5` 全程没有电机声、没有角度变化，先前 STOP 也无法提供可确认的命令执行证据。因此停止继续发送运动命令，结论转交硬件：Linux 侧仅能证明 ttySTM1 写入计数完整，不能证明信号经过 R77 到达 G431 PC11；同时 G431→R78→PG15 仍保持零回包。正式主程序继续禁用云台。
- 开始收束 App 作为主机唯一交互渠道的功能契约。用户选择后台/锁屏提醒采用系统通知与振动、前台采用弹窗/闪屏，并明确前台始终防自动锁屏；可选黑屏式屏保按设置中的空闲时间进入纯黑页面，但设备、会话和蓝牙保持唤醒。
- 用户明确允许只读审查 `C:\Users\Aze\Desktop\APP\EdgeFocusApp`，为避免把整个 App 仓库灌入主上下文，已派出 `app_source_audit` 子 agent，限定检查屏保/提醒/PRECHECK/双页面/手环搜索重连/BLE 契约，不修改文件。
- 2026-07-16：已确认正式会话 BLE 中断恢复策略：不中断板端会话，5 秒后手机事实过期，关键事件进入可重放队列；重连后重新协商并恢复当前会话快照。
- 2026-07-16：已将 App 审查拆分为两个独立只读子 Agent：一个交付功能实现审查，一个交付架构概览与代码索引，避免架构索引被旧任务说明覆盖。
- 2026-07-16：功能审查与独立架构索引均已返回；未修改或构建 App。两份报告关于 `TimerStateHolder` scope 出现冲突，经主 Agent 最小源码复核确认当前类在第 90 行已有 `@Singleton`，已撤销错误风险项。有效缺口为事件 ACK/去重、屏保/防锁屏、PRECHECK 双页面不等价、后台提醒投递和手环主机代理控制。
- 2026-07-16：用户批准 App 架构方向 A，但要求先对齐 App 与主机交互；已新增 `host_app_timing_audit` 只读 Agent，专门交付无会话、连接恢复、PRECHECK、RUNNING、PAUSED、ENDING/FINISHED 的双端时序和消息矩阵。
- 2026-07-16：最终对齐双模式边界：本地模式由 App 运行并生成结果、主机只做权威存储；主机模式由主机运行、App 做交互展示。两类记录统一归档主机，App 仅缓存。本轮已覆盖此前两次错误推断，并要求时序 Agent 分开审查两条链的 ID、幂等、断连补交和保存竞态。
- 2026-07-16：双端时序审查已交付。确认本地模式主机端幂等存储入口存在，但 App 缺少断连待同步队列；主机模式还存在可靠推送未协商、手机心跳过期、PRECHECK/PAUSED 重连缺口、断连误完成和自动结束双状态机等 P0。尚未修改 App 或主机实现。
- 2026-07-16：用户确认本地模式在主机未连接时可完整运行并缓存结果，连接后自动上传；主机已连接时仍可自由选择本地或主机模式。当前实现属于原设计的未闭环版本，不应重定义或删除本地模式。
- 2026-07-16：已最小核验 App 当前仅在 IDLE 切换 `SessionMode`，本地会话中途重连不会自动迁移成主机模式；该项现状符合产品设计，后续只需补回归测试和历史待同步上传，不改其基本语义。
- 2026-07-16：确认模式由设置页现有“使用 EdgeFocus 设备”开关选择，撤回每次会话弹出模式选择器的建议；进一步核验首页在选择主机但未连接时已禁用开始按钮并显示两处文字提示，现状符合产品设计，也撤回自动降级弹窗建议。
- 2026-07-16：用户授权主 Agent 系统阅读 App 全部受控源码，替代基于粗粒度 Agent 概览的逐点猜测。采用文件化审查，排除 build/生成物，只读覆盖构建、数据、BLE、会话、服务、UI 与测试；完成事实表前不继续提出源码可回答的问题。
- 2026-07-16：完成 App 全量阅读盘点：103 个受控相关文件、约 10.9k 行；确认 App 工作树已有多处用户改动，后续只读且不碰 build/生成物。开始按构建/数据/BLE/会话/服务/UI/测试分组阅读。
- 2026-07-16：完成构建入口、Manifest、Application/Activity、DI、Room、repositories、领域模型和小型策略/util 全文阅读。确认本地会话未同步查询/标记 API 已存在，可靠补传缺口位于编排层；同时确认屏保设置、防锁屏、主机可靠事件协议仍不在基础层。
- 2026-07-16：完成 `BleManager`、`BleChunkAssembler`、`BoardJsonParser` 全文阅读（含先前工具输出截断后补读的 420–735 行）。确认 transport/业务 facade 过度集中、可靠协商/ACK/心跳 envelope 缺失、无重连事务，并发现板载输出与电量占位默认仍残留。
- 2026-07-16：完成 `TimerStateHolder` 1–820 行全文阅读。确认本地模式和会话期模式冻结已实现；缺口集中在同步结果闭环、主机模式断连误完成、状态初始化污染、提醒覆盖槽以及横屏强制主机模式等编排问题。
- 2026-07-16：完成 `TimerStateHolder` 821–1209、`BleForegroundService` 与 `AppMonitorService` 全文阅读。确认主机状态 reducer 重复、断连插值越权、BLE FGS半成品、phone heartbeat缺失，并定位屏保/锁屏被误判为强手机干扰与双重提醒风险。
- 2026-07-16：完成 `HomeViewModel`、`ControlPanelViewModel`、导航、`BleViewModel`、`DistractionAppsViewModel`、`TestViewModel` 全文阅读；发现页面层重复拥有会话保存与监控生命周期、Home 的 PRECHECK 身份缺口、ViewModel 重建身份风险，以及本地结果仅结束瞬时上传且未闭环标记同步。`DashboardViewModel` 和 `SettingsViewModel` 因单次输出截断，列入下一批单独补读。
- 2026-07-16：已单独补读完整 `DashboardViewModel` 与 `SettingsViewModel`。确认主机历史确实合并进统一 Room，但同步与反馈重试被绑定在 Dashboard 页面生命周期；Settings 数据链目前还没有屏保开关和黑屏等待时间。
- 2026-07-16：完成竖屏首页、计时组件、底部导航、反馈组件、Dashboard 与 Settings 页面全文阅读。确认未连接主机时禁用开始的既有设计正确；定位板端电量 67% 占位、首页蓝牙空按钮、竖屏 PRECHECK 缺失、手环代理入口缺失及两套反馈枚举不兼容。
- 2026-07-16：完成 BLE 连接页、分心应用页、Debug 测试页、横屏配置页与计时拨盘全文阅读。确认主机连接页承诺自动重连但底层未闭环；应用分类文案仍是旧的“学习不提醒”；横屏仍保留板端 NORMAL/SILENT 与离座自动暂停旧配置。
- 2026-07-16：完成 963 行 `ControlPanelScreen` 全文阅读。确认横屏已有 PRECHECK/云台控制骨架，但相机预览只显示元数据不显示图片；前台主机提醒只在横屏消费；统计和设置仍残留困倦、手机端等级、虚假电量及板载屏幕/声音旧语义。
- 2026-07-16：重新以 `git ls-files` 对照全仓清单核账；主源码剩余主题/资源，随后进入全部单元测试、迁移测试和仓内旧设计文档复核，避免只读实现却漏掉原设计意图与既有回归契约。
- 2026-07-16：完成主题、字体和基础 values/通知图标资源阅读；主源码 UI 部分已闭环。开始逐个核对 18 个 JVM 测试与 1 个 Room 迁移测试，重点确认现有测试是在保护正确语义还是固化旧行为。
- 2026-07-16：完成全部 JVM/Room 迁移测试全文阅读。确认基础 BLE、JSON 和迁移测试可复用，但黑屏空前台包名、手机端等级、PRECHECK 直接跳过、67% 电量和板端屏幕/声音等旧契约被测试明确固化；核心状态机、重连、outbox 和双页面一致性完全缺少集成测试。
- 2026-07-16：完成 App 仓内 `_workspace` 设计/计划文档全文阅读。确认双模式、主机权威存储、PRECHECK、MAC 绑定和三档反馈确为旧版既定方向；同时标记开发板屏幕、板端声音/LED/振动、`phone_sync` 整场上传等内容已过期。
- 2026-07-16：复核 App 工作树仍有 6 个 Kotlin 主文件、解析测试与图标资源的既有未提交改动，本轮未触碰。首次执行 JVM 测试因当前 shell 没有 `JAVA_HOME` 立即失败；已定位 Android Studio 自带 JBR，下一次仅为本命令临时设置环境后重跑。
- 2026-07-16：Android Studio 当前 JBR 执行 Gradle 时在 `androidJdkImage/jlink` 失败，改用本机既有 JBR 17.0.14 后 `testDebugUnitTest` 成功（退出码 0，62.2 秒）。仅有 SDK XML 版本工具链警告；未修改 App 源码或系统环境。
- 2026-07-16：完成 App 设计与对齐文档：`docs/architecture/app-target-architecture.md`（380 行）和 `docs/integration/app-host-alignment-v3.md`（538 行），并在 App 仓新增实施交接入口。文档包含必须新增功能、保留/修改/删除矩阵、当前主机能力对照、线协议示例、手环主机扩展、实施完成定义和联调顺序；未修改 App 代码。
- 2026-07-16：启动展示海报架构素材任务；用户接受浏览器预览，同时要求 Mermaid 与文档。已读取 brainstorming/visual companion/planning 规则并盘点现有架构、硬件、集成文档，尚未生成最终图稿，先进入展示主线选择。
- 2026-07-16：全文复核当前主程序架构和 M33 风扇实测/运维文档，确认海报可用的真实数据、运行域、控制周期和故障回退；记录云台最新成功驱动事实与旧文档状态差异，后续母版按新状态表达但不夸大会话内跟踪完成度。
- 2026-07-16：复核云台/供电/异构控制完整规格及当前 gimbal 配置。确认可展示已完成硬件驱动链和安全架构，但机械参数仍为空，故 PRECHECK 自动搜索/持续跟踪不能标成已完成；完成展示主线问题所需上下文。
- 2026-07-16：按用户最新口径撤销云台过渡状态表达，浏览器预览已启动于 `http://localhost:51300`，展示 A（4 图）、B（9 图）、C（1+12 图）三种架构图谱密度及推荐专题清单，等待用户批准后写完整母文档和 Mermaid 图。
- 2026-07-16：用户批准 A/B/C 全部交付。规格 `docs/superpowers/specs/2026-07-16-edgefocus-architecture-atlas-design.md` 已完成并以 commit `df7d4cc` 单独提交；当前正在执行独立规格审查，最终图文尚未生成。
# 2026-07-16 EdgeFocus 完整架构图谱交付

- 用户批准同时交付 A（4 张核心图）、B（9 张工程图）和 C（C0 母图 + 12 张专题图）。
- 完成 `docs/architecture/edgefocus-complete-architecture.md`，覆盖产品、会话、AI、App/手环、云台、M33 风扇、供电、Demo、故障与部署。
- 完成 `docs/architecture/poster-diagrams/` 下的 README、A/B/C Mermaid 图源和海报文案库。
- 同步修正 `current-architecture.md` 的旧云台阶段表述，云台按 PRECHECK 自动构图和会话内跟踪的正式能力呈现。
- 用户补充 STM32MP257 是赛道指定型号，TOPS 不构成项目亮点；A/B/C 主图和浏览器指标卡已撤下 TOPS，只在母文档技术附注保留 `1.2–1.35 TOPS`。
- 浏览器预览：`http://localhost:51300`。
- 自动校验：A Mermaid=4、B=9、C=13（C11 为 Markdown 矩阵且 C07 含两张 Mermaid）、母文档=2；编号、围栏、相对链接、主图 TOPS 排除、预览更新和 `git diff --check` 均通过。
- 首次缓存区检查提示 `B-engineering-diagrams.md` 文件末尾多一空白行；PowerShell 未自动中止后提交已产生。随后删除空白行、显式检查 `$LASTEXITCODE` 并修正提交。

# 2026-07-16 云台 UART4 恢复与双后端

- 已读取仓库 `AGENTS.md` 和现有 `platform/gimbal` 概览；现有层已具备协议解析、状态机、PD6/PD7 门禁、恢复检测和独立 bring-up 测试。
- 当前 `config/gimbal-board.json` 仍为 `motion_verified=false` 且所有机械参数为空，不能直接开放主程序运动。
- 首次并行读取 G431 工程的 README/docs/Core 文件清单时其中一条命令退出 1，只返回了现有 Linux 侧检索结果；改为逐项存在性检查与限定路径读取。
- G431 资料库 git 查询首次因 sandbox 用户触发 dubious ownership；随后使用命令级 `-c safe.directory=...` 只读查询，未修改全局 git 配置。
- SSH 在沙箱内返回 Permission denied；按规则申请沙箱外只读 SSH 后仍真实超时。改用用户提供的 COM3 Linux 控制台，115200 8N1 成功进入 `root@ATK-DLMP257`。
- COM3 只读确认 UART4 DTB 已生效：`/dev/ttySTM3`、`serial3` alias 和 `status=okay` 均正确；正式服务仍禁用云台且串口无 owner。
- COM3 完成首轮无运动协议探测，六条查询/安全命令全部有正确回包，最终功率、Motor、Hold 与 Fault 均为 0。
- Windows `Get-CimInstance Win32_SerialPort` 被权限拒绝，但 `mode COM3` 和 .NET `SerialPort` 可用；后续固定使用 .NET 串口，不再重复 CIM 查询。
- 2026-07-16：用户批准安全环境中的云台运动测试；板端依次通过 `ARM → HOLD → PITCH/YAW → TRACK → STOP`，协议应答、实际动作和最终断力状态均符合 G431 协议 2.0。
- 2026-07-16：补做停止态 UART 误码增量检查；3 秒内 `UARTERR 6→7`，但无收发丢弃且命令完整，已将其定为需趋势监控的退化指标，而非单值硬故障。
- 2026-07-16：完成云台协议 2.0、UART-only/显式 GPIO 双模式、raw `!` 急停、精确应答、STOP 状态复核、500 ms 健康查询与三次无响应 Fault 门；默认设备改为 `/dev/ttySTM3`。
- 2026-07-16：完成 ARM 参考位环形相对安全窗口、PRECHECK 相对 waypoint 与正式会话视觉跟踪控制器；生产配置继续关闭运动，独立 bring-up 配置限制为 yaw ±15°、pitch ±10°。
- 2026-07-16：本机 `focus_app`/`gimbal_bringup` 编译通过，云台专项 6/6、完整 CTest 30/30 通过，`git diff --check` 无新增格式错误。
- 2026-07-16：准备上传源码到既有 ARM 编译机时被安全审查拦截；等待用户明确批准向 `192.168.10.6` 传输本项目源码。板端主程序和正式服务均未变更。

# 2026-07-22 云台全周人工控制与受限自动控制

- 完成现有 G431/主机角度链、人工 nudge 和未提交临时恢复带盘点。
- 用户确认两轴全周机械能力、AUTO 仅主机限 ±70°、范围外向内小步、人工向外跨界单次停靠。
- 用户确认普通/微调步进由主机配置为 30°/10°，APP/网页每条命令只携带 `fine_tune`，可任意交替。
- 规格写入 `docs/superpowers/specs/2026-07-22-gimbal-full-range-manual-bounded-auto-design.md`。
- 三轮独立规格审查修复物理停靠门槛、UART 不确定结果、双轴事务、环形角度、NVM ABI 和板端验收参数后 Approved。
- 用户已批准最终规格，进入文件化实施计划；writing-plans 技能不可用，按说明使用 planning-with-files 替代。
- 当前 G431 与主机仍各自保留上一轮未提交临时恢复带改动，尚未进入最终实现或提交。
- 当前板端运行的是诊断固件 `d32d4c2`；主程序服务此前保持停用，任何运动前需重新只读确认。
- 2026-07-22：完成主机纯环形角度/AUTO 策略、全周 ACK-only 人工入口、边界 `PENDING/ARMED` 门、30°/10°双步进和 `OUTCOME_UNKNOWN` 停机语义。
- 2026-07-22：App 与网页均改为逐次 `fine_tune`；网页增加四个独立微调按钮，字段说明写入 `docs/integration/gimbal-manual-boundary-app-fields.md`。
- 2026-07-22：G431 Production 接受完整单圈、Maintenance 保持 ±10°，`VER` 新增 `FULL_RANGE_POSITION`；旧 NVM V3 结构未修改。
- 2026-07-22：主机启用 Demo HTTP 的完整构建与 CTest 32/32、G431 host-core 1/1 和 STM32 Release 构建全部通过；BIN/HEX 已由新 ELF 重新生成并记录 SHA-256。
- 2026-07-22：已生成待批准的 `config/gimbal-full-range-acceptance-v1.json`，`board_motion_allowed=false`；尚未刷写、部署或执行板端运动。
- 2026-07-22：用户明确批准板端验收配置；提交 `7fc36a4` 将 `approval_state` 改为 `approved`、`board_motion_allowed=true`。
- 2026-07-22：使用 `hu@192.168.10.6` 完成 ARM 全量构建，发布包 SHA-256 为 `EF14A0E7ED5CA03492D2D705034F3DFC723E8A06523E95E9DC2172C2E31B5E64`。
- 2026-07-22：发布 `edgefocus-fullrange-7fc36a4-20260722-130700` 已原子部署至 `192.168.10.173`；安装二进制与 release 哈希一致，HTTP 状态、BLE GATT 注册和广播均正常。
- 2026-07-22：部署前无运动 UART 探测五项均超时，因此未把当前 G431 视为新固件；尚未执行云台运动，等待刷入 `3014b53` 对应 HEX 后继续能力协商与分阶段验收。
- 2026-07-22：板端 RTC 仍显示 2024-02-28，解包产生 future timestamp 警告；不影响哈希与服务健康，但时间保持问题仍需单独修复。

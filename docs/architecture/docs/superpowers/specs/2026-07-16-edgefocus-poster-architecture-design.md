# EdgeFocus 海报架构素材融合设计

## 1. 目标

为一张约 `80 × 200 cm` 的竖版 EdgeFocus 海报准备一组可独立排版、可继续在 Illustrator 中编辑的 Mermaid 架构素材。设计必须同时利用根目录中的系统/App 架构文档和 `已有素材` 中的三张实物图，避免海报成为纯框图集合。

最终权威产出为 `5` 个 `.mmd` 文件：`4` 张主架构图和 `1` 条会话生命周期状态带。实施阶段可以生成 SVG/PNG 作为渲染检查和后续 Illustrator 排版入口，但它们不是新的事实源；修改图内容时始终回到 `.mmd`。此阶段不制作完整海报成稿，也不改变原始图片。

## 2. 已批准的版面方向

海报采用竖向叙事与局部双栏相结合的结构：

1. 顶部用设备成品图作为 Hero 主视觉，并配标题与一句话定位。
2. Hero 下方放一张全宽系统闭环总览图。
3. 用一条横向会话生命周期状态带承接系统总览与技术细节。
4. 中部双栏分别放 Edge AI/Fusion 与 App/可靠交互两张主图。
5. 下部把设备拆解图与异构计算/安全闭环主图并排展示。
6. 底部用 PCB 图作为横向硬件细节，并放量化指标与完整图谱二维码。

该结构优先保证 1–2 米浏览距离下的主题识别，以及近距离阅读时的技术完整性。

## 3. 原始素材安排

### 3.1 实物图片

| 原始文件 | 海报角色 | 使用方式 |
|---|---|---|
| `已有素材/设备成品图.jpg` | 顶部 Hero 主视觉 | 保留设备完整轮廓；后续排版阶段可抠图、提亮和更换简洁背景，但不覆盖原件 |
| `已有素材/设备拆解图.jpg` | 内部集成实证 | 与异构安全图并排；后续可加相机、主板、驱动、风扇等少量标注线 |
| `已有素材/pcb图.jpg` | 硬件平台细节 | 作为底部横向图；最多标注 3–5 个关键器件，避免变成器件清单 |

### 3.2 架构文档

| 原始文件 | 作用 |
|---|---|
| `edgefocus-complete-architecture.md` | 系统边界、统一术语、职责、约束和量化口径的事实源 |
| `A-core-diagrams.md` | 四张主图的主要海报化底稿 |
| `B-engineering-diagrams.md` | 会话状态、云台、风扇、供电和可靠性的工程细节来源 |
| `C-complete-diagram-atlas.md` | 补足产品交互、AI、跨设备协议、四计算域和故障降级 |
| `edgefocus-app-v2-architecture.md` | App 分层、协调器、BLE、Room 和可靠事件的事实源 |
| `edgefocus-app-overview.mmd` | App 主图的简化骨架 |

B/C 套中未进入五张海报图的内容继续保留为扫码后的完整技术图谱和答辩资料，不删除、不改名。

## 4. 输出文件与职责

正式文件写入新目录 `poster-mmd/`，命名与职责如下。

### 4.1 `01-edgefocus-system-loop.mmd`

用途：回答“EdgeFocus 是什么、用户如何形成闭环”。

- 方向：`flowchart LR`，适合全宽横向区域。
- 核心节点：使用者、摄像头、手机 App、STM32MP257 边缘主机、手环、展示页、外部供电、云台、风扇。
- 主机内部仅保留 A35/Linux、NPU、M33 三个计算域；G431 在主机外单列。
- 主路径必须突出 `Camera → NPU → A35/Fusion → App/Band/Web`。
- 不展开 SQLite、Demo 模式、具体 GPIO 或发布链。
- 来源：A1、C00/C01、完整母文档第 3–4 节。

### 4.2 `02-session-lifecycle-strip.mmd`

用途：作为一条窄状态带解释正式会话边界。

- 类型：`stateDiagram-v2`。
- 状态：`IDLE → PRECHECK → FOCUSING ↔ PAUSED → ENDING → IDLE`。
- 只保留关键转移标签；用一条简短 note 强调 PRECHECK 不计时、不入库、不提醒。
- 不嵌套 PRECHECK 子状态，不展示 App 的 FINISHED 页面态。
- 来源：B2、C03、完整母文档第 6–7 节。

### 4.3 `03-edge-ai-fusion.mmd`

用途：回答“系统如何从可验证证据得到 Level 0–3”。

- 方向：`flowchart TB` 或紧凑 `LR`，以双栏半宽区域的实际可读性决定；优先控制宽度。
- 视觉支路：CameraFrame、质量/新鲜度门、YOLO11n Pose、17 keypoints、主体连续性、58-d 特征、2 s/16 steps、Temporal Prone MLP、Presence。
- 手机支路：PhoneFact 四分类、2 s 心跳、5 s TTL。
- 汇合：FusionCore、episode、applicability、Level 0 与 Level 1–3、reasons/30 s 证据时间线。
- 必须明确视觉不判断手机类别，NOT_PRESENT/UNKNOWN 不产生分神提醒。
- 来源：A2、C04、完整母文档第 8–9 节。

### 4.4 `04-app-reliable-interaction.mmd`

用途：在单张图内解释 App 分层和跨设备可靠交互。

- 方向：`flowchart TB`，适合双栏半宽区域。
- 四层骨架：Presentation、Application Coordination、Data/Protocol、Android Platform/Host。
- Presentation 只保留竖屏主界面、横屏控制面板、提醒覆盖层。
- Application 只保留 Session、Connection、Alert、Precheck、PhoneFact 五类协调职责。
- Data 只保留 Codec、优先级调度、BLE、Room/Inbox/Outbox。
- 在图右侧或底部加入可靠交互短链：`hello/get_state → command_id/event_id → ACK → outbox/reconnect`。
- 不展示完整页面树、Room 字段 ER 图或全部类名。
- 来源：`edgefocus-app-overview.mmd`、App 架构第 1/3/6/7/10 节、A4、C05。

### 4.5 `05-heterogeneous-safety.mmd`

用途：回答“为什么采用四个计算域，以及故障如何局部隔离”。

- 方向：`flowchart LR`，与设备拆解图并排时仍保持清晰。
- 四域：A35/Linux、NPU、Cortex-M33、STM32G431。
- 设备：摄像头、风扇、双轴云台、STUSB4500 外部供电。
- 关键通信：A35↔M33 RPMsg、A35↔G431 UART4、PD7 Enable、PD6 Fault。
- 安全结论：M33 3 s 失联进入风扇安全高转；G431 Fault 本地停机且固定相机继续；External Power UNKNOWN 采用 Go，但安全环不降频。
- 只表达策略级事实，不展开完整启动状态机、引脚所有权切换或所有故障矩阵行。
- 来源：A3、C06、B5/B6/B7、完整母文档第 12–14/18 节。

## 5. 统一视觉语言

五张图使用一致的 Mermaid class：

| 类型 | 建议颜色 | 含义 |
|---|---|---|
| external | 浅蓝 | 用户、App、手环、浏览器等外部交互 |
| host | 青绿色 | A35、会话、策略、协议与存储 |
| ai | 浅紫 | NPU、Pose、Temporal、Fusion |
| realtime | 浅橙 | M33、G431 和确定性控制 |
| device | 浅绿 | 摄像头、风扇、云台、供电器件 |
| fault | 浅红 | 安全动作、故障与降级结论 |

通用规则：

- 中文为主，保留必要英文术语与量化数字。
- 单节点不超过三行；每行尽量不超过 16 个中文字符。
- 4 张主图分别控制在约 12–18 个可见节点；状态带不超过 6 个状态。
- 箭头只表示真实数据流、控制流或状态转移，不用装饰性连线。
- 统一使用 `classDef`，避免 Mermaid 主题差异破坏颜色。
- 不在图内写完整段落；解释性文字放在海报图注中。
- 所有输出保持纯 `.mmd`，不嵌入 Markdown 代码围栏。

目标版面尺寸用于可读性复核：系统闭环图和异构安全图按约 `70–72 cm` 宽、`18–26 cm` 高检查；AI 与 App 两张半宽图按约 `33–35 cm` 宽、`22–32 cm` 高检查；状态带按约 `70–72 cm` 宽、`8–10 cm` 高检查。最终高度可随实际节点布局微调，但不得依赖小于约 `22 pt` 的印刷字号才能容纳内容。

## 6. 信息边界与取舍

主海报不直接展开以下内容：完整软件模块树、Room ER 图、Demo 三模式细节、构建部署泳道、完整故障降级矩阵、云台完整状态机、M33/Linux fallback 的全部交接步骤。这些内容仍保留在 B/C 套和母文档中，可通过二维码或答辩材料访问。

芯片 TOPS 不作为海报亮点。允许展示的核心量化口径为：`4 个计算域`、`17 keypoints`、`58-d × 16 steps`、`2 s / 5 s`、`100 ms`、`1 Hz / 3 s`、`3 × 250 ms`、`Home ≈ 4 FPS / Go ≈ 3 FPS` 和 `30 s` 证据时间线。

## 7. 数据流与版面阅读顺序

观众首先通过成品图和标题理解产品形态；随后从系统闭环图看到输入、边缘主机和提醒输出。会话状态带定义正式运行边界。中部两张图并列解释“如何判断”和“如何交互”。下部拆解图与异构安全图把软件职责映射到真实硬件。PCB、指标和二维码作为技术可信度收尾。

五张 Mermaid 图彼此独立，不使用跨文件连线；统一术语使它们组合后形成同一故事。

## 8. 验证与异常处理

实施时依次完成以下验证：

1. 语法验证：用可用的 Mermaid CLI 或等效渲染器逐个解析；任何 parse error 必须修复。
2. 内容核对：逐项对照母文档，检查计算域、通信方向、TTL、等级语义和 PRECHECK 边界。
3. 可读性验证：渲染 SVG/PNG，在模拟半宽与全宽海报区域中检查字体、换行、边交叉和节点拥挤。
4. 一致性验证：检查五张图的颜色、术语、大小写和 `Level 0–3` 表述一致。
5. 失败处理：若单图在目标宽度下不可读，优先删减次要节点或把说明移到图注；不通过缩小字体强行容纳。
6. 渲染器兼容：如果目标 Mermaid 版本不支持某种标签或布局语法，使用更基础的 flowchart/stateDiagram 写法，保持事实语义不变。

## 9. 验收标准

- `poster-mmd/` 中存在上述五个文件，均能成功渲染。
- 渲染测试使用第 5 节定义的全宽、半宽和状态带目标尺寸；SVG/PNG 可作为 QA 或排版中间产物，但 `.mmd` 为权威源文件。
- 原始文档和 `已有素材` 三张图片未被覆盖或删除。
- 四张主图在各自目标版面中不依赖放大即可辨认主要节点。
- 每张图只回答一个核心问题，并能单独用作海报配图。
- App、主机、AI、M33、G431 的职责和通信方向与母文档一致。
- PRECHECK、Level 0–3、PhoneFact 来源、TTL 与故障降级表述无歧义。
- 海报组合同时呈现真实产品、核心算法、App 交互和异构安全，而不是单纯复刻完整工程图谱。

## 10. 非目标

- 不在本阶段制作最终 80 × 200 cm Illustrator/Photoshop 海报文件。
- 不修改、抠图或调色原始 JPG。
- 不新增未经母文档确认的功能、性能数据或营销指标。
- 不把 B/C 套的每张工程图都复制为新 `.mmd`。

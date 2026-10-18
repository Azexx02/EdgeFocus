# 分神识别核心设计规格

- 日期：2026-07-13
- 状态：已获产品方向批准，待书面复核
- 展示期限：4 天完成并冻结， 第 5 天仅用于赴场与现场准备
- 目标平台：先在 Windows 主机验证，后续迁移到算力低于 2 TOPS 的边缘设备

## 1. 背景与目标

现有 `focus_app` 的视觉和行为判定包含单位错误、不可达状态、按帧而非按时间累计、模型静默降级、视觉与手机风险相互污染等问题。继续在原模块上调整阈值无法建立可信的稳定性基线。

本设计建立独立的 `distraction_core` 子项目，先在本机完成：

1. 从视频或摄像头提取可观察的视觉证据；
2. 将人物存在、严格趴下、头部偏离和可选视线偏离转换为带质量和时效的标准证据；
3. 通过时序融合输出可解释的 0～3 等级，其中 1～3 为三级分神严重度；
4. 使用离线回放和事件级指标验证稳定性；
5. 在结果稳定后，才为 STM32MP257 一类边缘设备选择、量化和移植模型。

产品不宣称直接识别“思考、沉思、发呆”等内部认知状态，只输出基于姿态、脸部、视线、手机上下文和持续时间的“可观察分神风险”。相关面部视频研究在真实环境中仍受到光照、脸部/眼部可见性和头部运动影响，不能作为直接读出内部状态的依据。[PAFE, CVPRW 2022](https://openaccess.thecvf.com/content/CVPR2022W/CVPM/papers/Lee_Predicting_Mind-Wandering_With_Facial_Videos_in_Online_Lectures_CVPRW_2022_paper.pdf)

## 2. 明确范围

### 2.1 本期包含

- 主机 Python 工程；
- 视频文件与摄像头输入；
- 相机方向和来源域元数据；
- 单用户/固定座位 ROI 与主目标锁定；
- 人物存在检测；
- Pose 关键点提取；
- 基于 1～3 秒关键点序列的可训练轻量状态分类器；
- 独立脸部/landmark/head-pose 专家；
- 按质量触发的可选趴下外观专家接口；
- 时间戳驱动的证据融合、等级演化和 episode；
- 手动手机状态模拟器；
- 简单视频/episode 标注、离线回放、指标统计和黄金轨迹测试；
- 机器可读 JSONL 输出与面向调试的叠加显示。

### 2.2 本期不包含

- 手机端状态采集、应用识别、BLE 或网络通信；
- 原产品 UI、提醒文案、冷却时间或设备执行；
- 直接在旧 `focus_app` 中替换模块；
- 第五天现场前的完整板端移植；
- 困倦、闭眼疲劳、建议休息功能；
- 对“沉思/发呆”的直接分类；
- 复杂标注平台或大规模训练集建设。

## 3. 产品语义

### 3.1 应用状态与等级分离

`applicability` 决定当前能否讨论分神等级：

- `ACTIVE`：主用户在位且证据足以参与融合；
- `NOT_PRESENT`：主用户持续不在位；它不是分神，不产生提醒；
- `UNKNOWN`：输入、画质、模型或跟踪证据不足。

`distraction_level` 仅在 `ACTIVE` 时有意义：

- `0`：没有达到分神确认条件；
- `1`：轻度分神；
- `2`：中度分神；
- `3`：重度分神。

当 `applicability` 不是 `ACTIVE` 时，等级输出 `null`，防止上层把未知或离座误读为专注。

进入 `UNKNOWN` 时，对外等级立即变为 `null`。为了支持界面连续性和问题诊断，输出中另设 `continuity.last_confirmed_level`、`last_confirmed_at_ms` 和随时间衰减的 `historical_score`；它们不得被当作当前等级。内部已打开的 episode 最多允许跨越 1,000 ms 的无证据空洞，空洞本身不累计持续时间；超时后以 `EVIDENCE_GAP` 结束 episode。

### 3.2 视觉业务状态

- `PRESENT/NOT_PRESENT`：主用户是否在固定学习区域；
- `PRONE`：头部或上半身接近/贴在桌面并持续存在；正常写字、阅读和低头使用手机必须作为困难负样本；
- `HEAD_AWAY`：头部持续偏离会话开始时标定的正常学习朝向。第一版只用相对中立 yaw 负责左右转头，低头 pitch 不直接触发该状态；左右方向与角度只用于诊断；
- `GAZE_AWAY`：仅当眼部 ROI 和 landmark 质量足够时提供的可选弱证据。

删除旧语义：`sleepy`、`eyes_closed_fatigue`、`rest_suggestion`、由错误 pitch 推导的伪 `posture`、伪视觉 `phone`。

### 3.3 手机模拟输入

本机版本提供手动状态选择，不实现真实手机采集：

- 未使用（序列化为 `active=false, category=null`）；
- `LEARNING`；
- `ENTERTAINMENT`；
- `GENERAL_OR_UNKNOWN`。

模拟器还可以设置事件时间戳、置信度和新鲜度，并支持在视频回放过程中切换状态。

手机证据规则：学习类只降低手机分支的分神可能性；娱乐类为强手机证据；普通或未知为中等手机证据。任何手机类别都不得覆盖独立的 `PRONE` 或 `HEAD_AWAY` 视觉证据。

## 4. 总体架构

```text
Video/Camera -> FramePacket -> Subject Detector/Tracker
                                  |-> Pose Adapter -> Keypoint Sequence Classifier
                                  |-> Face/Landmark -> Head/Gaze Evidence
                                  |-> Optional Appearance-Prone Expert

Manual Phone Simulator -----------------------------------|
All timestamped evidence -> Temporal Fusion -> Assessment -> JSONL/Overlay/Metrics
```

推荐在 `C:\Users\Aze\Desktop\self_developed\code\distraction_core` 建立独立工程：

```text
distraction_core/
  src/distraction_core/
    capture/
    adapters/
    features/
    fusion/
    contracts/
    evaluation/
  tools/
  configs/
  tests/golden/
  data/                 # 默认不纳入版本控制
  outputs/              # 默认不纳入版本控制
```

核心层不导入旧项目的相机、BLE、UI、策略或 NPU SDK。后续通过单独 adapter 连接旧项目。

每个固定摄像头配置必须包含规范化后的 `seat_roi`、`desk_polygon` 或桌沿线、`orientation_deg`、`mirrored`、正常学习朝向标定值和 `calibration_version`。坐标均定义在完成旋转与镜像后的 `[0,1] × [0,1]` 画面中。缺少桌面几何时 `PRONE` 专家无效；缺少中立头姿时 `HEAD_AWAY` 专家无效，不使用隐含默认值。

## 5. 模型与调度设计

### 5.1 人物与主目标

人物检测器负责候选框；跟踪器依据固定座位 ROI、位置连续性和短时外观连续性锁定主用户，忽略路人。短暂丢失转换为 `UNKNOWN`，持续丢失才转换为 `NOT_PRESENT`。

第一版基线采用 1,500 ms 的持续可信缺席确认 `NOT_PRESENT`。阈值写入配置，不能按帧数实现。

### 5.2 Pose 路径

Pose 模型只负责输出关键点、visibility、bbox 和时间戳，不以手写鼻肩比例直接决定业务状态。

归一化后的关键点、可见性、速度、相对桌面位置和缺失掩码组成 1～3 秒序列，输入轻量 MLP/TCN 分类器，输出：

- `NORMAL_STUDY`；
- `PRONE`；
- `AMBIGUOUS`。

训练和验证必须覆盖写字、阅读、轻微低头、低头看手机、逐渐趴下、保持趴下、起身恢复和遮挡。

### 5.3 脸部与头部路径

脸部检测、landmark 与 head-pose 专家独立输出 `HEAD_AWAY` 连续证据。视线仅在双眼区域分辨率和 landmark 质量满足门槛时启用；否则输出无效证据，不猜测视线。

每次会话要求用户以正常学习姿态看向学习区域 3 秒，取高质量窗口的中位 yaw 作为中立朝向。第一版 baseline：相对中立 yaw 绝对值达到 25° 并持续 1,500 ms 后进入 `HEAD_AWAY`；达到 40° 时可在 800 ms 后进入；回到 15° 以内并持续 1,000 ms 后退出。脸部不可见不等于转头，只会使该专家变为无效。上述值均为显式可调 baseline，必须用冻结验收集确认后才可称为标定值。

主机可以使用 MediaPipe Face Landmarker 作为参考 extractor。其视频/直播模式依赖时间戳，直播模式可能在忙时丢帧，因此融合层必须以 freshness 和实际时间间隔工作，而不能假设逐帧结果。[MediaPipe Face Landmarker](https://ai.google.dev/edge/api/mediapipe/python/mp/tasks/vision/FaceLandmarker)

### 5.4 可选趴下外观专家

当关键点因贴桌或遮挡大量消失、关键点分类器输出 `AMBIGUOUS` 时，可按需调用主用户/桌面 ROI 的轻量图像分类器。它不是首日关键路径，也不全时并行运行。

### 5.5 边缘部署约束

后续板端采用 INT8 优先和分时调度：人物/Pose 低频全帧运行，脸部模型只在主用户 ROI 且脸部可见时运行，可选外观专家仅由不确定性触发。STM32MP257 官方规格给出的 NPU 理论性能约为 1.2 TOPS INT8（800 MHz），最终容量必须以板上延迟、DDR 和 CPU 占用实测为准。[STM32MP257 datasheet](https://www.st.com/resource/en/datasheet/stm32mp257d.pdf)

ST 官方模型工具链提供 Face Detection、Face Landmarks、Pose Estimation 以及训练、量化、评估和硬件 benchmark 入口，可作为后续移植候选，但主机参考模型不等于可直接部署的板端模型。[STM32 AI Model Zoo](https://github.com/STMicroelectronics/stm32ai-modelzoo)，[Model Zoo Services](https://github.com/STMicroelectronics/stm32ai-modelzoo-services)

## 6. 稳定数据契约

所有持久化对象包含 `schema_version="1.0"`、`run_id`、`subject_id`、`config_hash` 和相关 `model_version`。除明确标注为 nullable 的字段外均为必填。`timestamp_ms` 是单调非递减的媒体时间：离线视频以首个有效 PTS 为 0，实时流使用 monotonic clock；UTC 采集时间只作为可选元数据，绝不参与持续时间计算。

`value`、`confidence`、`quality` 范围均为 `[0,1]`：`value` 表示该业务命题成立的概率/分数，`confidence` 表示模型对该输出的置信程度，`quality` 表示输入是否足以支持该专家。证据在 `current_timestamp_ms - timestamp_ms <= fresh_for_ms` 时新鲜，超过后立即过期；过期区间不累计为证据。

### 6.1 FramePacket

```json
{
  "schema_version": "1.0",
  "run_id": "run-20260713-01",
  "subject_id": "demo-user",
  "config_hash": "sha256:...",
  "timestamp_ms": 123456,
  "sequence_id": 42,
  "source_id": "camera_0",
  "orientation_deg": 90,
  "mirrored": false,
  "source_domain": "device_camera_v1",
  "frame": "runtime-only"
}
```

`orientation_deg` 只允许 `0/90/180/270`，表示从原始帧到正立规范帧需要顺时针旋转的角度；随后再按 `mirrored` 做水平镜像。所有模型和标定坐标只接收规范帧。

### 6.2 VisionEvidence

```json
{
  "schema_version": "1.0",
  "run_id": "run-20260713-01",
  "subject_id": "demo-user",
  "config_hash": "sha256:...",
  "model_version": "head_pose_v1",
  "timestamp_ms": 123456,
  "source": "head_pose_v1",
  "kind": "HEAD_AWAY",
  "value": 0.78,
  "valid": true,
  "confidence": 0.86,
  "quality": 0.82,
  "fresh_for_ms": 300,
  "latency_ms": 18,
  "diagnostics": {"yaw_deg": 31.2, "pitch_deg": -4.0}
}
```

所有专家均通过该证据接口进入融合器；融合器不得读取某个模型私有 tensor 或硬编码其类别索引。

`VisionEvidence.kind` 的稳定枚举为 `PRESENCE`、`PRONE`、`HEAD_AWAY`、`GAZE_AWAY`；`diagnostics` 可扩展但不得参与跨版本业务逻辑。无有效值时仍输出对象，令 `valid=false`、`value=null` 并提供 `invalid_reason`。

### 6.3 PhoneObservation

```json
{
  "schema_version": "1.0",
  "run_id": "run-20260713-01",
  "subject_id": "demo-user",
  "config_hash": "sha256:...",
  "timestamp_ms": 123456,
  "category": "LEARNING",
  "active": true,
  "confidence": 1.0,
  "fresh_for_ms": 5000,
  "source": "manual_simulator"
}
```

`active=false` 时 `category` 必须为 `null`；`active=true` 时类别必须为 `LEARNING`、`ENTERTAINMENT` 或 `GENERAL_OR_UNKNOWN`。手机观察过期只移除手机分支，不改变视觉 applicability。

### 6.4 DistractionAssessment

```json
{
  "schema_version": "1.0",
  "run_id": "run-20260713-01",
  "subject_id": "demo-user",
  "config_hash": "sha256:...",
  "timestamp_ms": 123456,
  "applicability": "ACTIVE",
  "distraction_level": 2,
  "reasons": ["HEAD_AWAY", "PHONE_ENTERTAINMENT"],
  "distraction_episode": {
    "id": "episode-12",
    "status": "OPEN",
    "started_at_ms": 115256,
    "ended_at_ms": null,
    "duration_ms": 8200,
    "end_reason": null
  },
  "evidence_episodes": {
    "PRONE": null,
    "HEAD_AWAY": {
      "id": "head-away-7",
      "kind": "HEAD_AWAY",
      "status": "OPEN",
      "started_at_ms": 115256,
      "ended_at_ms": null,
      "active_duration_ms": 8200,
      "end_reason": null
    }
  },
  "presence_episode": null,
  "evidence": {
    "presence": 0.98,
    "prone": 0.08,
    "head_away": 0.78,
    "gaze_away": null,
    "phone": 0.85
  },
  "health": {
    "coverage": {"presence": true, "prone": true, "head_away": true, "gaze_away": false},
    "stale_sources": [],
    "model_latency_ms": {"pose": 25, "face": 18},
    "inference_mode": "learned"
  },
  "continuity": {"last_confirmed_level": 2, "last_confirmed_at_ms": 123456, "historical_score": 0.71}
}
```

`reasons` 的稳定业务枚举为 `PRONE`、`HEAD_AWAY`、`GAZE_AWAY`、`PHONE_LEARNING`、`PHONE_ENTERTAINMENT`、`PHONE_GENERAL`；证据不足原因放入 `health.invalid_reasons`，不伪装为分神原因。没有 episode 时对应字段为 `null`。episode `status` 为 `OPEN/CLOSED`；关闭后必须包含 `ended_at_ms` 和 `end_reason`。离位使用独立 `presence_episode`，其类型固定为 `NOT_PRESENT`，不得复用分神 episode。

`distraction_episode` 是等级 1～3 的总体业务区间；`evidence_episodes` 是用于解释和评测的类型化区间，固定键为 `PRONE`、`HEAD_AWAY`，未来可选加入 `GAZE_AWAY`。不同类型可以重叠，同时存在时仍只打开一个总体分神 episode。评测器必须直接读取 `evidence_episodes` 与 `presence_episode` 的关闭记录，禁止从 `reasons` 或逐帧分数重新推导 episode。

## 7. 时序融合和三级等级

融合器使用实际时间区间积分，不按帧计数。两次观测间隔超过该来源 freshness 时，中间空洞不得算作持续证据。

等级由三个因素共同决定：

1. 证据强度；
2. 有效持续时间；
3. 独立来源一致性。

规则原则：

- 短暂、单一、弱证据保持 0 或进入待确认状态；
- 持续弱/中证据进入 1；
- 更长持续时间、强视觉证据或视觉与手机一致可进入 2；
- 极强语义证据、长时间事件或多源持续一致进入 3；
- 强证据允许更快升级；
- 降级要求恢复证据和迟滞时间，避免边界抖动；
- `UNKNOWN` 始终输出 `distraction_level=null`；历史等级只出现在 `continuity`，内部 episode 空洞最多跨越 1,000 ms 且不累计时长；
- `NOT_PRESENT` 结束当前分神 episode，但独立记录离位上下文；
- episode 内允许升级和降级，不因每次等级变化创建新 episode。

类型化 episode 开闭规则固定如下，所有时长使用媒体时间：

- `PRONE`：融合后的 PRONE 命题连续确认 3,000 ms 时打开，`started_at_ms` 回填为确认区间起点；连续恢复为非趴下 1,500 ms 后关闭，结束点记为恢复区间起点；
- `HEAD_AWAY`：按第 5.3 节的普通/强证据进入条件打开，回到中立区间连续 1,000 ms 后关闭，开始和结束时间均回填到确认区间起点；
- `NOT_PRESENT`：可信缺席连续 1,500 ms 时打开 `presence_episode`，可信在位连续 500 ms 后关闭；
- 每个类型化 episode 最多跨越 1,000 ms 的对应专家无效空洞，空洞不计入 `active_duration_ms`；超过后在最后有效证据处以 `EVIDENCE_GAP` 关闭；
- 总体 `distraction_episode` 在任一分神证据令等级首次达到 1 时打开，在等级连续恢复为 0 达 3,000 ms、进入 `NOT_PRESENT` 或证据空洞超限时关闭；
- 同一类型 episode 关闭后，后续再次确认必须创建新 id，不得复用旧 id。

第一版阈值必须在配置文件中，并在离线数据上调优；任何初值都标记为 baseline，不宣称是已标定值。

### 7.1 最低证据充分性矩阵

| 输入情况 | applicability | 等级 | 说明 |
|---|---|---:|---|
| `PRESENCE` 持续可信缺席 ≥1,500 ms | `NOT_PRESENT` | `null` | 打开/延续 presence episode |
| `PRESENCE` 不确定或过期 | `UNKNOWN` | `null` | 手机输入不能替代视觉存在证据 |
| 主用户存在，且 Pose-state 或外观趴下专家至少一个有效 | `ACTIVE` | 0～3 | `HEAD_AWAY/GAZE_AWAY` 可选增强，缺失不会拖垮整体 |
| 主用户存在，但 Pose-state 和外观趴下专家均无效 | `UNKNOWN` | `null` | 无法覆盖本期必需的严格趴下能力 |
| 脸部专家无效，但趴下路径有效 | `ACTIVE` | 0～3 | `coverage.head_away=false`，不得假定未转头 |

`ACTIVE + level 0` 表示“在当前明确列出的有效覆盖范围内未确认分神”，不表示所有视觉能力均有效；消费者必须同时读取 coverage。

## 8. 容错与可观测性

- 每个模型适配器隔离异常；单个适配器失败转换为无效证据并记录结构化错误；
- 模型超时、关键点不足、脸部不可见或画质过低只会使对应专家无效；是否令整体进入 `UNKNOWN` 按第 7.1 节矩阵决定；
- 输入方向必须由配置或元数据指定；方向缺失视为配置错误；
- 记录模型名称、版本、文件哈希、配置版本和运行后端；
- 输出每个模型的延迟、调用率、失败率、过期率和证据覆盖率；
- 视频回放采用原始视频时间戳，支持实时速度和无等待的确定性快速回放；
- 同一视频、配置和模型版本应产生相同的规范化业务轨迹：比较 applicability、等级、reason 和 episode 边界，边界允许一个采样周期误差；运行耗时、吞吐和墙钟字段不参与确定性比较；
- 默认不允许从真实模型静默降级为亮度或随机启发式，并继续宣称模型健康。

规则 baseline 与学习型分类器使用显式配置模式 `baseline` 或 `learned`，运行中不得自动互换。baseline 只用于对照和演示兜底，输出必须暴露 `health.inference_mode`；真实适配器失败时仍输出无效证据，不能偷偷切换为规则。

## 9. 数据采集与标签

现有三张图片只能作为冒烟样本：`离座.png` 对应 `NOT_PRESENT`，`低头玩手机.png` 是 `PRESENT/NOT_PRONE` 困难负样本，`睡觉.jpg` 接近 `PRONE` 正样本。由于方向、视角和光照域不同，不能直接作为阈值依据。

需要从实际展示摄像头补录每类若干段 10～30 秒视频：

- 空座位；
- 正常正坐；
- 写字；
- 阅读；
- 轻微低头；
- 低头使用手机；
- 左转、右转、短暂转头、持续转头；
- 逐渐趴下、持续趴下、恢复；
- 遮挡、路人、多人；
- 正常光、暗光和亮度变化；
- 相机断流或跳帧模拟。

标签以 episode 区间为主，包含 `PRESENT`、`NOT_PRESENT`、`PRONE`、`HEAD_AWAY`、`NORMAL_STUDY`、`IGNORE/AMBIGUOUS`。视频、逐帧关键点、模型证据、最终轨迹和标签必须可关联。

数据按完整视频和采集 session 分为调参集与冻结验收集，禁止从同一段视频切帧后同时进入两边。四天展示的最低验收集为：不少于 30 分钟负样本，其中写字/阅读合计不少于 10 分钟、低头手机不少于 5 分钟、短暂转头不少于 20 次；另含 `PRONE`、持续 `HEAD_AWAY` 各不少于 10 个 episode，`NOT_PRESENT` 不少于 5 个 episode。若只有一名演示者，报告必须明确结果仅代表该用户和相机域，不宣称跨用户泛化。

## 10. 验收指标

最高优先级是降低误提醒，允许用适度确认延迟换稳定性。由于提醒策略不属于本项目，定义可计算代理事件 `would_remind`：当一个分神 episode 首次进入 `level >= 2` 时计一次，直到连续恢复到 level 0 达 3 秒或 episode 结束前不重复计数。在负样本标签区间出现的 `would_remind` 计为误提醒。

事件标签最短时长：`PRONE=3,000 ms`、持续 `HEAD_AWAY=2,000 ms`、`NOT_PRESENT=2,000 ms`；不足 1,000 ms 的转头作为短暂转头负样本。预测直接取关闭后的 `evidence_episodes[kind]` 或 `presence_episode`；与真值类型相同且时间区间交并比 IoU ≥0.3 时按 IoU 从高到低进行一对一匹配。仍处于打开状态的末尾事件，以视频结束时间临时闭合后参与离线评测。检测延迟为真值开始到首次进入对应稳定状态的时间；未检出按漏报处理。

调参顺序固定为：先满足误提醒硬上限，再在召回下限之上优化检测延迟和召回，不能为了提高召回突破误提醒上限。核心指标：

- 每小时误提醒次数；
- `PRONE`、`HEAD_AWAY`、`NOT_PRESENT` 的 episode precision/recall；
- 事件开始到首次稳定识别的延迟；
- 同一 episode 内非必要等级切换次数；
- `UNKNOWN` 时间占比及原因分布；
- 写字、阅读、短暂转头、低头学习和画面抖动的误报率；
- 回放确定性；
- 各模型 P50/P95 延迟、调用频率和端到端吞吐。

第五天展示前的最低通过条件（仅针对上述冻结验收集，不代表统计泛化）：

1. 所有准备好的黄金视频均可一条命令回放并生成 JSONL 与叠加视频；
2. `NOT_PRESENT` 不输出分神等级；
3. 负样本中的 `would_remind` 不超过 1 次/30 分钟，目标为 0；
4. `PRONE` episode precision ≥0.90、recall ≥0.80，P95 检测延迟 ≤3,000 ms；
5. `HEAD_AWAY` episode precision ≥0.85、recall ≥0.80，P95 检测延迟 ≤2,500 ms，短暂转头不得产生 level≥2；
6. `NOT_PRESENT` episode precision/recall 均 ≥0.90，且始终输出等级 `null`；
7. 每个匹配 episode 中，1 秒内发生相反方向等级跳变的次数 ≤1；标准清晰画面中的 `UNKNOWN` 时间占比 ≤10%；
8. 手机学习类不会覆盖视觉趴下/转头，娱乐类与视觉一致时能加速升级；
9. 模型失败、断帧和低质量画面能按充分性矩阵解释，程序不崩溃；
10. 规范化业务轨迹重复回放一致，输出包含模型耗时、证据和升级原因。

## 11. 紧凑迭代安排

冻结版 P0 必交：离线视频回放、预训练人物/Pose、真实脸部/head-pose 链路、固定标定、确定性 baseline 融合、手动手机时间线、黄金回归和指标报告。实时摄像头、`GAZE_AWAY`、可选外观专家是条件性目标。轻量关键点时序分类器必须完成训练与对照实验，但只有在冻结验收集上不劣于 baseline 且满足误提醒硬门槛时才进入冻结运行链路；否则保留实验结果并明确展示为未晋级候选，不能以训练集效果替换验收结论。

当前主机已具备 Python 3.12、OpenCV、Ultralytics、PyTorch CUDA、scikit-learn 和 ONNX Runtime；MediaPipe 尚未安装。第一天可以选择安装 MediaPipe 或实现其他 head-pose adapter，但不得让单一可选依赖阻塞回放、契约和 Pose 主链路。

### 第 1 天：端到端骨架与模型基线

- 创建独立 Python 工程、配置、契约和测试入口；
- 同时完成离线视频回放、方向处理、JSONL 和手动手机时间线；叠加显示先做最小诊断版；
- 接入人物/Pose/脸部参考模型；
- 完成固定座位 ROI 和主目标锁定第一版；
- 建立视频采集清单并立即补录关键正负场景；实时摄像头输入为条件性目标；
- 当天结束前必须跑通“视频 -> 多专家证据 -> baseline 融合 -> 输出”。

### 第 2 天：学习型视觉判定与融合

- 保存归一化关键点序列和标签；
- 训练或拟合轻量关键点时序分类器；
- 完成严格 `PRONE`、`HEAD_AWAY`、质量门控和 UNKNOWN；
- 完成 episode 与动态 0～3 等级；
- 规则 baseline 与学习型分类器并行评估，二者由显式模式选择，不允许运行时静默降级。

### 第 3 天：批量评测和稳定性调优

- 批量回放全部视频；
- 优先修复写字、阅读、低头手机、短暂转头、遮挡和丢帧误报；
- 统计事件级精确率/召回率、误提醒、延迟、跳变、UNKNOWN 和模型耗时；
- 仅在关键点路径确实不可分时，再启用可选外观趴下专家；
- 冻结接口和测试集，不再扩展产品功能。

### 第 4 天：冻结演示版本

- 回归黄金轨迹和失败路径；
- 冻结模型、配置、依赖和演示视频；
- 生成指标摘要、模型/证据叠加视频和可解释 JSONL；
- 准备一键演示命令、离线备份、已知限制和后续板端迁移清单；
- 当天结束后不再做高风险模型或架构更换。

### 第 5 天：赴场与现场准备

- 不安排核心开发；
- 仅允许环境检查、演示设备连接、路径修正和不改变判定语义的小修复；
- 保留冻结版本和录屏作为现场兜底。

## 12. 迁移回原产品

主机结果达到验收条件后，再执行：

1. 选择可在 STM32 工具链量化和 benchmark 的人物、Pose、脸部模型；
2. 逐个模型在板端验证算子兼容、P95 延迟、内存、DDR 和 CPU 占用；
3. 将相同证据契约和黄金 JSON 轨迹移植到 C++；
4. 使用录制证据或适配器契约做主机 Python 与板端 C++ 差分测试；
5. 通过单一 adapter 接入原 `focus_app`，替换旧视觉/行为判定；
6. 最后单独接入真实手机上报和提醒策略。

迁移不得重新引入困倦语义、按帧累计、静默 heuristic fallback、手机倍数覆盖视觉风险或 `NOT_PRESENT == focused` 等旧行为。

# EdgeFocus 模块概览 (Modules Overview)

本文档旨在帮助开发者快速定位所需修改的模块。

## 核心架构
应用程序的入口点在 `main.cpp` 中。所有的模块都继承自 `Module` 基类（见 `app/Module.h`），由 `AppContext` 统一管理生命周期（初始化、运行循环、关闭）。

## 模块列表与功能

### 1. 核心应用上下文与状态 (app/)
* **`AppContext`**: 负责管理各个 `Module` 的生命周期，包含启动所有模块的线程。
* **`SystemStateStore`**: **关键数据中心**。保存系统当前的全局状态（例如当前的专注状态、设备连接状态、统计数据等）。任何模块若需获取最新状态快照，或者更新系统状态，都需通过此类。
  * **注意**: 该类已重构为非单例模式，在 `main.cpp` 中实例化并通过构造函数注入到各个依赖的模块中。
* **`BoundedQueue`**: 线程安全的阻塞队列，用于模块间的异步数据传递（如摄像头帧队列、视觉推理结果队列）。

### 2. 视觉管道 (vision/ & camera/)
* **`CameraPipeline` (`camera/CameraPipeline.h`)**: 负责从摄像头（或模拟源）捕获图像帧，并推入 `CameraFrame` 队列。
* **`VisionEngine` (`vision/VisionEngine.h`)**: 视觉推理引擎的对外接口。从 `CameraFrame` 队列获取图像，将其传递给 `InferenceScheduler` 生成 `VisionResult` 并推入下游队列。
* **`InferenceScheduler` (`vision/InferenceScheduler.h`)**: 模型推理调度器。根据当前的运行模式和图像质量，动态选择合适的底层 AI 模型适配器（如硬件加速版或 CPU 备用版）。
* **`FrameQuality`**: 图像质量评估模块。在送入 AI 神经网络前，判断图像是否存在过度模糊或过暗。
* **`ModelRegistry`**: 模型注册表。从 JSON 配置文件中动态加载模型的权重路径和参数。
* **`PoseModelAdapter`**: 具体的模型适配器实现。封装了硬件 NPU 的推理细节（如 STAI MPU），执行头部姿态、面部状态等识别任务。
* **`MockVisionSource`**: 提供模拟的视觉推理结果，用于无硬件情况下的开发与测试。

### 3. 行为分析层 (behavior/)
* **`BehaviorEngine` (`behavior/BehaviorEngine.h`)**: 行为引擎的入口。从 `VisionResult` 队列消费单帧推理结果，通过 `FocusStateMachine` 分析长时间序数据，并在状态改变时记录到数据库。
* **`FocusStateMachine`**: 专注状态机。内部组合了 `SlidingWindow`、`EventDetector` 和防抖逻辑，将一段时间的视觉特征推断为最终的专注状态（如 `FOCUSED`, `SLEEPY_OR_SLUMPED` 等）。
* **`SlidingWindow`**: 滑动时间窗口容器，用来缓存最近 30 秒的连续单帧视觉特征（`VisionResult`）。
* **`EventDetector`**: 事件检测器。对滑动窗口内的数据进行规则匹配（例如计算低头帧占比，或连续闭眼时间），从而抛出抽象的时序事件（`FocusEvent`）。
* **`RiskScorer`**: 风险打分模块，根据检测到的异常事件评判当前的疲劳或分心程度（目前主要作为参考指标）。

### 4. 策略与干预 (policy/)
* **`ReminderPolicy` (`policy/ReminderPolicy.h`)**: 根据当前的专注状态和系统模式（正常/静音/严格），决定是否需要发出提醒以及提醒的等级（`CommandLevel`）。产生 `InterventionCommand`。
* **`ModeManager`**: 管理当前的系统模式。

### 5. 设备控制 (device/)
* **`DeviceManager` (`device/DeviceManager.h`)**: 抽象设备管理器，接收 `InterventionCommand`，并调度给具体的硬件控制器。
* **`ScreenController`**: 控制屏幕（如闪烁提醒）。
* **`BleBandController`**: 控制蓝牙手环（如震动提醒）。

### 6. 持久化存储 (storage/)
* **`SQLiteStore` (`storage/SQLiteStore.h`)**: 基于 SQLite 的本地数据库封装。提供异步写入能力（记录状态变化和提醒事件）和同步查询能力。在 `main.cpp` 中实例化并注入到需要记录数据的模块中。
* **`ReportGenerator`**: 学习报告生成器。它负责在学习阶段结束后，通过聚合查询 `SQLiteStore` 中的日志，生成包含总专注时长、分心次数等指标的统计报告（`SessionReport`）。

### 7. 用户界面 (ui/)
* **`UIManager` (`ui/UIManager.h`)**: 管理所有 UI 页面的渲染（基于 FrameBuffer）和触摸交互。从 `SystemStateStore` 获取数据并展示。包含多个子页面，如 `StatusPage`（主状态）、`StatsPage`（统计数据）等。

## 不同模块之间的调用关系 (Calling Relationships & Data Flow)

项目采用了“**生产者-消费者队列解耦 + 依赖注入**”的混合调用关系。以下是核心数据流转和调用层级：

1. **硬件 -> 视觉层 (跨线程队列通信)**
   * `CameraPipeline` (线程 1) 抓取到图像后，将 `CameraFrame` 对象 `push` 进 `BoundedQueue<CameraFrame>`。
   * `VisionEngine` (线程 2) 阻塞地从上述队列 `pop` 出图像。
   * `VisionEngine` 内部调用 `InferenceScheduler::runOnce`，后者依次调用 `FrameQuality` 进行质量评估，然后调用底层 `PoseModelAdapter` 运行神经网络。
   * 神经网络的结果被组装成 `VisionResult` 并 `push` 到另一个队列 `BoundedQueue<VisionResult>` 中。

2. **视觉层 -> 行为分析层 (跨线程队列通信)**
   * `BehaviorEngine` (线程 3) 阻塞地从 `BoundedQueue<VisionResult>` 中 `pop` 取出数据。
   * 它将单帧数据调用传入 `FocusStateMachine::processFrame()`。
   * 状态机内部：新数据被压入 `SlidingWindow`，然后调用 `EventDetector::detect()` 对整个窗口的历史数据进行扫描，得出有无异常行为（如连续 10 秒低头）。
   * 根据检测结果，状态机经历防抖判定，若确认发生了状态跃迁（例如从 `FOCUSED` 变为 `SLEEPY_OR_SLUMPED`），则更新自身状态。
   * `BehaviorEngine` 获取到新状态后，**直接调用** `SystemStateStore::updateFocusState()`，同时**直接调用** `SQLiteStore::logEvent()` 记录日志。

3. **系统状态 -> 策略层 (轮询调用)**
   * `ReminderPolicy` (独立线程 4) 在 `runOnce()` 的循环中，**直接调用** `SystemStateStore::snapshot()` 获取当前的专注状态快照。
   * 接着调用自身的 `processState()`。根据 `ModeManager` 读取的当前系统模式（如静音模式），决定是否生成一条干预指令 `InterventionCommand`。
   * 若生成了指令，则**直接调用** `SystemStateStore::updateReminder()` 更新状态供 UI 读取。同时调用 `SQLiteStore::logEvent()` 写入提醒日志。
   * 最后，调用全局或传入的 `DeviceManager::issueCommand(cmd)` 接口将指令放入设备执行队列中。

4. **指令执行 -> 外设层 (队列异步执行)**
   * `DeviceManager` 内部维护着一个接收指令的队列。
   * 它取出 `InterventionCommand` 后，通过 `switch-case` 判定提醒等级，**直接调用** `BleBandController::sendAlert()` 或 `ScreenController::flashScreen()`。

5. **状态 -> 用户界面 (无锁只读调用)**
   * `UIManager` (UI 渲染主线程) 以固定的帧率运行 `renderFrame()`。
   * 它每帧都会**调用** `SystemStateStore::snapshot()` 读取整个系统的最新状态（包含专注状态、设备电量、提醒文案等），且完全不需要与后端引擎发生死锁。
   * 将状态快照以参数形式传递给当前的页面（如 `StatusPage::render(fb, snap)`），由页面类负责调用 `FrameBuffer` 的绘图 API 将像素画到屏幕上。

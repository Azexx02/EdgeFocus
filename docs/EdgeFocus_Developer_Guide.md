# EdgeFocus 开发者完整指南 (Developer Guide)

本文档是 EdgeFocus 智能专注力检测板的核心开发者文档集。它整合了**模块架构概览**、**判断逻辑速览**、**功能与 API 查找表**，以及**对外开放接口（BLE/手机端通信）**的全部规范。

---

## 第一部分：系统架构与模块分布

系统被设计为多个松耦合的独立子系统，通过共享的线程安全状态存储进行交互。

### 1. `app/` (核心控制与状态)
- **`EdgeFocusApp`**: 整个应用程序的生命周期管理器，负责按顺序启动所有模块、开启子线程，并监听关闭信号。
- **`SystemStateStore`**: 核心的**线程安全数据中心**。后端 AI 推理和行为状态机将状态 **写入** 此存储，前端 UI 通过快照 `snapshot()` 机制 **读取** 并渲染，完美解耦了 25fps 的屏幕刷新和波动的 AI 推理帧率。

### 2. `vision/` (AI 与视觉引擎)
- **`VisionEngine`**: 启动后台线程捕获摄像头帧（V4L2 设备），并喂给调度器。
- **`InferenceScheduler`**: 根据硬件能力挑选最佳的 AI 适配器，执行推理，并将张量坐标映射为标准的 `VisionResult` 结构。
- **`PoseModelAdapter`**: 核心硬件加速器适配层。加载 `edgefocus_pose_v1_1.nb` 模型，通过 STAI OpenVX 调用 **STM32MP257 芯片内置 NPU** 进行姿态推理，提取 17 个关键点并计算出 `head_down_score`。

### 3. `behavior/` (行为逻辑与状态机)
- **`EventDetector`**: 事件检测规则集，将每一帧的原始 AI 数据翻译为具体事件（如：低头超过 10 秒 -> 触发 `is_sleepy`）。
- **`FocusStateMachine`**: 维护一个 30 秒的滑动窗口，进行防抖与状态平滑。防止系统在“专注”与“分心”边界疯狂震荡闪烁。
- **`BehaviorEngine`**: 主逻辑循环，驱动状态机更新 `SystemStateStore`。

### 4. `ui/` (用户界面与屏幕驱动)
- **`FrameBuffer`**: 屏幕底层驱动抽象。通过 `/dev/spidev0.0` 直接操控 ILI9341 SPI 屏幕（目前已稳定运行于 12MHz 时钟频率，确保与 NPU 共享 DMA 时的总线稳定性）。
- **`UIManager`**: 直接读取 `/dev/input/event` 监听 I2C 触摸屏坐标，并负责调度和渲染各个页面（`StatusPage`, `StatsPage`, `SettingsPage` 等）。

### 5. `policy/` (提醒策略)
- **`ReminderPolicy`**: 决定在什么时间、以什么强度下发干预（如轻度语音提示，或重度蓝牙震动手环警告）。具有冷却时间（Cooldown）机制。

---

## 第二部分：核心逻辑速览与功能拓展查找

### 核心事件检测规则
1. **防误判检测 (`LOW_CONFIDENCE`)**：当连续数帧模型置信度过低（如被遮挡）时，暂停逻辑判断。
2. **离座 (`AWAY`)**：连续 5 秒未检测到人，触发离座。
3. **疲劳/趴桌 (`SLEEPY_OR_SLUMPED`)**：连续 10 秒检测到头部状态为 `DOWN`（低头）触发。
4. **玩手机 (`PHONE_IN_USE`)**：目前采用外部蓝牙嗅探/手机端广播的信号注入方式（详见 `EventDetector::injectPhoneSignal`），收到信号后 5 秒内触发玩手机警告。

### 缺失/待补全的功能列表 (功能查找)
以下是架构中已预留接口，但仍使用占位符或暂未真正接通底层的特性：

* **Vision 细粒度状态**：`VisionResult` 中预留了眼睛开闭 (`EyeState`) 和 手部位置 (`HandState`)，但目前的 `.nb` 姿态模型不支持输出，后端自动映射为 `LOW_CONFIDENCE` 或依靠头部姿态做降级替代。
* **数据存储持久化**：UI `StatsPage` 目前使用静态假数据 (`int heights[] = {30, 50, ...}`)。实际每日专注时长的统计需要对接 `SQLiteStore` 的 `SessionReport` 读取接口。
* **UI 提醒阻断**：当 `ReminderPolicy` 下发提醒时，用户在屏幕上点击消警（`onUserAck`），系统会成倍增加下一次提醒的冷却时间，但目前尚未接入关闭外部手环震动的反向回调逻辑。

---

## 第三部分：对外开放接口与外部接入 (External APIs)

为了实现生态联动，EdgeFocus 不仅在内部跑自己的逻辑，还定义了与手机 App、蓝牙手环等外部设备交互的接口契约。

### 1. 外部干预外设 (物理层输出)
* **蓝牙震动手环 (`BleBandController`)**：
  在触发重度提醒时，通过 C++ `system()` 调用底层的 `bash /root/scripts/send_ble_signal.sh`。该脚本利用 Linux 原生蓝牙协议栈，向配稳的手环发出激活指令。
* **屏幕高频闪烁 (`ScreenController`)**：
  触发警告时，可通过修改背光 GPIO（`PD8`）的占空比或短频重置屏幕底层来制造物理闪烁警报。

### 2. ESP32-Band BLE 通信协议 (手环固件开发必读)
开发板（发送端）与 ESP32 震动手环（接收端）通过 BLE 协议通信：
* **角色**: ESP32 手环作为 Peripheral 广播自己，开发板作为 Central 扫描并连接。
* **UUID 规范**:
  * **Service UUID**: `0x180C`
  * **Characteristic UUID**: `0x2A56` (Write)
* **交互逻辑**: 开发板连接后，向 `0x2A56` 写入任意单字节数据（如 `0x01`），手环即可震动并蜂鸣 500ms。

### 3. 手机端 App HTTP API (手机开发必读)
开发板将默认在局域网内暴露 HTTP RESTFul 接口，手机 APP 可用作辅助看板，但开发板依旧保持独立运行能力（脱机仍可记录和提醒）。

* **GET `/api/v1/session/current_state`**
  轮询当前用户的专注状态枚举。
  *返回结构*：`{"status":"success","data":{"focus_state_str":"FOCUSED", "current_focus_duration_sec": 1200}}`

* **GET `/api/v1/reports/summary?type=daily`**
  获取今日专注时长与分心次数的统计报告，用于渲染 APP 内的图表。

* **PUT `/api/v1/device/preferences`**
  供手机端调整开发板的提醒模式。
  *请求参数*：`{"system_mode": 1, "reminder_preferences": {"enable_sound": false}}`
  *模式对照*：`0=NORMAL` (正常), `1=SILENT` (静音), `2=HOME` (宽松)。

* **POST `/api/v1/reminder/ack`**
  手机端弹窗点击“已知晓”时，通过此接口通知开发板停止物理蜂鸣或闪光。

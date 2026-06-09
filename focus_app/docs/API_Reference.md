# EdgeFocus 核心 API 参考 (API Reference)

本文档记录了项目中一些核心类和数据结构的使用方法，方便后续开发查阅。

## 1. 核心状态存储: `SystemStateStore`

`SystemStateStore` 是线程安全的系统状态中心。模块通过构造函数注入获取它的引用。

```cpp
// 注入示例
class MyModule : public Module {
public:
    MyModule(SystemStateStore& store) : Module("MyName"), store_(store) {}
    // ...
private:
    SystemStateStore& store_;
};
```

### 关键方法
* **获取状态快照 (UI 层最常用)**:
  ```cpp
  SystemSnapshot snap = store_.snapshot();
  // snap 包含了 focus_state, focus_duration_sec, device_status, current_reminder 等完整信息，且读取过程是线程安全的。
  ```
* **更新专注状态 (行为分析层使用)**:
  ```cpp
  store_.updateFocusState(FocusState::SLEEPY_OR_SLUMPED, duration_sec, "User appears sleepy");
  ```
* **更新设备状态 (外设管理层使用)**:
  ```cpp
  DeviceStatus status;
  status.battery_percent = 80;
  status.camera_ok = true;
  store_.updateDeviceStatus(status);
  ```
* **提醒管理 (策略层使用)**:
  ```cpp
  store_.updateReminder(CommandLevel::LEVEL_1, "请坐直", timestamp_ms);
  store_.incrementReminder();
  store_.clearReminder(); // UI 或定时清理
  ```

## 2. 数据库存储: `SQLiteStore`

`SQLiteStore` 用于持久化日志和统计分析。为避免阻塞主业务逻辑，`logEvent` 是异步写入的。

```cpp
// 在 BehaviorEngine 或 ReminderPolicy 中记录事件
StorageEvent ev;
ev.session_id = "session_123";
ev.type = StorageEventType::StateChange;
ev.timestamp = current_time_ms;
ev.data = "Changed to FOCUSED";

sqlite_store_.logEvent(ev); // 异步排队，快速返回
```

* **查询方法** (目前支持同步查询，可用于复盘页面或生成报告):
  ```cpp
  auto events = sqlite_store_.queryEvents("session_123");
  ```

## 3. 视觉结果: `VisionResult`

`VisionResult` 是 AI 模型推断出的特征集合（定义在 `vision/VisionResult.h`），由 `VisionEngine` 产生并送往 `BehaviorEngine`。

* **数据结构亮点**:
  ```cpp
  struct VisionResult {
      int64_t timestamp_ms;
      FrameQuality frame_quality;
      float presence_conf;     // 有人在屏幕前的置信度 (0.0~1.0)
      float head_down_score;   // 低头/趴桌的打分
      HeadState head_state;    // NORMAL, DOWN, LEFT, RIGHT
      
      // 以下特征包装了置信度和有效性 (StateVar)
      StateVar<Presence> presence;
      StateVar<FaceState> face;
      StateVar<HeadPose> head_pose; // 包含 pitch, yaw, roll
      StateVar<EyeState> eyes;      // OPEN, CLOSED, PARTIAL
      StateVar<PostureType> posture;
      StateVar<HandState> hands;
      StateVar<PhoneState> phone;
      
      ReasonCode reason_code;  // 主要推断原因
  };
  ```

## 4. 提醒与策略: `InterventionCommand`

`ReminderPolicy` 生成的干预指令，指示设备进行提醒。

```cpp
struct InterventionCommand {
    CommandLevel level;   // NONE, LEVEL_1 (轻提示), LEVEL_2 (警告), LEVEL_3 (强提醒), REST_SUGGESTION
    std::string message;  // 提醒文本 (如: "检测到手机使用，请放下手机")
    int64_t timestamp_ms; // 触发时间戳
};
```

## 5. 线程间通信: `BoundedQueue<T>`

项目中统一使用 `BoundedQueue` 作为模块间的解耦桥梁。

```cpp
BoundedQueue<VisionResult> queue(10, false); // 容量为10，满了之后 false 表示 block (阻塞推入)

// 生产端
VisionResult res = ...;
queue.push(std::move(res)); 

// 消费端
if (auto opt_res = queue.tryPop()) {
    VisionResult res = std::move(*opt_res);
    // 处理结果
}
```

利用这些文档，新的开发者可以快速理解各个部分的作用和主要 API 的交互方式。

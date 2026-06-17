# 后续任务计划

---

## Task A: Session 来源标记 + 反馈重设计

### 需求
1. Session 记录区分来源：`LOCAL`（本地模式）和 `EDGEFOCUS`（板端同步）
2. 会话详情的反馈改为三个按钮：「提醒过多」「提醒过少」「适中」
3. Local 模式数据汇入板端（专注时长 + 手机干扰次数），走会话记录同步
4. Local 反馈给 app，板端来源的反馈通过 reminder_ack 给板端

### 改动

**Session 实体：**
```kotlin
data class Session(
    // ... existing fields
    val source: String = "LOCAL",  // "LOCAL" 或 "EDGEFOCUS"
    val feedback: String? = null,   // "too_much" / "too_little" / "just_right" 或 null
)
```

**DashboardScreen 会话详情：**
- 点击记录 → 弹窗显示完整数据 + 来源标签
- 底部三个反馈按钮：「提醒过多」「提醒过少」「适中」
- 点击 → 更新 Session.feedback（通过 updateFeedback）
- BOARD 来源的反馈同时发送 reminder_ack 到板端

**Local 模式同步板端：**
- 停止会话时，如果 LOCAL + BLE 已连接 → 通过 phone_sync 同步专注时长和手机干扰次数
- 走已有的 syncSessionToBoard 路径

---

## Task B: 硬件控制（规范 + UI）

### 需求
控制面板第 3 页硬件控制改为可交互，值从板端获取，调整后发送到板端。

### 规范（写入沟通文档）

板端需在 `update_preferences` 中支持：
```json
{
  "cmd": "update_preferences",
  "data": {
    "vibration_strength": 0.5,
    "led_brightness": 75,
    "reminder_sensitivity": 0.5,
    "system_mode": 0,
    "away_timeout_min": 15
  }
}
```

板端需在 `get_current_state` 或 `get_device_status` 中返回当前硬件参数值。

### UI 改动
- 控制面板第 3 页：
  - 振动强度 → Slider
  - LED 亮度 → Slider
  - 提醒灵敏度 → Slider
  - 系统模式 → 两个按钮（正常/静音）
  - 离座超时 → Slider
- 调整时发送 update_preferences 到板端

---

## Task C: 沟通文档更新

在 `docs/board-communication.md` 中补充：
- 硬件控制接口规范（vibration_strength, led_brightness, reminder_sensitivity）
- 系统模式切换（正常/静音）
- Local 模式会话同步协议
- Session 反馈字段说明

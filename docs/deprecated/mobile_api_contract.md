# [已废弃] EdgeFocus 手机端通信接口定义 (Mobile API Contract)

> **本文档已废弃**，请使用 `docs/Mobile_API_Contract_v2.md` 作为唯一 API 契约。
> 手机端和板端开发均以 v2 文档为准。

本文档定义了开发板（EdgeFocus终端）与未来手机端 APP 之间的最小化通信 API 接口。

## 核心设计原则 (Design Principles)
1. **隐私优先**：默认不暴露任何原始的实时视频流或图片给手机端，仅传输经过端侧 AI 处理后的脱敏状态（枚举值）和统计数据。
2. **松耦合**：手机端 APP 定位为辅助查看和配置工具，不可作为开发板核心业务循环的强制依赖组件。开发板必须能够在完全脱离手机的情况下，独立运行并完成专注力监测与干预闭环。
3. **基于 JSON 的风格**：推荐使用 HTTP/REST 或 MQTT+JSON 格式进行交互，兼顾实时性和易用性。

---

## 1. 获取设备状态 (Get Device Status)
获取设备当前的基本运行信息。

**Endpoint**: `GET /api/v1/device/status`

**Response**:
```json
{
  "status": "success",
  "data": {
    "device_id": "M257_ABC123",
    "battery_level": 85,
    "is_session_active": true,
    "current_session_id": "sess_123456",
    "system_mode": 0
  }
}
```

## 2. 获取当前学习状态 (Get Current Focus State)
实时或轮询获取用户当前的专注力状态枚举值。

**Endpoint**: `GET /api/v1/session/current_state`

**Response**:
```json
{
  "status": "success",
  "data": {
    "session_id": "sess_123456",
    "timestamp": 1684001234567,
    "focus_state": 0,
    "focus_state_str": "FOCUSED",
    "current_focus_duration_sec": 1200 
  }
}
```

## 3. 获取今日/周摘要 (Get Summary Reports)
获取用户的专注力时长、分心次数等统计报告。对应存储层的 `SessionReport`。

**Endpoint**: `GET /api/v1/reports/summary?type={daily|weekly}&date={YYYY-MM-DD}`

**Response**:
```json
{
  "status": "success",
  "data": {
    "report_type": "daily",
    "date": "2026-06-06",
    "total_session_duration": 7200,
    "total_focus_duration": 6000,
    "distraction_count": 5,
    "reminder_count": 3
  }
}
```

## 4. 开始/暂停/结束会话 (Session Control)
手机端可以遥控终端，控制专注周期的生命周期。

**Endpoint**: `POST /api/v1/session/control`

**Request**:
```json
{
  "action": "start" // 可选: "start", "pause", "resume", "end"
}
```

**Response**:
```json
{
  "status": "success",
  "data": {
    "session_id": "sess_123457",
    "state": "active"
  }
}
```

## 5. ACK 提醒 (Acknowledge Reminder)
用于手机端弹窗提示干预时，用户点击“已了解”，将状态同步回开发板，开发板据此停止当前的物理干预动作（如关闭蜂鸣器或提示灯）。

**Endpoint**: `POST /api/v1/reminder/ack`

**Request**:
```json
{
  "reminder_id": "rem_888",
  "action": "acknowledge"
}
```

**Response**:
```json
{
  "status": "success"
}
```

## 6. 设置模式与提醒偏好 (Set Mode and Preferences)
配置开发板的系统模式（SystemMode）以及提醒逻辑阈值。

**Endpoint**: `PUT /api/v1/device/preferences`

**Request**:
```json
{
  "system_mode": 1,
  "reminder_preferences": {
    "distraction_threshold_sec": 15,
    "enable_sound": false,
    "enable_light": true
  }
}
```

**Response**:
```json
{
  "status": "success",
  "data": {
    "system_mode": 1,
    "updated_at": 1684001240000
  }
}
```

---

### 数据结构枚举对照

**FocusState (专注状态)**:
对应 `behavior/FocusState.h`
- `0`: FOCUSED (专注)
- `1`: LOOKING_AWAY (左顾右盼)
- `2`: SLEEPY_OR_SLUMPED (疲劳/趴桌)
- `3`: PHONE_IN_USE (玩手机)
- `4`: AWAY (离座)
- `5`: LOW_CONFIDENCE (检测置信度低)
- `6`: PAUSED (暂停中)
- `7`: CALIBRATING (校准中)

**SystemMode (系统模式)**:
对应 `policy/ModeManager.h`
- `0`: NORMAL (正常模式，声光干预全开)
- `1`: SILENT (静音模式，仅灯光或软提醒)
- `2`: HOME (家庭模式，可能阈值较宽松)

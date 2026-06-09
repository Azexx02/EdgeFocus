# EdgeFocus 手机端通信接口定义 (V1.0 Draft)

为了支持“手机端辅助监测手机情况”以及后续的设置同步与数据查看，我们需要在 STM32MP2 开发板和手机 App 之间建立通信通道。考虑到设备可能会在“图书馆”等没有稳定 Wi-Fi 的环境使用，**强烈推荐主要采用低功耗蓝牙 (BLE) 或基于局域网的简单 TCP/UDP (如果连了同一个热点) 进行通信。**

为了便于你在手机端并行开发，这里初步拟定一套 **通用消息格式 (JSON)**。不管底层走的是 BLE GATT 的特征值写入，还是局域网的 TCP Socket，内容载荷 (Payload) 都是这套格式。

## 1. 核心接口：手机干扰状态上报 (Phone Status Sync)
手机端 App（比如通过检测屏幕解锁状态、应用使用时长）主动向开发板发送当前手机使用状态。
开发板收到后，会触发状态机进入 `PHONE_IN_USE` 状态，并据此决定是否震动手环。

**方向**: `Mobile App -> Board`
**Payload**:
```json
{
  "cmd": "sync_phone_status",
  "data": {
    "is_in_use": true,             // true: 正在被玩, false: 已经锁屏/放下
    "app_category": "entertainment", // (可选) entertainment/social/learning
    "duration_sec": 120            // (可选) 本次连续使用秒数
  }
}
```
*   **开发板处理逻辑**：如果是 `is_in_use: true`，且当前是 `ACTIVE`（专注）状态，开发板将直接把当前 `FocusState` 切成 `PHONE_IN_USE`，并触发 `LEVEL_3` 震动提醒。如果是 `false`，则清除手机干扰惩罚。

## 2. 状态查询与控制接口 (Command & Control)
手机端作为超级遥控器，可以随时起停番茄钟或修改配置。

### 2.1 强制起停番茄钟 (Start/Stop)
**方向**: `Mobile App -> Board`
允许手机端强制控制设备进入专注倒计时，或者提前结束专注。
```json
{
  "cmd": "set_phase",
  "data": {
    "phase": "ACTIVE",             // "IDLE", "CALIBRATING", "ACTIVE"
    "target_duration_mins": 45     // (可选) 如果传入，设备端的进度条可基于此总长计算
  }
}
```

### 2.2 修改设备设置 (同步模式与时长)
**方向**: `Mobile App -> Board`
```json
{
  "cmd": "update_settings",
  "data": {
    "system_mode": "SILENT",       // "NORMAL" 或 "SILENT"
    "auto_terminate_mins": 30      // 自动离位结算时长
  }
}
```

## 3. 双端实时同步机制 (Dual-End Real-Time Sync)
为了实现“双端番茄钟同步”（手机端UI能秒级看到开发板的计时和状态变化），设备需要通过 `Notify` 特征值主动向手机推送以下报文：

### 3.1 状态心跳广播 (Tick Sync)
**方向**: `Board -> Mobile App` (每隔 1~2 秒主动 Notify 一次)
```json
{
  "cmd": "sync_tick",
  "data": {
    "phase": "ACTIVE",             // 当前阶段
    "focus_state": "FOCUSED",      // 实时 AI 状态 (FOCUSED/AWAY/LOOKING_DOWN 等)
    "elapsed_sec": 1205,           // 当前番茄钟已进行秒数
    "distraction_count": 3         // 当前累计分神次数
  }
}
```
*手机端收到这个心跳包，就可以平滑地渲染番茄钟的倒计时圈和状态文字。*

### 3.2 瞬发干预警告 (Real-time Alert)
**方向**: `Board -> Mobile App` (AI 检测到分神并决定惩罚时触发)
```json
{
  "cmd": "distraction_alert",
  "data": {
    "reason": "HEAD_DOWN",
    "level": 2,                    // 震动干预等级
    "message": "请抬起头！"
  }
}
```

## 4. 数据拉取接口 (Data Pull)
手机端需要展示当天的专注报告。

### 3.1 获取今日汇总
**方向**: `Mobile App -> Board`
```json
{
  "cmd": "get_today_summary"
}
```
**开发板回复 (Board -> Mobile App)**:
```json
{
  "ack": "get_today_summary",
  "data": {
    "total_seconds": 7200,
    "focus_seconds": 6000,
    "distraction_events": 5,
    "sessions_count": 3
  }
}
```

## 4. 底层通信协议 (BLE GATT Server)
为保证在无网环境（如公共图书馆）的稳定使用，**系统现采用低功耗蓝牙 (BLE) 作为唯一的跨端通信桥梁**。
STM32MP2 开发板将运行一个后台 GATT Server 进程，并广播包含服务信息的 Advertising Packet。

### 4.1 UUID 分配 (规划)
*   **服务 (Service) UUID**: `0000FF00-0000-1000-8000-00805F9B34FB` (待定，可自定义专属 UUID)
*   **命令写入特征值 (TX Characteristic)**: `0000FF01-...`
    *   **属性**: `Write`, `WriteWithoutResponse`
    *   **作用**: 手机端向该特征值写入上述 JSON 字符串。
*   **状态通知特征值 (RX Characteristic)**: `0000FF02-...`
    *   **属性**: `Read`, `Notify`
    *   **作用**: 当开发板有报告数据返回，或者状态发生变化时，通过此特征值 Notify 手机端。

由于单次 BLE MTU 限制（通常 20 字节到 244 字节不等），在发送过长 JSON（如获取今日汇总报告）时可能需要自行在应用层处理分包组装，或者我们在后续将协议压缩为二进制（ Protobuf / 自定义字节流 ）。目前为了你手机端快速搭通，建议先直接发短 JSON 测试。

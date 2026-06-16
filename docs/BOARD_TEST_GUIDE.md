# 板端逐模块测试指南

## 测试策略

每个模块有独立的测试程序，单独编译、单独部署、单独运行。出问题时能精确定位到哪个模块。

```
test_storage          → SQLiteStore + ReportGenerator (纯数据层)
test_behavior_fsm     → FocusStateMachine + EventDetector (纯逻辑层)
test_reminder_policy  → ReminderPolicy + ModeManager (策略层)
test_device_manager   → DeviceManager + ScreenController + BleBand (硬件层)
test_vision_result    → VisionResult + MockVisionSource (视觉层)
test_ble_gatt         → BleGattServer API (通信层)
```

## 依赖关系图（测试隔离）

```
test_storage           → SQLite3 (独立，无其他模块依赖)
test_behavior_fsm      → 无外部依赖 (纯状态机逻辑)
test_reminder_policy   → SQLiteStore + DeviceManager (策略需要设备接口)
test_device_manager    → 无外部依赖 (直接操作硬件)
test_vision_result     → 无外部依赖 (Mock数据 + CPU推理)
test_ble_gatt          → SQLiteStore + ReportGenerator + ReminderPolicy + DeviceManager
```

## 部署顺序（推荐）

按依赖关系从底层到上层：

```
1. test_storage         ← 最底层，验证SQLite读写
2. test_behavior_fsm   ← 纯逻辑，无硬件依赖
3. test_vision_result  ← 视觉推理，需要摄像头或Mock
4. test_device_manager ← 硬件层，需要SPI/GPIO/BLE
5. test_reminder_policy← 策略层，依赖设备层
6. test_ble_gatt       ← 通信层，依赖所有其他模块
```

## 使用方法

### 1. 交叉编译单个测试

```bash
# 编译单个测试
python deploy/build_test.py test_storage

# 编译所有测试
python deploy/build_test.py test_all
```

### 2. 部署到板端

```bash
# 部署并运行单个测试
python deploy/run_board_test.py test_storage

# 部署并运行所有测试（按顺序）
python deploy/run_board_test.py all

# 先编译再部署
python deploy/run_board_test.py test_storage --build
```

### 3. 手动部署（如果脚本不可用）

```bash
# 在SDK服务器上编译
ssh hu@192.168.10.6
cd /home/hu/edgefocus_build/focus_app/build_arm
source /opt/st/stm32mp2/5.0.3-snapshot/environment-setup-cortexa35-ostl-linux
make test_storage -j$(nproc)

# 传到板端
scp tests/test_storage root@192.168.10.210:/root/tests/

# 在板端运行
ssh root@192.168.10.210
cd /root/tests
./test_storage
```

## 各测试程序验证内容

### test_storage
- [x] SQLiteStore 初始化、建表
- [x] logEvent 写入事件
- [x] queryEvents 查询事件
- [x] ReportGenerator 生成报告
- [x] 扩展字段 (longest_focus_streak_ms, phone_distraction_count)
- [x] 数值状态码匹配

### test_behavior_fsm
- [x] 正常专注 → FOCUSED
- [x] 低头检测 → SLEEPY_OR_SLUMPED
- [x] 手机使用 → PHONE_IN_USE
- [x] 离位检测 → AWAY
- [x] 低置信度 → LOW_CONFIDENCE
- [x] 状态恢复迟滞
- [x] 视线偏移 → LOOKING_AWAY (新增)
- [x] 手机干扰分级 (新增)
- [x] 校准质量门控 (新增)
- [x] 风险评分 (新增)

### test_reminder_policy
- [x] 事件升级 (LOOKING_AWAY → LEVEL_1)
- [x] 疲劳检测 (SLEEPY → REST_SUGGESTION)
- [x] 静音模式降级
- [x] 用户确认冷却
- [x] 学习类手机不干预 (新增)
- [x] 强干扰 LEVEL_3 (新增)

### test_device_manager
- [x] 命令队列入队/出队
- [ ] 屏幕闪烁 (需要SPI硬件)
- [ ] 手环震动 (需要BLE连接)

### test_vision_result
- [x] ConfidenceValue 默认值
- [x] VisionResult 辅助函数
- [x] Mock 场景 (7种)
- [x] CPU Fallback 推理

### test_ble_gatt
- [x] get_device_status
- [x] get_current_state
- [x] get_today_summary
- [x] get_last_report
- [x] session_control (start/end)
- [x] update_preferences
- [x] reminder_ack
- [x] phone_sync
- [x] get_console_data
- [x] 未知命令处理

## 常见问题排查

| 问题 | 可能原因 | 排查方法 |
|------|---------|---------|
| test_storage 段错误 | SQLite 文件权限 | 检查 /root/tests/ 目录权限 |
| test_device_manager BLE 失败 | 蓝牙未开启 | `rfkill unblock bluetooth` |
| test_vision_result 推理失败 | 摄像头未连接 | 用 Mock 模式测试 |
| test_ble_gatt 连接失败 | D-Bus 未启动 | `systemctl status bluetooth` |
| 编译失败 | SDK 环境未加载 | `source /opt/st/.../environment-setup-cortexa35-ostl-linux` |

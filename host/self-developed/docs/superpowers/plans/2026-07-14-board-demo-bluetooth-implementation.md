# EdgeFocus 板端展示与蓝牙可靠性实施计划

依据：`docs/superpowers/specs/2026-07-14-cross-device-demo-and-bluetooth-contract.md`

## 阶段 1：运行域与证据契约

1. 扩展 `SessionSnapshotPublisher`，一次原子发布 visual/phone/assessment。
2. 为视觉快照加入主体框、关键点和媒体时间。
3. 新增独立 `DemoRuntime`，持有自己的帧队列、快照、手机事实和预览，不写正式数据库或提醒通道。
4. 为 `SessionOrchestrator` 增加正式会话启动前钩子，原子停止 Demo 并释放 NPU。
5. 移除展示页创建/暂停/结束正式会话的能力，封闭旧 Demo mutation API。

## 阶段 2：预录 NPU 与前端回退

1. 从 P01 稳定片段生成约 30～35 秒 H.264 MP4。
2. `DemoRuntime` 使用 OpenCV 按原始 FPS 循环读取该文件，并进入同一 NPU/时序/融合实现。
3. 预录 NPU 允许本地演示手机类别；正式会话只接受 App事实。
4. 提供本地视频与 fallback trace 静态端点；强制回退为纯前端动画且禁用手机按钮。
5. 每轮循环更新 run ID、清空 30 秒轨迹和时序连续性。

## 阶段 3：展示页

1. 保留现有深色 EdgeFocus 页面视觉系统。
2. 增加仅 IDLE 可操作的现场/预录切换和 NPU/保底开关。
3. 视频上以 SVG/Canvas 绘制主体框、关键点和证据新鲜度。
4. 恢复 PRES/PRONE/PHONE/LEVEL 四轨近 30 秒时间线。
5. 明确显示 LIVE、RECORDED NPU、FRONTEND REPLAY 和降级状态。

## 阶段 4：云台硬禁用

1. `main.cpp` 不实例化或注册 `GimbalMonitor/GimbalSessionFeature`。
2. 偏好默认和清洗逻辑强制 `gimbal_enabled=false`。
3. BLE/HTTP 控制入口统一返回 disabled；展示页不显示云台组件。

## 阶段 5：蓝牙可靠性

1. PhoneFact 按会话 sequence 幂等、5 秒有效期，重复包不续鲜。
2. 增加 App `client_hello/event_ack` 与按能力启用的关键事件 outbox。
3. 修正 attempted/sent/received/presented/user_ack 状态。
4. 手环改为非阻塞 `1 in-flight + 1 pending`、TTL、重试、覆盖与过期状态。
5. 增加断线、重连、过期、重复 ACK/sequence 测试。

## 阶段 6：验证与部署

1. Windows 主机构建逻辑测试。
2. 编译机交叉构建完整 OpenCV/STAI/BlueZ 版本。
3. 部署至 `192.168.10.174`，验证摄像头、预录 NPU、强制回退和蓝牙测试客户端。
4. Playwright 在 1920×1080 检查布局、控制权限、循环与控制台错误。
5. 记录板端性能、故障回退和已知限制。

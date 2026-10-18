# 云台全周人工控制与受限自动控制实施计划

## 目标

在不改写 G431 NVM V3 的前提下，让固件 Production 接受完整单圈位置；主机统一执行 AUTO ±70°业务边界、MANUAL 边界停靠门、30°/10°双步进，并让 App 与网页只发送逐次 `fine_tune` 布尔值。

## 实施切片

1. 新建 `platform/gimbal/GimbalMotionPolicy.*` 和纯单元测试：覆盖环形角度、AUTO 夹紧/范围外向内、MANUAL 边界门与双轴原子决策。
2. 在 `GimbalHardwareService` 中给自动入口接入策略，新增 ACK-only 全周人工入口，并处理 UART 不确定结果。
3. 在 `GimbalPrecheckController` 中维护每轴 `NORMAL/PENDING/ARMED`，按实测稳定姿态解锁越界；配置普通 30°、微调 10°。
4. `AppCommandService` 和 Demo 页面解析/发送 `fine_tune`，响应返回解析步进与边界决策；不接受客户端角度。
5. G431 Production 接受 `0..3599`，Maintenance 保留 ±10°；`VER` 增加 `FULL_RANGE_POSITION`，NVM V3 不迁移。
6. 运行主机专项测试、G431 host test、可用全量测试和 diff 检查。
7. 生成 App 字段说明和板端验收配置；配置未获明确批准前不执行运动。

## 验收重点

- AUTO 在范围内向外夹到 ±70°；范围外仅允许逐轴向 0°靠近。
- MANUAL 从范围内跨界先停稳于 ±70°；已在边界时首击可越界；普通/微调可交替消费许可。
- 双轴 TRACK 整体发送或整体拒绝；UART 不确定结果不重发。
- Production 全周与 Maintenance ±10°隔离；旧 NVM golden image 不迁移、不重写。

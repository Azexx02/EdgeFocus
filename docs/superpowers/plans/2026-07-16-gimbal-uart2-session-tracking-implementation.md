# 云台 UART4 / 协议 2.0 / 会话跟踪实施计划

1. 扩展协议事件和测试：解析协议 2.0 能力、`P_DEG/Y_DEG`、ARM/SET/TRACK 应答；生成整数 `SET` 与 0.1° `TRACK`。
2. 把 Linux I/O 改成显式 `uart_only` / `uart_gpio_interlock` 双模式，默认 UART4 `/dev/ttySTM3`；补 raw `!` 停机入口。
3. 改造硬件服务：UART-only ARM 双应答门、精确命令应答、停止状态复核、双模式 Fault/恢复；扩展 fake-I/O 回归。
4. 把板级配置升级为协议/控制模式/相对运动窗口/方向与跟踪参数，保留对旧配置的明确拒绝而非猜测迁移。
5. PRECHECK 改为 ARM 参考位 + 相对 waypoints；增加视觉跟踪控制器并在 FOCUSING 更新、PAUSED HOLD、丢人保持。
6. 更新独立 bring-up，先在 host 跑单测和 clean build，再交叉编译、部署独立二进制并执行板端受限运动验收。
7. 独立验收通过后才更新 `motion_verified`、解除主服务禁用并验证 PRECHECK/人工控制/会话跟踪/Fault 恢复。
8. 同步 AGENTS、当前架构、板端实测和部署记录；检查 dirty worktree，只提交本任务涉及的文件。

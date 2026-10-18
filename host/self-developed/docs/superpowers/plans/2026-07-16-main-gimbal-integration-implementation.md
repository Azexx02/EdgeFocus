# 主程序云台融合实施计划

依据：`docs/superpowers/specs/2026-07-16-main-gimbal-integration-design.md`。

1. 对照独立程序提交 `586e511`，把搜索计划、候选关联/构图和实际角度验证拆成主程序原生小组件，不引入第二套 UART、相机或 NPU 所有者。
2. 扩展 `IGimbalSessionHardware` 与 `GimbalHardwareService`：ANGLE 重试、稳定冻结、SET 后到位复核、有界静态误差补偿、健康时回参考位。
3. 扩展板级配置：已验证硬限制、常规搜索范围、全俯仰恢复、75 秒上限、候选居中和构图参数；正式配置设置 `motion_verified=true`。
4. 将 `GimbalPrecheckController` 改成观察、常规搜索、恢复搜索、候选居中、构图确认和 Manual/HOLD 状态机；保持 session/sequence/manual lease 门禁。
5. 统一 `GimbalVisualTracker` 的目标映射：肩部优先、头部与主体框回退；单步 1.5°、两帧死区确认；FOCUSING 才持续追踪。
6. 更新 `GimbalSessionFeature` 的 PRECHECK/FOCUSING/PAUSED/ENDING 调度、恢复行为和 `GimbalSnapshot.detail` 投影。
7. 新增或扩展 host 测试：搜索计划、硬件到位、候选居中/丢失、构图窗口、75 秒超时、会话阶段、故障恢复和 PRECHECK 正式边界。
8. 运行 MSVC/Ninja clean build 与全部测试；只修复本任务引入的失败，不清理工作树其他改动。
9. 使用现有交叉编译/部署链构建 ARM 全目标，先运行板端测试，再部署主程序 release；部署前备份并保留回滚。
10. 板端验证 IDLE 不动、PRECHECK 自动找人、HOLD/手动租约、App 确认后追踪、PAUSED、ENDING 回中/停机和服务健康；随后交付全量功能测试清单。

# M33 风扇极性、测速与自启动实施计划

## 目标

按已批准的设计，在不影响 `focus_app` 会话域的前提下，将风扇控制从手动 M33 验证状态收束为可校准、可健康检查、可自动回退的独立板级服务。

## 实施顺序

1. 新增可在主机测试的 CH1N 逻辑 duty 到 compare 映射函数，覆盖边界、反向映射和非法输入。
2. M33 板级适配改用该映射，恢复 `safe_duty_ns=8000`，启用 PB11/EXTI 测速。
3. 运行主机单元测试并重新构建 M33 ELF；先手动部署，保持所有新服务禁用。
4. 在板上验证逻辑 duty 与 RPM 单调关系，记录原始脉冲；PPR 未独立确认前不把 RPM 当作标定依据。
5. 新增幂等的资源切换脚本、M33 loader、bridge、健康检查和 Linux PWM 回退 unit。
6. 先手工执行 start/fallback 故障注入，确认无 PB10 双写、状态文件不陈旧、Linux 回退输出可读回。
7. 只有波形、PPR、常温转速、硬件默认自转和回退全部通过后，才启用开机链并执行冷启动验收。
8. 更新架构、硬件和 agent 交接文档，记录最终 PPR、档位转速及回滚命令。

## 阻断条件

- 无法在风扇接口独立确认满速控制波形：允许继续 raw pulse/堵转诊断，但禁止启用自启动。
- PPR 候选无法唯一判定：标记 `ppr_unresolved`，禁止启用自启动。
- PB11 无脉冲、RPM 非单调、Linux fallback 读回或测速失败：禁止启用自启动。
- 任一服务切换可能导致 Linux 与 M33 同时驱动 PB10：立即回退厂商 loader。

## 验证命令

```powershell
cmake -S platform/fan -B build/fan-host -DEDGEFOCUS_FAN_BUILD_BRIDGE=OFF
cmake --build build/fan-host --config Release
ctest --test-dir build/fan-host -C Release --output-on-failure
```

板端验收以 systemd 状态、remoteproc firmware/state、`fan-status.json` 新鲜度、PB11 脉冲、PWM sysfs 读回以及两次冷启动结果共同为准。

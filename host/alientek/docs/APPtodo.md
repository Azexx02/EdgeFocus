# APP TODO

更新时间：2026-07-03

本文只记录 App 侧需要新增或适配的事项。主机内部策略语义不要求 App 复现。

重要边界：
- `screen_output_route`、`sound_output_route`、`gimbal_enabled` 是工程/高级诊断配置，用于不同硬件版本适配；普通用户设置页不要暴露成可随手切换的入口。
- `normal_channels` / `silent_channels` 才是普通用户可见的提醒渠道偏好；normal/silent 只筛渠道，不改变提醒等级。
- `get_console_data` 和云台诊断动作只用于开发/联调，不做普通用户入口。

## P0：主链路必须适配

1. 会话控制闭环
   - 支持 `session_control start / skip_precheck / pause / resume / end`。
   - App 发起 `session_control start` 后必须等待主机响应，再按主机返回的 `session_phase` 和 `precheck` 渲染当前步骤；这是既有主流程，不是新增等待环节。
   - `session_phase=precheck` 时一定展示通用预检内容：摄像头画面预览、距离/姿态提示、继续/跳过。
   - 如果 `precheck` 中包含 `gimbal` 对象，则在通用预检页里展示云台校准/控制区；如果不包含 `gimbal`，不要展示任何云台内容，也不要把云台离线、不安全或故障错误暴露给普通用户。
   - `precheck.gimbal` 只在主机确认“本次会话云台功能已启用、运行时接入云台模块、且运动控制可用”时出现；`gimbal_enabled=false`、`EDGEFOCUS_DISABLE_GIMBAL=1`、云台 offline 或 unsafe 时，都按“无云台控制区”处理，但通用摄像头预检页仍然存在。
   - focusing 阶段展示倒计时，使用主机返回的 `remaining_ms`，不要自行正计时替代。
   - 结束后按同一 `session_id` 查询 report/history，并提交 feedback。

2. phone_sync 上报
   - App 在学习会话中上报手机使用状态。
   - 字段至少包含 `is_in_use`、`distraction_level`、`phone_state` 或 `app_category`。
   - 退出手机使用时必须发 `is_in_use=false`，让主机清除旧手机压力。
   - App 不把 `PHONE_IN_PLAYING` 固定理解成 LEVEL_3。
   - `phone_sync` 响应只表示主机收到了这次输入，不代表最终行为判定或提醒结果；真正状态以后续 `get_current_state` / `push_state` / `push_alert` 为准。

3. 历史和反馈
   - 实现 `get_session_history` 分页。
   - 实现 `get_last_report`。
   - 实现 `session_feedback`，feedback 使用 `too_much / too_little / just_right`。

## P0：新增接口适配

1. 摄像头预览
   - 调用 `get_camera_preview_image`。
   - 用作拍照式画面预览，不要做成低帧率视频。
   - 建议 App 侧手动刷新或低频刷新。

2. 预检提示
   - 从 `get_current_state` 或 `get_console_data` 读取 `precheck_guidance`。
   - 展示距离/姿态提示。
   - UI 文案需要有防抖，不要提示频繁跳变。

3. 云台状态与控制
   - 云台属于会话 precheck 主流程的一部分，不是普通用户单独打开的调试功能。
   - 优先读取 `session_control start` 和 `get_current_state` 返回的 `precheck.gimbal`，决定当前预检阶段是否展示云台校准内容。
   - 如果 `precheck.gimbal` 不存在，不要因为 `get_gimbal_status` 有诊断数据就强行展示云台校准页。
   - 调用 `get_gimbal_status` 展示云台在线、安全、归位、角度、错误信息。
   - 调用 `gimbal_control` 支持预检阶段用户手动小步调整。
   - 云台控制区只应在 `precheck.gimbal` 存在时显示；正常情况下用户不会看到 `Gimbal not ready for motion`。如果因状态变化导致运动类 `gimbal_control` 返回该错误，不要卡住主流程，隐藏或禁用云台控制区并保留摄像头预检。
   - UI 提供上下左右/归中/停止按钮。
   - 小步默认 3 度，最大不超过 8 度。
   - 普通用户主流程优先使用 `nudge_* / move_xy / set_xy / stop`；`ver/ping/stat/fault/angle` 如保留，应放入调试/诊断入口。

4. 提醒输出路由
   - 需要适配两个独立路由：
     - `screen_output_route`: `board / phone`
     - `sound_output_route`: `board / phone`
   - 当前开发机可默认 `board`。
   - 屏幕不可用时把 `screen_output_route` 设为 `phone`，App 承接主展示、预检画面、报告展示和屏幕类提醒。
   - 声卡不可用时把 `sound_output_route` 设为 `phone`，App 承接提示音播放。
   - 这两个字段是主机硬件旁路配置：设为 `phone` 后主机跳过对应板载硬件调用，避免驱动异常卡住主流程。
   - 它们不是普通用户提醒偏好；普通用户设置页不要暴露成随手切换项。

5. 提醒渠道配置
   - normal 模式可选：屏幕、手环、声音，默认全选。
   - silent 模式可选：屏幕、手环，默认全选。
   - silent 不提供声音选项，或即使展示也要解释为不可用。
   - App 调 `update_preferences` 时可传 `normal_channels` / `silent_channels` 字符串数组；不建议普通 UI 直接传 mask。
   - `normal_channels_mask` / `silent_channels_mask` 是兼容字段；如果和数组同时传，主机优先使用 mask。
   - 未传字段保持主机原配置，不要在局部设置更新时把其他配置重置成 App 本地默认。
   - normal/silent 只筛渠道，不改变提醒等级。

6. 声音音量
   - 设置页增加 `sound_volume`。
   - 范围 0..1，可展示为 0..100%。
   - 当 `sound_output_route=phone` 且 `push_alert.phone_actions` 包含 `SOUND` 时，App 按该音量播放提示音。

7. 云台禁用
   - 设置页增加 `gimbal_enabled`。
   - 当为 `false` 时，App 隐藏或禁用云台手动控制入口；主机会把云台状态标记为 disabled。

## P1：手机作为主展示屏

1. 状态页
   - 展示 `focus_state`、`session_phase`、`remaining_ms`、`elapsed_ms`、`reminder`。
   - 主机小屏不可用时，App 就是主展示屏，不需要等待板载屏幕失败后才接管。

2. 提醒展示
   - 处理 `push_alert`。
   - 若 `phone_actions` 包含 `SCREEN`，App 弹出视觉提醒。
   - 若 `phone_actions` 包含 `SOUND`，App 播放短提示音，不做语音播报。
   - App 不自行决定提醒等级，只展示主机给的 `level` 和 `message`。

3. 设备能力展示
   - 展示 `board_screen_available`、`board_sound_available`、`bracelet_connected`。
   - 展示 `output_capability` 中的 `screen_output_route`、`sound_output_route`、`band_available`、`last_attempted_channels`、`last_delivered_channels`。
   - 注意 `board_screen_available` / `board_sound_available` 是当前路由下板载输出有效性；route 为 `phone` 时为 false 是旁路行为，不代表硬件损坏。
   - 展示 `last_attempted_channels` / `last_delivered_channels`，便于调试提醒是否实际发出。

## P2：体验优化

1. 默认专注时间
   - 设置页增加默认专注时间。
   - App 首页刚启动或会话结束后使用主机 `default_focus_duration_min`。
   - 首页临时调节只影响本次 start 的 `target_duration_min`。
   - App 不要把首页临时调节写入 `update_preferences.default_focus_duration_min`，除非用户明确在设置页修改默认时长。

2. 中文化
   - 会话状态、提醒、云台控制、历史报告、设置页都需要中文化。
   - 不要把主机内部英文枚举直接暴露给用户。

3. 错误处理
   - BLE 断连后重连并重新订阅 RX Notify。
   - 命令超时要显示可重试状态。
   - 对 `status:error` 展示简短中文原因。

4. Demo 支持
   - Demo HTTP 后台不进 App。
   - App 只做真实用户入口。

5. 用户体验
   - APP在前台且启动专注会话时不自动锁屏
   - App在专注会话有一段时间没有碰手机会自动黑屏保护用户手机屏幕寿命（时间可由用户设置）


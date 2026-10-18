EdgeFocus UI 数据展示需求清单
这份文档整理了目前 C++ 后端 SystemStateStore 中能提供给 UI 层渲染的所有数据条目（Data Elements）。当您重新设计 UI 时（例如使用 Figma 制作新的原型，或开发新的 Web/Qt/LVGL 界面），可以参考此列表来安排界面组件。

整个系统的数据统一打包在 SystemSnapshot 结构体中，UI 线程可以通过无锁的 snapshot() 方法随时获取最新数据。

1. 顶栏/设备状态 (Device & System Info)
通常放置在屏幕边缘或顶部状态栏，用于提示硬件基础情况。

运行模式 (System Mode)
值域: NORMAL (普通), SILENT (静音), HOME (严格)
UI 建议: 可使用图标或文字标签。静音模式可显示静音图标，提醒用户此时没有声光强打扰。
摄像头状态 (Camera Status)
值域: 正常 (true) / 异常或掉线 (false)
UI 建议: 发生异常时需显示红色的错误警告图标。
手环连接状态 (Bracelet Status)
值域: 已连接 (true) / 未连接 (false)
电池电量 (Battery)
值域: 0 - 100 (如果未接入电池读取功能则为 -1)
2. 核心状态展示区 (Core Focus State)
这是首页最核心的元素（例如一个大的呼吸圆环、状态卡片或动画人物）。

当前专注状态 (Focus State)
值域:
FOCUSED (专注中)
LOOKING_AWAY (视线离开)
SLEEPY_OR_SLUMPED (疲劳/趴桌)
PHONE_IN_USE (发现手机)
AWAY (离座)
LOW_CONFIDENCE (画质低/算法不确定)
PAUSED (已暂停)
CALIBRATING (初始校准中)
UI 建议: 强烈建议为每个状态分配不同的主题色（如专注=绿色/蓝色，视线离开=黄色，玩手机=红色），并辅以相应的图标或动效。
状态附加说明 (Focus Reason)
内容: 比如在检测到趴桌时，后端传出的文字为“检测到身体过度前倾”。
UI 建议: 作为状态标题下方的副标题。
画面质量提示 (Frame Quality Warning)
内容: 0.0~1.0的质量评分，以及具体的异常原因（如 blur detected / too dark），以及是否需要用户介入 (guidance_needed)。
UI 建议: 当 guidance_needed == true 时，应在主页悬浮提示用户“请调整台灯光线”或“请擦拭摄像头”。
3. 当前学习会话 (Current Session)
用于在主页显示当前这一次学习的表现。

会话进行状态: 是否正在打卡/学习中 (session.active)
会话已进行时长: 单位为毫秒，UI 通常格式化为 HH:MM:SS 形式的计时器。
本次会话分心次数: 累计违规的次数（计数器）。
本次会话被提醒次数: 累计被系统干预警告的次数（计数器）。
UI 建议: 时长用大号数字居中显示，分心和提醒次数可以作为两个并排的小统计卡片。
4. 今日整体统计 (Today's Stats)
用于第二页（StatsPage）或历史统计数据页展示。

今日累计学习总时长: （秒）
今日累计专注时长: （秒）-> 可以和总时长计算出今日的“专注率”。
今日学习次数: 进行过几次有效的 Session。
今日分心事件总数: 整个白天发生的违规总和。
5. 弹窗与提醒交互 (Active Interventions)
当用户发生违规时，后端会下发提醒。UI 需要将其实时展示给用户。

提醒等级 (Reminder Level)
值域: LEVEL_1 (轻度), LEVEL_2 (警告), LEVEL_3 (强力干预), REST_SUGGESTION (建议休息)。
UI 建议: LEVEL_1 可以是底部浮现的小 Toast；LEVEL_2/3 必须是覆盖在屏幕中央醒目的模态弹窗 (Modal) 或闪烁全屏边框。
提醒文案 (Message)
内容: 后端传来的具体干预文字（如“学习期间请放下手机”）。
提醒发生时间: 用于计算这个弹窗已经弹了多久。
消警操作 (Acknowledge Action)
UI 建议: 弹窗上必须提供一个“我知道了”或“滑块解锁”的交互区域，点击后调用后端的消警逻辑（关闭弹窗并触发冷却期）。
6. 复盘报告 (Session Review / Last Report)
当用户结束一段 Session 后弹出的结算界面。

报告数据: 包含刚才这段学习的 总时长, 专注时长, 分心次数, 被提醒次数 等。
UI 建议: 做一个庆祝动画，展示这段学习的专注得分（如 专注时长 / 总时长 * 100）。
7. 页面导航预留 (Navigation)
如果您要重做底部或侧边的导航栏，系统目前支持以下页面的切换：

主状态页 (Status)：显示核心圆环和倒计时。
统计数据页 (Stats)：显示今天/历史的数据图表。
复盘报告页 (Review)：单次学习结束的结算页。
校准页 (Calibration)：引导用户摆放摄像头位置，查看摄像头画面的页面。
设置页 (Settings)：调节音量、切换模式（正常/静音）。
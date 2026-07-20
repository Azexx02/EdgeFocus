# G431 云台文档导航

当前产品基线为协议 3.0：永久机械原点、A/B Flash 事务槽、异步运动 generation、POSE settled、电流遥测/保护和可中断维护校准。

1. [协议 3.0 命令速查](uart_command_table.md)
2. [接线、板级信号与主控适配](g431_board_control_signals.md)
3. 历史协议文档仅供旧固件诊断，不得用于国赛版运动控制。

国赛版主机面对协议 2.0 固件只做只读诊断并固定相机降级。任何修改状态命令都要求先执行 `HELLO HOST PROTO=3.0`。

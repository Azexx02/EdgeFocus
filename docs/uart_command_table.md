# STM32G431 云台串口命令表

串口：COM28，115200，8N1，3.3 V TTL，无流控。  
每条文本命令必须以 `\r\n` 或 `\n` 结束。

## 开机动作

上电约 800 ms 后先执行一次低功率电角度对齐，随后 X 轴使用编码器位置闭环 FOC 寻找两侧机械限位，最后回到标定中心。若启动时已经靠近第一侧端点，固件会自动改为先探测另一侧，再反向扫描完整行程。Y 轴连续旋转，不参与开机撞限位。

X 轴 AS5600 原始计数行程不要求等于 180°；固件用实测的两个端点按比例映射为逻辑坐标 `0..180°`。Flash 中端点距离不足、校验和错误或版本不匹配时，`CALLOAD` 会拒绝数据。

`MOVE`、`AUTOHOME`、自动回中和 `HOLD` 均使用位置闭环 FOC；不再使用开环六步换相执行机械定位。

```text
BOOT_HOME,X_START
BOOT_HOME,X_OK
```

出现 `ERR,BOOT_HOME_X` 时不得继续运动。寻限位期间只允许单字节 `!` 急停。

长时间运动或寻限位期间串口不会停止服务。`PING`、`STAT`、`ANGLE`、`FAULT`、`FOCSTAT`、`HOMEINFO`、`STOP` 可正常使用；新的运动或配置命令返回 `ERR,BUSY`。

## 业务命令

| 命令 | 参数 | 功能 | 典型最终响应 |
|---|---|---|---|
| `VER` | 无 | 查询协议版本和能力 | `VER,PROTO=1.0,...,CAPS=...|FOC_HOLD` |
| `PING` | 无 | 通信测试 | `OK,G431` |
| `STOP` | 无 | 关闭电机、Boost 和保持 | `OK,STOP` |
| `STAT` | 无 | 查询运行与标定状态 | `STAT,SAFE=0,...` |
| `ANGLE` | 无 | 查询原始值和角度 | `ANGLE,X=...,Y=...,XD=...,YD=...` |
| `FAULT` | 无 | 查询故障和限位事件 | `FAULT,...,LIMIT=NONE,LRAW=65535` |
| `FOCSTAT` | 无 | 查询 FOC 对齐、误差、输出和看门狗 | `FOCSTAT,EN=...,XALIGN=...,...` |
| `SAFE` | 无 | 立即关闭电机、Boost、保持和故障输出 | `OK,SAFE` |
| `LIMIT` | 无 | 查询逻辑角度范围 | `LIMIT,X=180,Y=359` |
| `HOMEINFO` | 无 | 查询两轴端点、中心和逻辑行程 | `HOMEINFO,XOK=...,...` |
| `SET XY x y` | X：`0..180`，Y：`0..359` | 双轴绝对定位，固定先 X 后 Y | `SET,XY_OK,X=...,Y=...,XR=...,YR=...` |
| `MOVE XY x y speed` | 速度：`1..90` 度/秒 | 双轴分段轨迹运动 | `MOVE,XY_OK,X=...,Y=...,XR=...,YR=...` |
| `DEG X x` | `0..180` | X 轴绝对定位 | `DEG,X=...,RAW=...,TARGET=...` |
| `DEG Y y` | `0..359` | Y 轴绝对定位 | `DEG,Y=...,RAW=...,TARGET=...` |
| `CENTER X` | 无 | X 轴回到标定中心 90° | `CENTER,X_OK,DEG=90,RAW=...` |
| `HOLD 1` | 无 | 保存当前位置并启用双轴 FOC 保持 | `OK,HOLD_ON,X=...,Y=...` |
| `HOLD 0` | 无 | 关闭 FOC 保持和电机输出 | `OK,HOLD_OFF` |

示例：

```text
VER\r\n
PING\r\n
SET XY 90 0\r\n
MOVE XY 120 350 20\r\n
STOP\r\n
```

## 运动响应

运动命令先返回开始行，再返回成功或错误行：

```text
SET,XY_START,X=90,Y=0,MODE=0
SET,XY_OK,X=90,Y=0,XR=3136,YR=2416
```

同一时刻只能执行一条运动命令。收到最终响应前不要发送下一条普通命令。

定位成功后保持当前位置：

```text
SET XY 90 0
HOLD 1
```

`HOLD 1` 会把命令执行时的当前 X/Y 角度保存为本次保持点。保持期间 `STAT` 应显示 `BOOST=1,MOTOR=1,HOLD=1`。关闭保持使用 `HOLD 0`、`STOP` 或单字节 `!`。首次 `HOLD 1` 可能需要电角度对齐，最多等待 8 秒。

只有 `ANGLE` 中 `XD/YD` 均不是 `65535`，并且 `STAT` 同时满足 `XHOME=1,YHOME=1,CAL=1` 时，才允许执行 `HOLD 1`。

保持期间不能直接发送新的定位命令，否则返回 `ERR,HOLD_ACTIVE`。应按 `HOLD 0`、新定位、`HOLD 1` 的顺序改变目标。

保持诊断使用：

```text
FOCSTAT
```

重点字段：`XALIGN/YALIGN` 为电角度是否就绪，`XE/YE` 为原始位置误差，`XDUTY/YDUTY` 为输出幅值，`WD` 为看门狗触发次数，`UARTERR` 为串口错误恢复次数。

AS5600 为绝对编码器。保持期间检测到约 2°位置偏差时，固件会立即提高到最大恢复幅值并回拉保存位置，不会累积传统步进式“丢步”误差。偏差达到约 45°并持续 3 秒仍无法恢复时才安全退出。

## 紧急停止

运动期间可发送单字节：

```text
!
```

`!` 不需要换行。随后发送：

```text
STOP
FAULT
STAT
```

强电磁干扰环境下可以连续发送 3 个 `!`。查询命令超时、响应损坏或意外收到 `ERR,UNKNOWN_CMD` 时可重试 1 次；运动命令不得自动重试。

## 限位与堵转提示

X 轴触碰限位或被阻挡后会立即关闭驱动，并返回：

```text
ERR,X_DEG,E=12,...,LIMIT=X_MIN,LRAW=<raw>
ERR,X_DEG,E=13,...,LIMIT=X_MAX,LRAW=<raw>
ERR,X_DEG,E=14,...,LIMIT=X_BLOCKED,LRAW=<raw>
```

| 错误码 | `LIMIT` | 含义 |
|---:|---|---|
| `12` | `X_MIN` | X 轴触碰最小机械限位 |
| `13` | `X_MAX` | X 轴触碰最大机械限位 |
| `14` | `X_BLOCKED` | X 轴在中间位置堵转 |

`LRAW=65535` 表示没有有效的限位触发位置。Y 轴可连续旋转，不进行机械端点判定。

## 常用错误

| 响应 | 含义 |
|---|---|
| `ERR,UNKNOWN_CMD` | 命令不存在 |
| `ERR,BAD_SET_XY` | `SET XY` 格式或范围错误 |
| `ERR,BAD_MOVE_XY` | `MOVE XY` 格式或范围错误 |
| `ERR,BAD_DEGREE` | 单轴角度格式或范围错误 |
| `ERR,NOT_HOMED` | 标定无效 |
| `ERR,ANGLE_READ` | 编码器读取失败 |
| `ERR,CALLOAD,EMPTY_OR_INVALID` | Flash 标定为空、损坏或端点距离无效 |
| `ERR,LINE_TOO_LONG` | 命令超过 63 字节；固件丢弃到下一换行符 |
| `ERR,HOLD_ACTIVE` | 保持开启，必须先执行 `HOLD 0` |
| `ERR,...,E=6` | 运动超出轨迹保护窗口 |
| `ERR,...,E=9` | 定位过程中收到 `!` |
| `ERR,...,E=10` | 驱动故障或保护触发 |
| `ERR,...,E=11` | 达到最大步数仍未收敛 |

收到运动错误后必须发送 `STOP`，再查询 `FAULT` 和 `ANGLE`，不得自动重复原运动命令。

## 启动顺序

```text
等待 BOOT_HOME,X_OK
VER
PING
STOP
STAT
FAULT
ANGLE
```

只有 `STAT` 返回以下条件时才允许运动：

```text
SAFE=0,XHOME=1,YHOME=1,CAL=1
```

## 标定与维护命令

以下命令只允许生产标定或人工调试使用：

| 命令 | 功能 |
|---|---|
| `HELP` | 查询命令列表 |
| `ECHO text` | 串口回显 |
| `SAFE` | 维护模式安全关闭 |
| `LIMIT` | 查询角度范围 |
| `HOMEINFO` | 查询标定数据 |
| `MARK X MIN` | 记录当前位置为 X 最小端 |
| `MARK X MAX` | 记录当前位置为 X 最大端 |
| `MARKRAW X MIN n` | 写入 X 最小端原始值 |
| `MARKRAW X MAX n` | 写入 X 最大端原始值 |
| `CALSAVE` | 保存标定到 Flash |
| `CALLOAD` | 从 Flash 加载标定 |
| `CALCLEAR` | 清除标定 |
| `HOME X/Y/ALL` | 设置临时原点 |
| `AUTOHOME`、`AUTOHOME X/Y` | 自动寻限位调试 |
| `POS X/Y n` | 按编码器原始值定位 |
| `TEST X/Y` | FOC 小角度往返测试 |
| `BOOST 1/0` | 手动控制 Boost |
| `MOTOR 1/0` | 手动控制驱动使能 |

正常主程序不得调用维护命令。完整协议约束见
[gimbal_uart_control_protocol.md](gimbal_uart_control_protocol.md)。

`AUTOHOME X` 成功后应依次执行 `HOMEINFO`、`ANGLE`、`CALSAVE`。只有
`XOK=1`、`XTR=180` 且当前 `XD` 有效时才允许保存。保存后可用
`CALLOAD` 和 `STAT` 回读确认 `CAL=1`。

SSCOM 若不能自动添加换行，可勾选十六进制发送。例如 `PING\r\n` 对应：

```text
50 49 4E 47 0D 0A
```

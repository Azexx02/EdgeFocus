# 云台外部串口控制协议

协议版本：V1.0  
适用固件：STM32G431 云台控制板  
文档性质：主程序与云台固件之间的最终接口约束

## 1. 固定硬件参数

| 项目 | 约束 |
|---|---|
| UART | UART4，PC10 TX / PC11 RX |
| 电平 | 3.3 V TTL，禁止直接连接 RS232 |
| 串口格式 | 115200，8N1，无流控 |
| 编码 | ASCII |
| 命令结束符 | `\r\n` 或 `\n` |
| 最大命令长度 | 63 字节，不含结束符 |
| X 轴坐标 | `0..180` 度，有机械限位 |
| Y 轴坐标 | `0..359` 度，可连续旋转 |

X 轴大角度运动由 G431 固件自动拆分为不超过 5 度的闭环段，主程序不得自行拆段。Y 轴跨越 0 度时按最短角差运动。

X 轴逻辑 `0..180°` 由自动标定得到的两个 AS5600 原始端点按比例映射，不要求原始计数跨度等于 2048。无效或过短的端点记录不得置位 `XHOME/CAL`。

## 2. 通信规则

1. 命令和参数区分大小写，必须使用本文规定的大写形式。
2. 除紧急停止字符 `!` 外，同一时刻只允许存在一条未完成命令。
3. 收到运动命令的 `*_START` 后，必须等待 `*_OK` 或 `ERR,...` 才能发送下一条普通命令。
4. 固件不主动周期上报，主程序使用查询命令读取状态。
5. 所有角度和速度均为十进制无符号整数。
6. V1 已定义字段的含义和顺序不得修改。固件升级只能在行尾追加字段。
7. 主程序解析响应时应允许行尾出现未知新增字段。

## 3. V1 业务命令

主程序只依赖以下命令：

```text
VER
PING
STOP
STAT
ANGLE
FAULT
FOCSTAT
SET XY <x_deg> <y_deg>
MOVE XY <x_deg> <y_deg> <speed>
DEG X <x_deg>
DEG Y <y_deg>
CENTER X
HOLD 1
HOLD 0
```

`TRACK` 不属于 V1 接口，主程序不得调用。

## 4. 版本与联机

### 4.0 上电 X 轴寻限位

固件上电约 800 ms 后自动执行一次 X 轴寻限位：

1. 若 Flash 中已有且有效的标定，先加载端点比例；
2. 向 X 一侧低速运行并检测机械堵转；
3. 反向运行并检测另一侧机械堵转；
4. 按两个原始端点的实际跨度重新建立逻辑 `0..180°` 坐标；
5. 回到测得行程中心。

Y 轴为连续轴，不执行开机撞限位。事件响应：

```text
BOOT_HOME,X_START
BOOT_HOME,X_OK
ERR,BOOT_HOME_X
```

寻限位期间只允许发送单字节 `!` 急停。主程序必须等待 `BOOT_HOME,X_OK`，或在收到错误后进入 `FAULT` 状态，不得在开机寻限位期间发送普通运动命令。

### 4.1 VER

请求：

```text
VER
```

固定响应格式：

```text
VER,PROTO=1.0,FW=G431-GIMBAL,CAPS=STOP|SET_XY|MOVE_XY|FAULT|X_SEGMENT|Y_CONTINUOUS|LIMIT_PROTECT|FOC_HOLD|FOCSTAT
```

主程序必须检查 `PROTO` 的主版本。主版本不是 `1` 时不得进入运动状态。

### 4.2 PING

请求与响应：

```text
PING
OK,G431
```

超时为 500 ms，连续 3 次失败后判定云台离线。

## 5. 停止

### 5.1 普通停止

```text
STOP
```

成功响应：

```text
OK,STOP
```

`STOP` 关闭电机驱动、Boost 和保持输出，并恢复空闲 DAC。空闲或故障状态下重复发送也必须成功。

### 5.2 运动中紧急停止

运动期间发送单字节：

```text
!
```

`!` 不需要换行，用于打断正在执行的运动或 FOC 保持。强干扰环境下可连续发送 3 个 `!`。停止后主程序应发送 `STOP`，再通过 `STAT` 和 `FAULT` 确认状态。若 1 秒内没有任何响应，上级系统应切断电机电源使能或复位 G431。

## 6. 双轴控制

### 6.1 SET XY

将双轴移动到指定绝对角度：

```text
SET XY <x_deg> <y_deg>
```

参数：

| 参数 | 范围 | 单位 |
|---|---:|---|
| `x_deg` | `0..180` | 度 |
| `y_deg` | `0..359` | 度 |

开始响应：

```text
SET,XY_START,X=<x_deg>,Y=<y_deg>,MODE=0
```

成功响应：

```text
SET,XY_OK,X=<actual_x>,Y=<actual_y>,XR=<x_raw>,YR=<y_raw>
```

失败响应：

```text
ERR,SET_X,E=<code>,LIMIT=<type>,RAW=<raw>
ERR,SET_Y,E=<code>,LIMIT=<type>,RAW=<raw>
```

V1 的执行顺序固定为 X 后 Y。命令不是原子操作：Y 失败时 X 可能已到目标位置。任一轴失败后固件立即进入安全停止状态。

示例：

```text
SET XY 90 0
```

### 6.2 MOVE XY

按分段轨迹移动双轴：

```text
MOVE XY <x_deg> <y_deg> <speed>
```

参数：

| 参数 | 范围 | 单位 |
|---|---:|---|
| `x_deg` | `0..180` | 度 |
| `y_deg` | `0..359` | 度 |
| `speed` | `1..90` | 度/秒 |

开始响应：

```text
MOVE,XY_START,X=<x_deg>,Y=<y_deg>,SPEED=<speed>
```

成功响应：

```text
MOVE,XY_OK,X=<actual_x>,Y=<actual_y>,XR=<x_raw>,YR=<y_raw>
```

失败响应：

```text
ERR,MOVE_X,E=<code>,LIMIT=<type>,RAW=<raw>
ERR,MOVE_Y,E=<code>,LIMIT=<type>,RAW=<raw>
```

轨迹被拆为最大 10 度的双轴插补点，每个插补点固定先更新 X 再更新 Y。`speed` 是轨迹段的最大请求速度，受电机负载、闭环收敛时间和保护逻辑限制，不保证严格恒速。Y 轴采用最短圆周方向。

示例：

```text
MOVE XY 120 350 20
```

### 6.3 单轴绝对角度

```text
DEG X <0..180>
DEG Y <0..359>
```

开始响应：

```text
DEG,X_START,TARGET=120,RAW=2450,LIMIT=180
```

成功响应：

```text
DEG,X=120,RAW=2452,TARGET=120
```

失败响应：

```text
ERR,X_DEG,E=<code>,DEG=<actual>,RAW=<raw>,LIMIT=<type>,LRAW=<raw>
```

X 轴分段由固件内部完成。主程序发送最终目标即可。

### 6.4 X 轴回中

```text
CENTER X
```

成功响应：

```text
CENTER,X_OK,DEG=90,RAW=<raw>
```

Y 轴为连续轴，没有业务层 `CENTER Y`。

### 6.5 FOC 位置保持

启用双轴位置保持：

```text
HOLD 1
```

成功响应：

```text
OK,HOLD_ON,X=<captured_x>,Y=<captured_y>
```

关闭保持并断开电机输出：

```text
HOLD 0
```

响应：

```text
OK,HOLD_OFF
```

`HOLD 1` 读取并保存命令执行时的当前 X/Y 绝对角度，将其作为本次保持点。因此既可以在定位成功后开启，也可以直接在人工摆放位置开启。保持点保存在 RAM，复位后失效；每次从关闭状态重新执行 `HOLD 1` 都会重新捕获当前位置。复位后的首次启用会执行专用电角度零点与方向对齐，轴可能短暂移动，随后回到保存点；响应时间最长 8 秒。

保持开启时发送新的定位或标定命令会返回 `ERR,HOLD_ACTIVE`。改变目标前必须先执行 `HOLD 0`，完成新定位后再执行 `HOLD 1`。

当前硬件没有三相电流采样，采用 AS5600 位置环、电角度换算和正弦电压矢量的电压模式 FOC，控制周期为 10 ms。保持期间 `STAT` 应显示 `BOOST=1,MOTOR=1,HOLD=1`。

控制循环设有独立 100 ms 输出看门狗；位置持续偏离、编码器异常或主循环失去心跳时会关闭 PWM 和 Boost。`STOP`、`HOLD 0` 和单字节 `!` 均可结束保持，其中 `!` 优先级最高。

外力造成约 2°偏差时进入最大回拉。偏差达到约 45°并持续 3 秒仍无法恢复时，固件判定失控并安全退出；正常手推后松手会继续回到保存的保持点。

FOC 诊断：

```text
FOCSTAT
```

响应：

```text
FOCSTAT,EN=<0|1>,XALIGN=<0|1>,YALIGN=<0|1>,XE=<raw>,YE=<raw>,XDUTY=<ticks>,YDUTY=<ticks>,CYC=<count>,WD=<count>,UARTERR=<count>,PERIOD=10
```

`XALIGN/YALIGN` 表示电角度映射是否就绪；`XE/YE` 是位置误差原始计数；`XDUTY/YDUTY` 是当前正弦矢量幅值；`CYC` 是本次保持控制循环数；`WD` 是上电后的 FOC 看门狗触发次数；`UARTERR` 是 UART 错误恢复次数。

## 7. 状态查询

### 7.1 STAT

```text
STAT
```

响应：

```text
STAT,SAFE=0,BOOST=0,MOTOR=0,HOLD=0,XHOME=1,YHOME=1,CAL=1,XT=90,YT=0,EN=0,IX=0,IY=0,XLIM=180,YLIM=359
```

| 字段 | 含义 |
|---|---|
| `SAFE` | `1` 表示固件编译为控制禁用模式 |
| `BOOST/MOTOR/HOLD` | 对应输出状态 |
| `XHOME/YHOME` | 两轴 RAM 标定是否有效 |
| `CAL` | Flash 标定是否有效 |
| `XT/YT` | 最近一次目标角度 |
| `EN` | 外部使能输入状态 |
| `IX/IY` | 两轴电流 ADC 原始值 |
| `XLIM/YLIM` | 两轴角度上限 |

只有 `SAFE=0,XHOME=1,YHOME=1,CAL=1` 时才允许发送运动命令。

### 7.2 ANGLE

```text
ANGLE
```

响应：

```text
ANGLE,X=<x_raw>,Y=<y_raw>,XD=<x_deg>,YD=<y_deg>,XHOME=<raw>,YHOME=<raw>,EN=<0|1>
```

`X/Y` 范围为 `0..4095`。`XD/YD=65535` 表示编码器读取失败或坐标无效。

### 7.3 FAULT

```text
FAULT
```

响应：

```text
FAULT,XHOME_E=<code>,YHOME_E=<code>,XPOS_E=<code>,YPOS_E=<code>,IX=<raw>,IY=<raw>,LIMIT=<type>,LRAW=<raw>
```

错误码为 `0` 表示对应模块当前没有记录到错误。`LIMIT` 可取 `NONE`、`X_MIN`、`X_MAX` 或 `X_BLOCKED`，`LRAW` 是触发保护时的 X 编码器值；`65535` 表示没有有效的触发位置。`FAULT` 是查询，不会自动清除或重新驱动电机。

X 轴闭环运动中若编码器连续约 280 ms 几乎没有位移，固件立即关闭该轴输出。触发位置在端点 3 度范围内时报告 `X_MIN` 或 `X_MAX`，其余位置报告 `X_BLOCKED`。Y 轴无机械限位，不执行端点碰撞判定。

## 8. 错误响应

通用错误：

| 响应 | 含义 |
|---|---|
| `ERR,UNKNOWN_CMD` | 未定义命令 |
| `ERR,BAD_SET_XY` | `SET XY` 格式或范围错误 |
| `ERR,BAD_MOVE_XY` | `MOVE XY` 格式或范围错误 |
| `ERR,BAD_DEGREE` | 单轴角度格式或范围错误 |
| `ERR,NOT_HOMED` | 至少一轴没有有效标定 |
| `ERR,ANGLE_READ` | 当前角度无法读取或转换 |
| `ERR,CONTROL_DISABLED` | 固件编译为控制禁用模式 |
| `ERR,HOLD_ACTIVE` | FOC 保持开启，必须先发送 `HOLD 0` |

定位错误码：

| E | 含义 |
|---:|---|
| 1 | 编码器读取失败或目标无效 |
| 2 | 驱动启动失败 |
| 3 | 初始换相后编码器读取失败 |
| 4 | 校准期间收到急停 |
| 5 | 校准期间编码器读取失败 |
| 6 | 运动超出保护窗口 |
| 7 | 校准结束编码器读取失败 |
| 8 | 无法识别换相方向 |
| 9 | 定位过程中收到急停 |
| 10 | 编码器、限位、驱动故障脚或电流保护触发 |
| 11 | 达到最大控制步数仍未收敛 |
| 12 | X 轴触碰最小机械限位，`LIMIT=X_MIN` |
| 13 | X 轴触碰最大机械限位，`LIMIT=X_MAX` |
| 14 | X 轴中途堵转或被异物阻挡，`LIMIT=X_BLOCKED` |

收到任何运动错误后，主程序必须进入 `FAULT` 状态，发送 `STOP`，查询 `FAULT` 和 `ANGLE`，不得自动原样重发运动命令。

## 9. 超时约束

| 命令 | 主程序超时 |
|---|---:|
| `VER/PING/STOP/STAT/ANGLE/FAULT/FOCSTAT` | 500 ms |
| `DEG Y` | 10 s |
| `DEG X` | 30 s |
| `CENTER X` | 30 s |
| `SET XY` | 45 s |
| `MOVE XY` | 60 s |
| `HOLD 1` | 8 s |
| `HOLD 0` | 500 ms |

查询命令超时、响应损坏或意外收到 `ERR,UNKNOWN_CMD` 后可重试 1 次。运动命令不得自动重试，因为首次命令可能已经产生运动。运动超时后连续发送最多 3 个 `!`，等待 200 ms，再发送 `STOP`。

## 10. 主程序状态机

| 状态 | 进入条件 | 允许操作 |
|---|---|---|
| `OFFLINE` | 未通过 `VER` 和 `PING` | `VER`、`PING` |
| `SAFE` | 已联机但标定未确认 | 查询命令、`STOP` |
| `READY` | `SAFE=0,XHOME=1,YHOME=1,CAL=1` | 所有 V1 业务命令 |
| `MOVING` | 收到 `*_START` | 等待最终响应，仅允许 `!` |
| `HOLDING` | 收到 `OK,HOLD_ON` | 查询命令、`HOLD 0`、`STOP`、`!` |
| `FAULT` | 收到 `ERR` 或运动超时 | `!`、`STOP`、查询命令 |

固定启动流程：

```text
等待 BOOT_HOME,X_OK
VER
PING
STOP
STAT
FAULT
ANGLE
```

## 11. 标定与维护接口

以下命令仅供生产标定和现场维护，主程序正常业务不得调用：

```text
HELP
ECHO <text>
SAFE
LIMIT
HOMEINFO
MARK X MIN
MARK X MAX
MARKRAW X MIN <0..4095>
MARKRAW X MAX <0..4095>
CALSAVE
CALLOAD
CALCLEAR
HOME X
HOME Y
HOME ALL
AUTOHOME
AUTOHOME X
AUTOHOME Y
POS X <0..4095>
POS Y <0..4095>
TEST X
TEST Y
BOOST 1
BOOST 0
MOTOR 1
MOTOR 0
```

`AUTOHOME Y` 当前不可用于产品流程。`HOLD 1/0` 已列入 V1 业务命令，因此不属于维护命令。

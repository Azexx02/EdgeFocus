# 板端环境确认 (W00)

本文档汇总当前 ATK-DLMP257B 开发板的已知环境事实，供后续所有 agent 参考。

## 1. 硬件标识

| 项目 | 值 |
|---|---|
| 核心板 | ATK-CLMP257B |
| 底板 | ATK-DLMP257B |
| 主控 | STM32MP2 (Cortex-A35 + M33) |
| 架构 | aarch64 |

## 2. 网络

| 项目 | 值 |
|---|---|
| 推荐以太网口 | ETH2 (底板常规网口) |
| Linux 接口名 | `end0` |
| 最近已验证 IP | `192.168.10.210` (DHCP, 不可假设重启后不变) |
| switch 侧接口 | `sw0ep` (仅用于交换功能测试，不作为默认开发口) |
| 串口 | COM7, 115200 |
| 串口适配器 | USB-Enhanced-SERIAL-A CH342 |

### 网络恢复方法

1. SSH 到 `end0` (优先)。
2. 网络不通时用 COM7 串口登录。
3. 串口临时恢复命令：
   ```sh
   ip addr flush dev sw0ep
   ip link set sw0ep up
   udhcpc -i sw0ep
   ```
4. 持久化方案已配置：`sw0ep` 有 `/etc/systemd/network/10-sw0ep.network` 支持 DHCP。
5. 推荐将网线插到 ETH2 (end0)，由系统默认 `80-wired.network` 管理 DHCP。

## 3. 登录

| 项目 | 值 |
|---|---|
| 用户 | root |
| 板端 OS | ST OpenSTLinux - Weston 5.0.3-snapshot-20241210 (scarthgap) |
| 内核 | 6.6.48 |

## 4. 显示

| 状态 | DTB | 说明 |
|---|---|---|
| 无显示 (默认) | `stm32mp257d-atk-ddr-2GB.dtb` | 显示控制器禁用, 无 `/dev/dri/card0` |
| MIPI 可用 | `stm32mp257d-atk-ddr-2GB-mipi.dtb` | `/dev/dri/card0` 出现, Weston 正常 |
| RGB | `stm32mp257d-atk-ddr-2GB-rgb.dtb` | 未测试 |
| LVDS 单链路 | `stm32mp257d-atk-ddr-2GB-lvds-1xSingleLink.dtb` | 未测试 |
| LVDS 双单链路 | `stm32mp257d-atk-ddr-2GB-lvds-2xSingleLink.dtb` | 未测试 |
| LVDS 双链路 | `stm32mp257d-atk-ddr-2GB-lvds-dualLink.dtb` | 未测试 |

MIPI 启用后确认信息：
- 输出头：`DSI-1`
- 分辨率：`720x1280@55.8`
- 背光设备：`/sys/class/backlight/panel-dsi-backlight`

### 远程桌面

当前镜像 Weston RDP 后端缺失 (`rdp-backend.so`)，无 `wayvnc` 包。远程桌面不可用。
验证 UI 输出的方式：截图或导出视频。

## 5. 摄像头

| 项目 | 值 |
|---|---|
| USB 摄像头 capture node | `/dev/video7` (必须运行时检测，不可硬编码) |
| USB 摄像头 metadata node | `/dev/video8` |
| 非 capture 设备 (勿用) | `/dev/video4` = `dcmipp_main_isp_params_output` |
| MIPI CSI 接口 | 底板提供，当前未使用 |

### 摄像头检测方法

```sh
v4l2-ctl --list-devices
v4l2-ctl -D -d /dev/video7
```

## 6. 交叉编译

| 项目 | 值 |
|---|---|
| 交叉编译主机 | 见 `开发板部分代码/交叉编译主机信息.txt` |
| SDK 路径 (板端参考) | `/opt/st/stm32mp2/5.0.3-snapshot/` |
| 包管理 | apt/dpkg (板端) |

## 7. 已验证的运行时库 (板端)

- `libstai_mpu.so`
- OpenCV: `libopencv_core.so`, `libopencv_videoio.so`, `libopencv_imgproc.so`, `libopencv_highgui.so`
- 多媒体: `ffmpeg`, `gst-launch-1.0`, `libavcodec.so`, `libavformat.so`, `libx264.so`, `libpng16.so`

## 8. 无屏验证工作流

优先级：
1. SSH 到 `end0`
2. 网络不明时用 COM7 串口
3. 无屏验证用截图或导出视频
4. 不依赖 Weston RDP / VNC

## 9. 已知限制

- RDP/VNC 远程桌面不可用。
- 默认 DTB 不启用显示，需要手动切换到 MIPI DTB。
- ISP 辅助工具 `dcmipp-isp-ctrl` 权限问题 (USB 摄像头路径下不影响)。
- OpenCV/GStreamer 有 `Cannot query video position` 警告，不影响功能。
- `poweroff` 后重启 IP 可能变化，需重新确认。

## 10. 参考资料

- `资料/BOARD_SESSION_NOTES_192.168.10.171.md` — 完整 session 记录
- `资料/ATK-DLMP257B_QUICK_NOTES.md` — 快速参考
- `开发板部分代码/交叉编译主机信息.txt` — 交叉编译主机连接信息

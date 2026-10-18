# 板端部署脚本

正点原子版主机的部署、冒烟测试与诊断脚本。凭据通过环境变量传入。

## 环境变量

| 变量 | 说明 |
| --- | --- |
| `EDGEFOCUS_BOARD_IP` | 板端 IP（默认 `192.168.10.210`） |
| `EDGEFOCUS_BOARD_USER` | 板端 SSH 用户 |
| `EDGEFOCUS_BOARD_PASS` | 板端 SSH 密码 |
| `EDGEFOCUS_BUILD_PASS` | 交叉编译机 SSH 密码 |
| `EDGEFOCUS_WIFI_IFACE` | 无线网卡（默认自动） |
| `EDGEFOCUS_WIFI_SSID` | Wi-Fi SSID |
| `EDGEFOCUS_WIFI_PASSWORD` | Wi-Fi 密码 |
| `EDGEFOCUS_WIFI_ADDRESS` / `GATEWAY` / `DNS` | 静态网络参数 |
| `EDGEFOCUS_DEPLOY_VISION_MODE` | `real` / `mock` / `preview` |

示例：

```powershell
$env:EDGEFOCUS_BOARD_IP='192.168.10.210'
$env:EDGEFOCUS_BOARD_USER='root'
$env:EDGEFOCUS_BOARD_PASS='<your-password>'
$env:EDGEFOCUS_DEPLOY_VISION_MODE='real'
python board_gatt_smoke.py --cmd get_current_state
```

## 脚本

| 脚本 | 用途 |
| --- | --- |
| `board_deploy.py` | 通用 SSH 部署与命令执行 |
| `deploy_integration_test.py` | ARM 集成测试部署 |
| `build_test.py` | 交叉编译与测试构建 |
| `remote_build.py` | 远程编译 |
| `run_board_test.py` | 板端模块测试（storage / FSM / policy / BLE 等） |
| `run_on_board.py` | 在板端运行应用 |
| `start_app.py` | 停止服务并手动启动应用 |
| `board_gatt_smoke.py` | App GATT 冒烟（JSON 命令） |
| `ble_app_smoke.py` | BLE 连接冒烟 |
| `patch_fb.py` | 帧缓冲诊断 |
| `install_wifi_static.py` | 配置 Wi-Fi 自动连接与静态地址 |
| `wifi_connect.py` | 串口登录后手动连接 Wi-Fi |
| `export_logs.sh` | 导出板端日志 |
| `focus_app.service` | systemd 单元模板 |

## 注意

- 部分脚本是一次性现场诊断工具，硬编码了开发网络地址；使用前按实际环境修改或通过环境变量覆盖。
- 不要把密码写回仓库；使用 `export` / `$env:` 或 SSH key。

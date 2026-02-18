# 32mc (`esp32s3_cube3d`)

`32mc` 是一个运行在 **ESP32-S3** 上的轻量 Minecraft 在线客户端。  
它会连接到 Minecraft Java 协议服务端（当前主要面向 `bareiron`），把服务端下发的区块解码为本地体素窗口，并在 ST7735(我使用的) 屏幕上实时渲染。

这个项目的目标不是完整复刻 Java 客户端，而是在极低资源设备上实现可玩类似minecraft的体验。

## 功能特性

- 基于 ESP32-S3 + ST7735 的 3D 渲染
- 支持 Minecraft 登录流程（Handshake/Login/Configuration/Play）
- 解析并应用服务端 `Chunk Data and Update Light`
- 支持移动同步、切换手持物品、破坏方块、放置方块
- 支持远程玩家可视化
- 内置 Web 控制面板，可在浏览器改按键映射和联机参数

## 目录结构

- `src/main.cpp`: 程序入口与主循环

- `src/mc_client.cpp`: 网络协议收发与状态机
- `src/render.cpp`: 体素渲染与 HUD
- `src/controls.cpp`: 输入处理、相机与交互逻辑
- `src/world.cpp`: 本地体素世界与碰撞/射线检测
- `src/web_control.cpp`: WiFi 管理 + Web API/UI
- `include/game_shared.h`: 全局配置常量（WiFi、引脚、默认服务器等）

## 硬件要求

- ESP32-S3（`esp32-s3-devkitc-1`）
- ST7735/ST7735S SPI 屏幕（160x128）

## 引脚定义（默认）

`include/game_shared.h`：

- TFT MOSI: `GPIO18`
- TFT SCLK: `GPIO17`
- TFT CS: `GPIO46`
- TFT DC: `GPIO8`
- TFT RST: `GPIO20`
- TFT BLK: `GPIO9`
- 左按钮: `GPIO39`
- 右按钮: `GPIO40`

如果你的板子接线不同，请修改 `include/game_shared.h`。

## 软件环境

- [PlatformIO](https://platformio.org/)
- 框架: Arduino（见 `platformio.ini`）
- 依赖库:
  - `Adafruit GFX Library`
  - `Adafruit ST7735 and ST7789 Library`

## 快速开始

1. 配置 WiFi 与默认服务器
修改 `include/game_shared.h`：

```cpp
inline constexpr char kWifiSsid[] = "";
inline constexpr char kWifiPass[] = "";
inline constexpr char kMcDefaultHost[] = "192.168.3.144";
inline constexpr uint16_t kMcDefaultPort = 25565;
```

说明：
- 你需要填写自己WIFI的 SSID 和密码后再编译烧录！！！！重要！！！

2. 检查串口配置
根据你的环境调整 `platformio.ini`：

- `upload_port`
- `monitor_port`

3. 编译与烧录

```bash
pio run -e esp32-s3-devkitc-1
pio run -e esp32-s3-devkitc-1 -t upload
```

4. 打开串口日志

```bash
pio device monitor -b 115200
```

5. 打开 Web 控制面板
- 设备连上 WiFi 后，在串口查看分配到的 IP
- 浏览器访问 `http://<esp_ip>/`

## 默认按键（Web 键盘）

默认映射在 `src/game_shared.cpp`：

- 视角: `ArrowUp/Down/Left/Right`
- 移动: `W/A/S/D`
- 跳跃: `Space`
- 破坏: `Q`
- 放置: `R`
- 物品栏切换: `Z/X` 或 `1..5`

注意：
- 当前本地离线世界编辑默认关闭（`kEnableLocalBlockEdit = false`）更改他后默认进入离线世界，如果不更改你需要准备一个服务端 1.21.8离线推荐使用本项目适配的 `bareiron` 服务端

## Web Control

主要接口在 `src/web_control.cpp`：

- `GET /api/state`: 获取状态
- `GET /api/mc_cfg`: 设置联机参数
- `GET /api/mc_reconnect`: 强制重连 MC
- `GET /api/map`: 修改按键映射
- `GET /api/key`: 按键按下/释放事件
- `GET /api/release_all`: 释放全部按键

## 与服务端配套说明

推荐搭配本仓库内 `bareiron-main` 使用。  
当前实现面向 `bareiron` 的 1.21.8/协议 772 行为，针对 ESP32 做了窗口化与低带宽适配。

## 限制

- 不是完整 Java 客户端，仅实现可玩子集
- 本地世界窗口较小（`16 x 14 x 16`），依赖服务端中心区块流式更新
- 渲染、碰撞、方块映射均是简化实现
- Web 控制面板默认无鉴权，建议只在可信局域网使用


您可以直接把这个项目的地址甩给 codex iflow claudecode codebuddy qoder gemini cli opencode 等ai让他给你刷写和部署

噢噢噢噢！！！！本项目基于`MIT`协议开源！！！！！！

# ble_navi：D133 BLE 导航画面接收端

D133 作为 BLE 外设（GATT Server），接收手机或电脑发来的导航画面（JPEG / RGB565 / 1bpp 转向图标）和路名文本，用 VE 硬解、GE 送显。

蓝牙链路全部使用 SDK 原厂组件，这个包不修改任何原厂文件：

- AIC8800 驱动在 WiFi 上电时拉一次 `AIC_DEV_AIC8800_BT_RST_GPIO`（demo88 上是 PD.6）。
- 随后 `rwnx_fdrv_init → aicbt_init` 通过 SDIO 下载 BT patch，并把 BT 配到 UART，1.5 Mbps，带流控。
- 原厂 NimBLE（`packages/third-party/nimble`）经 UART2 的 H4 协议与 BT 通信，硬件流控 PA.2/PA.3。

## 文件

| 文件 | 作用 |
|---|---|
| `ble_port_nimble.c` | GATT 表、广播，以及 MTU / DLE / 2M PHY / 连接间隔的协商 |
| `ble_service.c` | 分发收到的数据，msh 命令 `ble_navi` |
| `ble_img_rx.c` | BNP 协议重组：位图、缺包区间、断点续传、CRC32 |
| `chronos_nav.c` | Chronos App 导航协议，移植自 fbiego/chronos-esp32 @9cb9269（MIT） |
| `disp_sink.c` | MPP JPEG 硬解，GE 贴图到 framebuffer |
| `web/` | Web Bluetooth 发送端，带分块图、吞吐曲线和模拟模式 |

## 使用

1. 烧录后在串口等待打印 `rwnx_fdrv_init exit`，然后执行 `ble_navi start`。设备开始广播 `D133-Navi`。
2. 在电脑上执行 `cd packages/third-party/ble_navi/web && python3 -m http.server 8000`。
3. 用 Chrome 或 Edge 打开 `http://localhost:8000`，点“连接开发板”，再发送一帧。
4. 在串口执行 `ble_navi`，查看 MTU、PHY、连接间隔、帧数、重传和显示统计。

导航画面请用 JPEG 格式：一帧 1024×600 约 20–60 KB。同样尺寸的 RGB565 原始数据有 1.2 MB，传一帧要十几秒。

如果 `ble_navi start` 之后串口出现 `host reset` 或 HCI 超时，先用原厂工具 `bt_test -c 01 03 0c 00`（HCI_Reset）确认控制器有响应。

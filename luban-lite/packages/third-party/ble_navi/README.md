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

## 尔雅 BLE 导航协议（领骑摩托 App）

打开 `BLE_NAVI_ERYA`（默认打开）后，设备同时实现《尔雅 BLE 导航协议》Ver 1.2，可以直接用“领骑摩托” App 投屏。实现在 `erya_proto.c`（协议、分帧、拼图）和 `erya_json.c`（基于 SDK 的 cJSON），主机测试在 `tools/`（`make -C tools erya`）。

- 设备名为 `EY_` 加蓝牙 MAC，例如 `EY_6C21A22E66B8`。广播里带服务 UUID 0xAAA0。
- AAA5：手机连上后读到 `<MAC>&<车型key>`，未连接时为 `0`。手机写入 0x00（安卓）或 0x01（苹果）后，设备在 AAA4 通知 `-3`（仅 BLE），并在 AAA6 下发图片格式。
- AAA7：EB 帧，JPEG 按包序号拼接；AAA8：EY 帧，导航文字。
- 收齐的 JPEG 由 VE 硬解后显示在 (`BLE_NAVI_ERYA_DISP_X`, `BLE_NAVI_ERYA_DISP_Y`)；导航文字交给 `disp_ui_nav_update()`。
- 串口执行 `ble_navi` 时，会多打印一行 `erya ...` 统计（图片成功/丢弃、包数、最后一张的大小和耗时）。

**车型 key：** 协议要求 AAA5 里带尔雅发放的车型 key（`BLE_NAVI_ERYA_MODEL_KEY`，默认空）。没有正式 key 时 App 可能拒绝投屏，需要通过商务向尔雅申请。

**文档里没写明、按假设实现的部分：** 服务 UUID 取 0xAAA0（`BLE_NAVI_ERYA_SVC_UUID` 可改）；AAA1～AAAA 全部开放读、写、无响应写和通知；AAA7/AAA8 按字节流解析，所以 App 一个包拆成多次写入或一次写入带多个包都能处理。

### 尔雅协议网页发送端（电脑扮演领骑摩托 App）

`web/erya.html`（与 `index.html` 同一个服务，页头有互相跳转的链接）：

1. 连接 `EY_` 设备，读 AAA5（MAC & 车型 key），写手机类型，收 AAA4 `-3` 和 AAA6 图片格式。车型 key 为空时照样继续，便于在激活前开发。
2. 按 AAA6 给的宽高和质量（q−100 → JPEG 质量）编码示例导航画面或本地图片，按 MTU 切成 EB 包写 AAA7；可连续发送，模拟导航刷新。MTU 从同一固件的 BNP 状态特征读取。
3. 表单填写 `fun:navi` 的字段，组 EY 帧写 AAA8。

协议没有逐包确认，页面显示的是浏览器写入成功的包；设备是否拼好整张图，看串口 `[erya] AAA7 picture complete` 或 `ble_navi` 的 `pics ok`。

测试（`tools/`）：`make erya` 测固件引擎；`make erya-web` 用 `erya_sender.js` 组包、经 `erya_replay` 交给固件代码比对；`make erya-ui` 在 jsdom 里点完整个页面（假 BLE 设备），记录的写入再交给固件代码回放（一次性准备：`npm i --prefix tools jsdom@24 @napi-rs/canvas`）。

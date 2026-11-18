# BNP 发送端（手机 / 电脑 → D133）

Web Bluetooth 页面：把导航画面（JPEG 或 RGB565）分块发到板子上显示，
同时显示链路参数、吞吐、每个分块的确认 / 缺失 / 重传状态。

## 打开

Web Bluetooth 只在安全上下文里可用：

    cd luban-lite/packages/third-party/blenavi/web/bnp-sender
    python3 -m http.server 8000
    # 桌面 Chrome / Edge 打开 http://localhost:8000
    # 安卓 Chrome：用 https，或者 chrome://inspect 端口转发到 localhost

板子串口先执行 `blenavi start`，页面点“连接开发板”，选 `D133-Navi`，再点“发送一帧”。

## 协议

`bnp-core.js` 按 `rtthread/bnp_proto.h` 打包：START → DATA（无响应写）
→ 每 N 块一次 QUERY（带响应写，既是流控屏障也是进度来源）→ COMMIT。
板子回 INCOMPLETE 时只补发缺失区段；回 CRC 时整帧重发；断线后用同一
frame_id + CRC 再 START 即可续传。

## 测试（不需要板子）

`tools/sim` 把板端原版 `ble_img_rx.c` 编成主机程序，发送端直接打它：

    cd ../../tools/sim
    make test   # 丢包、单块误码、断线续传、1.2 MB 整屏、MTU 23
    make e2e    # 用 Playwright 驱动本页面，蓝牙桥接到模拟设备，3% 丢包

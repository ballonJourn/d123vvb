# blenavi: D133 changes

BLE navigation receiver for D133: the Chronos phone app (Google Maps / OsmAnd
turn-by-turn) and BNP image frames (JPEG / RGB565 map pictures) over BLE,
shown with VE/GE. Upstream code is imported unmodified, one commit each;
everything below is on top of it.

## Imported upstream (unmodified)

| Directory | Upstream | Commit | Licence | How |
|---|---|---|---|---|
| `chronos-esp32/` | fbiego/chronos-esp32 | 9cb9269 (v1.9.1) | MIT | git subtree |
| `esp-nimble-cpp/` | h2zero/esp-nimble-cpp | 29f025e (2.6.0) | Apache-2.0 | git subtree |
| `ArduinoCore-API/` | arduino/ArduinoCore-API | 0f4e57e | **LGPL-2.1** | git subtree |
| `ESP32Time/` | fbiego/ESP32Time | 870a186 | MIT | git subtree |
| `web/epd-nrf5-html/` | tsl0922/EPD-nRF5 `html/` | 7e31961 | GPL-3.0 | snapshot + `UPSTREAM` (host tool, not linked) |

esp-nimble-cpp is pinned to 2.x: chronos-esp32 targets the NimBLE-Arduino
2.x API (`setCallbacks(ptr)`); esp-nimble-cpp 3.0 changed it to references.
NimBLE-Arduino itself was not used: its `nimconfig.h` always binds to its
bundled NimBLE copy; esp-nimble-cpp is the same API on an external host.

ArduinoCore-API is LGPL-2.1 and is linked statically (String/Print/Common
only). Recipients of the firmware must be able to relink it: keep the blenavi
objects available, or replace `String` with an MIT implementation.

Not imported: NordicPlayground/nrf52-ble-image-transfer-demo. Its licence
(clause 4) allows use only with Nordic chips. Only its tuning values are used
(DLE 251, 2M PHY, 15 ms interval) and its `ble_params_info` report layout.

## Stack on the board

```
chronos-esp32 (Chronos app protocol)   bnp_service.cpp (BNP images)
            \                          /
         esp-nimble-cpp 2.6.0 (NimBLEServer, one advertisement)
            |  compat/esp_nimble_cpp_rtt.h, FreeRTOS-Wrapper
         packages/third-party/nimble (vendor NimBLE host, H4 1.5 Mbps)
            |  UART2, HW CTS/RTS
         AIC8800 BT core (patch loaded by aicbt_init, CONFIG_AIC8800_BT_SUPPORT)
```

## Local files

| File | Purpose |
|---|---|
| `rtthread/compat/esp_nimble_cpp_rtt.h` (forced include, C++ only) | `CONFIG_BT_NIMBLE_ENABLED`; IDF version 5.0 so esp-nimble-cpp picks the new host API (`ble_gatts_notify_custom`) and keeps `setDataLen()` (DLE) active; host headers its non-IDF path omits; `#undef LOG_TAG` (RT-Thread ulog clash); `ble_npl_callout_init` returns 0 (vendor returns void); `nimble_port_init()` -> no-op (vendor already ran it at boot via `INIT_ENV_EXPORT`, a second call would re-init the HCI UART); `ble_npl_{mutex,sem,callout,event}_deinit` mapped to `rt_*_delete`; `uxGetCriticalNestingDepth` -> `rt_critical_level` |
| `rtthread/compat/nimble_port_freertos_rtt.c` | `nimble_port_freertos_init/deinit` as an RT-Thread thread running `NimBLEDevice::host_task`; `nimble_port_stop/deinit` return -1 (vendor port cannot stop the host) |
| `rtthread/compat/esp_log.h` | ESP-IDF log macros used by `NimBLELog.h` -> `rt_kprintf` |
| `rtthread/compat/Arduino.h`, `arduino_rtt.cpp` | Arduino port glue on ArduinoCore-API: `millis/delay`, `Serial` (console), ESP-style board info that chronos shows, FreeRTOS includes |
| `rtthread/compat/WString.h`, `Stream.h` | ESP32-core header names for ArduinoCore-API `api/String.h`, `api/Stream.h` (not putting `api/` on the path: `String.h` would shadow `<string.h>` on case-insensitive file systems) |
| `rtthread/compat/arduino_libc_rtt.c` | `ltoa/ultoa` (newlib has only itoa/utoa); `dtostrf` from ArduinoCore-API's own `dtostrf.c.impl`; `ble_phy_tx_power_set/get` report "not supported" (TX power comes from the AIC8800 BT patch) |
| `rtthread/blenavi_main.cpp` | `ChronosESP32` (default ctor + `setName`: the upstream named ctor does not delegate), navigation/icon callbacks -> display, BNP attached to the same server; msh `blenavi [start]` |
| `rtthread/bnp_service.cpp/.h` | BNP GATT service on esp-nimble-cpp; MTU from each write's `NimBLEConnInfo` (ChronosESP32 owns the server callbacks); DLE 251 / 2M PHY / 15 ms on connect |
| `rtthread/ble_img_rx.c/.h`, `bnp_proto.h` | BNP reassembly: MTU chunks, length/size/format header, sequence numbers, receive bitmap, selective NACK, resume after reconnect, CRC32 |
| `rtthread/disp_sink.c/.h` | VE JPEG decode + GE blit (call sequence of `packages/artinchip/mpp/mpp_test/pic_dec_test.c`), chronos 48x48 1bpp icon |
| `Kconfig`, `SConscript` | `LPKG_USING_BLENAVI`; upstream sources compiled from their own directories; forced include and `-fpermissive` scoped to blenavi (`LOCAL_CXXFLAGS`) |
| `demo88-nor.config` | options to add to the demo88-nor defconfig (AIC8800 BT, UART2 auto flow 1.5 Mbps, vendor NimBLE H4, blenavi) |
| `../Kconfig` | one `source` line |

`-fpermissive`: the vendor NimBLE port headers include `aic_core.h` ->
`aic_osal.h`, whose inline functions use implicit `void*` conversions (valid
C, not C++).

## Checked here (not on the board)

- Kconfig: demo88-nor helloworld defconfig + `demo88-nor.config` resolves on
  this tree with kconfiglib (FreeRTOS-Wrapper and C++ selected, NimBLE
  controller off, dataserver unaffected).
- SConscript selects 38 sources; all compile against this tree's vendor NimBLE
  (headers verified by preprocessor output); the C files also for RV32.
- Partial link: all 90 NimBLE references resolve in the vendor NimBLE objects;
  the rest are RT-Thread, FreeRTOS-Wrapper, SDK (`aic_memheap_*`, `mpp_*`),
  libc and the C++ runtime.
- C++ was checked with host g++ (no bare-metal RISC-V libstdc++ here); build
  with the Xuantie toolchain to confirm.

## Known limits

- `NimBLEDevice::deinit()` fails (vendor port has no host stop).
- `NimBLEDevice::setPower()` returns false (AIC8800 TX power is fixed by the BT patch).
- `AIC_PAN_DISPLAY` double buffer: frames are drawn into the shown buffer.
- Road names are printed by default; override `disp_ui_nav_update()` for LVGL.

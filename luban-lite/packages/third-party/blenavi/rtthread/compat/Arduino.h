/*
 * Arduino.h - Luban-Lite/RT-Thread platform glue for the Arduino core API.
 *
 * The API itself (String, Print, Stream, Common.h helpers) is
 * arduino/ArduinoCore-API, imported unmodified. This header is what each
 * Arduino port supplies: timing, a Serial port, and - because
 * fbiego/chronos-esp32 reads board info for its "about" screen - an ESP-style
 * info object, plus the FreeRTOS calls it uses (via RT-Thread's FreeRTOS-Wrapper).
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "api/ArduinoAPI.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
using namespace arduino;

/* Serial -> RT-Thread console (Print from ArduinoCore-API does the formatting) */
class RttConsole : public arduino::Print {
public:
    void begin(unsigned long) {}
    size_t write(uint8_t c) override;
    size_t write(const uint8_t *buf, size_t n) override;
};
extern RttConsole Serial;

/* ESP32 Arduino core's flash mode enum (chronos formats it as text) */
typedef enum { FM_QIO, FM_QOUT, FM_DIO, FM_DOUT, FM_FAST_READ, FM_SLOW_READ, FM_UNKNOWN = 0xff } FlashMode_t;

/* ESP32 Arduino core's EspClass subset used by chronos-esp32, answered for D133 */
class EspClass {
public:
    const char *getChipModel();
    uint8_t getChipRevision();
    uint8_t getChipCores();
    uint32_t getCpuFreqMHz();
    uint32_t getFlashChipSize();
    uint32_t getFlashChipSpeed();
    FlashMode_t getFlashChipMode();
    uint32_t getHeapSize();
    uint32_t getPsramSize();
    const char *getSdkVersion();
    uint32_t getSketchSize();
};
extern EspClass ESP;
#endif

/*
 * arduino_rtt.cpp - RT-Thread implementations behind compat/Arduino.h
 * SPDX-License-Identifier: Apache-2.0
 */
#include "Arduino.h"
#include <rtthread.h>
#include <rthw.h>

extern "C" unsigned long millis(void)
{
    return (unsigned long)(rt_tick_get() * 1000 / RT_TICK_PER_SECOND);
}
extern "C" unsigned long micros(void)
{
    return millis() * 1000UL;
}
extern "C" void delay(unsigned long ms)
{
    rt_thread_mdelay((rt_int32_t)ms);
}
extern "C" void delayMicroseconds(unsigned int us)
{
    rt_hw_us_delay(us);
}

size_t RttConsole::write(uint8_t c)
{
    char s[2] = { (char)c, 0 };
    rt_kprintf("%s", s);
    return 1;
}
size_t RttConsole::write(const uint8_t *buf, size_t n)
{
    for (size_t i = 0; i < n; i++)
        write(buf[i]);
    return n;
}
RttConsole Serial;

const char *EspClass::getChipModel() { return "ArtInChip D133"; }
uint8_t EspClass::getChipRevision() { return 0; }
uint8_t EspClass::getChipCores() { return 1; }
uint32_t EspClass::getCpuFreqMHz() { return 480; }
uint32_t EspClass::getFlashChipSize() { return 16u * 1024 * 1024; }
uint32_t EspClass::getFlashChipSpeed() { return 0; }
FlashMode_t EspClass::getFlashChipMode() { return FM_QIO; }
uint32_t EspClass::getHeapSize()
{
    rt_size_t total = 0, used = 0, max_used = 0;
    rt_memory_info(&total, &used, &max_used);
    return (uint32_t)total;
}
uint32_t EspClass::getPsramSize() { return 8u * 1024 * 1024; }
const char *EspClass::getSdkVersion() { return "Luban-Lite (RT-Thread)"; }
uint32_t EspClass::getSketchSize() { return 0; }
EspClass ESP;

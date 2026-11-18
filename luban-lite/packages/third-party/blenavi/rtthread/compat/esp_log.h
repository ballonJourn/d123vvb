/*
 * esp_log.h - ESP-IDF logging API subset used by esp-nimble-cpp (NimBLELog.h),
 * mapped to rt_kprintf.
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <stdarg.h>
#include <stdint.h>
#include <rtthread.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { ESP_LOG_NONE, ESP_LOG_ERROR, ESP_LOG_WARN, ESP_LOG_INFO, ESP_LOG_DEBUG, ESP_LOG_VERBOSE } esp_log_level_t;
#ifndef BLENAVI_ESP_LOG_LEVEL
#define BLENAVI_ESP_LOG_LEVEL ESP_LOG_INFO
#endif
static inline uint32_t esp_log_timestamp(void)
{
    return (uint32_t)(rt_tick_get() * 1000 / RT_TICK_PER_SECOND);
}
static inline void esp_log_write(esp_log_level_t level, const char *tag, const char *fmt, ...)
{
    (void)tag;
    if (level > BLENAVI_ESP_LOG_LEVEL) return;
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    rt_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    rt_kprintf("%s", buf);
}
#define ESP_LOG_LEVEL_LOCAL(level, tag, format, ...) \
    do { if ((level) <= BLENAVI_ESP_LOG_LEVEL) esp_log_write((level), (tag), format, ##__VA_ARGS__); } while (0)
#define ESP_LOGE(tag, format, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_ERROR, tag, format, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_WARN, tag, format, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_INFO, tag, format, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_DEBUG, tag, format, ##__VA_ARGS__)
#ifdef __cplusplus
}
#endif

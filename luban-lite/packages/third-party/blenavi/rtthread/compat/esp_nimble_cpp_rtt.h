/*
 * esp_nimble_cpp_rtt.h - force-included (-include) when building
 * h2zero/esp-nimble-cpp, unmodified, on RT-Thread over the vendor NimBLE
 * (packages/third-party/nimble) and RT-Thread's FreeRTOS-Wrapper.
 * Only fills gaps; nothing here changes NimBLE or esp-nimble-cpp behaviour.
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <stdint.h>
#include <rtthread.h>
#include "nimble/nimble_npl.h"

/* esp-nimble-cpp includes the host headers only on ESP_PLATFORM (IDF) or for
 * its bundled Arduino copy; "external NimBLE on another RTOS" gets none, so
 * supply the same set the IDF branch of NimBLEDevice.cpp includes. */
#include "nimble/nimble_port.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/* RT-Thread's ulog (pulled in by the vendor port) defines a LOG_TAG macro;
 * esp-nimble-cpp declares `static const char* LOG_TAG` in every file. */
#undef LOG_TAG

/* esp-nimble-cpp selects host-API variants by ESP-IDF version. The vendor
 * NimBLE host is new-style (ble_gatts_notify_custom, ble_gap_set_data_len),
 * i.e. what IDF >= 5.0 ships. With an older/undefined version esp-nimble-cpp
 * would remap notify to the old ble_gattc_* names and make
 * NimBLEServer::setDataLen() a no-op (no DLE). Code under ESP_PLATFORM is not
 * compiled here, so these values only pick the host-API flavour. */
#define ESP_IDF_VERSION_VAL(major, minor, patch) (((major) << 16) | ((minor) << 8) | (patch))
#define ESP_IDF_VERSION_MAJOR 5
#define ESP_IDF_VERSION_MINOR 0
#define ESP_IDF_VERSION_PATCH 0
#define ESP_IDF_VERSION ESP_IDF_VERSION_VAL(5, 0, 0)

/* esp-nimble-cpp guards every file with ESP-IDF's sdkconfig switch;
 * on Luban-Lite the stack is enabled by LPKG_USING_NIMBLE. */
#ifndef CONFIG_BT_NIMBLE_ENABLED
#define CONFIG_BT_NIMBLE_ENABLED 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* FreeRTOS API missing from RT-Thread's FreeRTOS-Wrapper.
 * RT-Thread equivalent: scheduler lock nesting level. */
static inline unsigned long uxGetCriticalNestingDepth(void)
{
    return (unsigned long)rt_critical_level();
}

/* NimBLE porting API added upstream after the vendor's RT-Thread port.
 * The vendor port (porting/npl/rtthread/src/npl_os_rtthread.c) creates these
 * objects with rt_mutex_create / rt_sem_create / rt_timer_create. */
static inline ble_npl_error_t ble_npl_mutex_deinit(struct ble_npl_mutex *mu)
{
    if (mu && mu->handle) { rt_mutex_delete((rt_mutex_t)mu->handle); mu->handle = 0; }
    return BLE_NPL_OK;
}
static inline ble_npl_error_t ble_npl_sem_deinit(struct ble_npl_sem *sem)
{
    if (sem && sem->handle) { rt_sem_delete((rt_sem_t)sem->handle); sem->handle = 0; }
    return BLE_NPL_OK;
}
static inline void ble_npl_event_deinit(struct ble_npl_event *ev)
{
    (void)ev; /* vendor events are plain structs, nothing to release */
}
static inline void ble_npl_callout_deinit(struct ble_npl_callout *co)
{
    if (co && co->handle) { rt_timer_delete((rt_timer_t)co->handle); co->handle = 0; }
}

/* Vendor ble_npl_callout_init() returns void; newer NimBLE returns int and
 * esp-nimble-cpp tests it. Keep the call, yield 0 (vendor init cannot fail). */
#define ble_npl_callout_init(...) (ble_npl_callout_init(__VA_ARGS__), 0)

/* On Luban-Lite the vendor port already ran nimble_port_init() at boot
 * (INIT_ENV_EXPORT(nimble_port_rtthread_init)); a second call would re-init
 * the event queue, mbuf pools and the HCI UART transport. NimBLEDevice::init()
 * calls it, so turn it into "already initialised". */
#define nimble_port_init() (0)

/* Newer NimBLE port API, absent from the vendor RT-Thread port. */
int nimble_port_stop(void);
int nimble_port_deinit(void);

/* FreeRTOS NimBLE port entry points used by NimBLEDevice::init()/deinit();
 * implemented on an RT-Thread thread in nimble_port_freertos_rtt.c. */
void nimble_port_freertos_init(void (*host_task_fn)(void *));
void nimble_port_freertos_deinit(void);

#ifdef __cplusplus
}
#endif

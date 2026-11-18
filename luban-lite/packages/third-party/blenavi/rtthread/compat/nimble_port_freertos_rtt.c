/*
 * nimble_port_freertos_rtt.c - the FreeRTOS NimBLE port entry points that
 * h2zero/esp-nimble-cpp calls, implemented for the vendor RT-Thread port.
 *
 * nimble_port_freertos_init(fn): run the host task fn (NimBLEDevice::host_task,
 * which calls nimble_port_run()) in an RT-Thread thread - the same thing the
 * vendor's ble_hs_thread_startup() does for nimble_port_run().
 * SPDX-License-Identifier: Apache-2.0
 */
#include <rtthread.h>
#include "rtconfig.h"

#ifndef LPKG_NIMBLE_HOST_THREAD_STACK_SIZE
#define LPKG_NIMBLE_HOST_THREAD_STACK_SIZE 4096
#endif
#ifndef LPKG_NIMBLE_HOST_THREAD_PRIORITY
#define LPKG_NIMBLE_HOST_THREAD_PRIORITY 8
#endif

static rt_thread_t s_host;

void nimble_port_freertos_init(void (*host_task_fn)(void *))
{
    if (s_host)
        return;
    s_host = rt_thread_create("nimble_host", host_task_fn, RT_NULL,
                              LPKG_NIMBLE_HOST_THREAD_STACK_SIZE,
                              LPKG_NIMBLE_HOST_THREAD_PRIORITY, 10);
    if (s_host)
        rt_thread_startup(s_host);
    else
        rt_kprintf("[blenavi] cannot create nimble host thread\n");
}

void nimble_port_freertos_deinit(void)
{
    /* called by host_task after nimble_port_run() returns: thread exits */
    s_host = RT_NULL;
}

/* The vendor port has no host stop/deinit; BLE stays up for the board's
 * lifetime. NimBLEDevice::deinit() reports failure instead of corrupting state. */
int nimble_port_stop(void)
{
    rt_kprintf("[blenavi] nimble_port_stop: not supported by the vendor port\n");
    return -1;
}

int nimble_port_deinit(void)
{
    return -1;
}

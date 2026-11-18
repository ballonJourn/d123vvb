/*
 * blenavi_main.cpp - D133 BLE navigation receiver.
 *
 *  - fbiego/chronos-esp32 (unmodified) speaks the Chronos app protocol:
 *    Google Maps / OsmAnd turn-by-turn text + 48x48 turn icon from a phone.
 *  - bnp_service.cpp adds the BNP image service (JPEG/RGB565 frames, MTU
 *    chunks, CRC32, selective NACK, resume) to the same NimBLEServer.
 *  - disp_sink.c shows both (VE JPEG decode, GE blit).
 *
 * msh: blenavi start | blenavi
 * SPDX-License-Identifier: Apache-2.0
 */
#include <ChronosESP32.h>
#include <rtthread.h>
#include <string.h>
extern "C" {
#include "disp_sink.h"
}
#include "bnp_service.h"

#ifndef BLENAVI_DEV_NAME
#define BLENAVI_DEV_NAME "D133-Navi"
#endif

static ChronosESP32 *s_watch;

static void on_connection(bool connected)
{
    if (connected)
        bnp_service_on_connect(NimBLEDevice::getServer());
    else
        bnp_service_on_disconnect();
}

static void on_config(Config cfg, uint32_t a, uint32_t b)
{
    (void)b;
    if (cfg == CF_NAV_DATA) {
        Navigation &n = s_watch->getNavigation();
        struct blenavi_nav v = {};
        v.active = n.active;
        v.title = n.title.c_str();
        v.directions = n.directions.c_str();
        v.eta = n.eta.c_str();
        v.duration = n.duration.c_str();
        v.distance = n.distance.c_str();
        v.speed = n.speed.c_str();
        disp_sink_nav(&v);
    } else if (cfg == CF_NAV_ICON && a == 2) {
        /* chronos sends the 48x48 icon as parts 0..2 (96 bytes each) */
        disp_sink_turn_icon_1bpp(s_watch->getNavigationIcon(), 48, 48);
    }
}

static void loop_thread(void *)
{
    for (;;) {
        s_watch->loop();
        rt_thread_mdelay(20);
    }
}

static int blenavi_start()
{
    if (s_watch)
        return 0;
    if (disp_sink_init() != 0)
        rt_kprintf("[blenavi] display sink init failed, frames will be dropped\n");

    /* default ctor + setName: upstream's ChronosESP32(String, ...) ctor calls
     * ChronosESP32() in its body (a temporary), skipping the default init. */
    s_watch = new ChronosESP32();
    s_watch->setName(BLENAVI_DEV_NAME);
    s_watch->setConnectionCallback(on_connection);
    s_watch->setConfigurationCallback(on_config);
    s_watch->begin();                         /* NimBLEDevice::init, NUS service, advertising */

    /* ATT MTU 247 = DLE 251 - 4 (L2CAP): one write fills exactly one LL PDU.
     * The vendor NimBLE default (BLE_ATT_PREFERRED_MTU 256) gives 257-byte
     * L2CAP frames, i.e. a full PDU plus a 6-byte one for every chunk. */
    NimBLEDevice::setMTU(247);

    bnp_service_attach(NimBLEDevice::getServer());
    NimBLEDevice::startAdvertising();         /* re-advertise with the BNP UUID added */

    rt_thread_t t = rt_thread_create("chronos", loop_thread, RT_NULL, 4096, 20, 10);
    if (t)
        rt_thread_startup(t);
    rt_kprintf("[blenavi] advertising as %s (Chronos app + BNP)\n", BLENAVI_DEV_NAME);
    return 0;
}

static int cmd_blenavi(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "start"))
        return blenavi_start();
    if (s_watch)
        rt_kprintf("chronos: connected=%d nav_active=%d\n", s_watch->isConnected(),
                   s_watch->getNavigation().active);
    bnp_service_dump();
    disp_sink_dump_stats();
    return 0;
}
MSH_CMD_EXPORT_ALIAS(cmd_blenavi, blenavi, blenavi [start] - BLE navigation receiver);

#ifdef BLENAVI_AUTOSTART
static int blenavi_autostart(void) { return blenavi_start(); }
INIT_APP_EXPORT(blenavi_autostart);
#endif

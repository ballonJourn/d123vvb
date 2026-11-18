/*
 * bnp_service.cpp - image/navigation transfer service (BNP, bnp_proto.h) built
 * on h2zero/esp-nimble-cpp. Shares the NimBLEServer that fbiego/chronos-esp32
 * creates, so one advertisement serves both the Chronos app and BNP senders.
 *
 * ChronosESP32 owns the server callbacks, so BNP takes the connection handle
 * and ATT MTU from each write's NimBLEConnInfo, and connect/disconnect from
 * ChronosESP32::setConnectionCallback (see blenavi_main.cpp).
 *
 * Link tuning values from NordicPlayground/nrf52-ble-image-transfer-demo:
 * DLE 251 octets, 2M PHY, 15 ms connection interval.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <NimBLEDevice.h>
#include <rtthread.h>
#include <string.h>
#include "aic_core.h"
extern "C" {
#include "ble_img_rx.h"
#include "disp_sink.h"
}
#include "bnp_service.h"

#ifndef BLENAVI_MAX_FRAME_BYTES
#define BLENAVI_MAX_FRAME_BYTES (1024 * 600 * 2)
#endif
#ifndef BLENAVI_CONN_INTERVAL_MS
#define BLENAVI_CONN_INTERVAL_MS 15
#endif

static struct bir s_rx;
static NimBLEServer *s_server;
static NimBLECharacteristic *s_ctrl, *s_data, *s_stat;
static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_mtu = 23;
static uint32_t s_notify_fail;

/* ---------- bir_ops (C engine -> C++ transport) ---------- */
static int op_notify(void *, const uint8_t *b, uint16_t n)
{
    if (s_conn == BLE_HS_CONN_HANDLE_NONE || !s_ctrl->notify(b, n, s_conn)) {
        s_notify_fail++;
        return -1;
    }
    return 0;
}
static void *op_alloc(void *, size_t n) { return aicos_malloc_align(MEM_CMA, n, CACHE_LINE_SIZE); }
static void op_free(void *, void *p) { aicos_free_align(MEM_CMA, p); }
static uint32_t op_now(void *) { return (uint32_t)(rt_tick_get() * 1000 / RT_TICK_PER_SECOND); }
static int op_frame(void *, const struct bir_frame *f) { return disp_sink_submit(f); }

/* BNP NAV_TEXT TLV -> blenavi_nav (strings point into a static copy) */
static void op_nav(void *, const uint8_t *t, uint16_t len)
{
    static char road[96], dist[32], eta[32], dur[32], speed[16];
    struct blenavi_nav n = {};
    n.active = 1;
    for (uint16_t i = 0; i + 2 <= len;) {
        uint8_t tag = t[i], l = t[i + 1];
        const uint8_t *v = t + i + 2;
        if (i + 2 + l > len) break;
        char *dst = nullptr; size_t cap = 0;
        switch (tag) {
        case BNP_NAV_ROAD: dst = road; cap = sizeof(road); n.directions = road; break;
        case BNP_NAV_DIST: dst = dist; cap = sizeof(dist); n.title = n.distance = dist; break;
        case BNP_NAV_ETA: dst = eta; cap = sizeof(eta); n.eta = eta; break;
        case BNP_NAV_DURATION: dst = dur; cap = sizeof(dur); n.duration = dur; break;
        case BNP_NAV_SPEED: dst = speed; cap = sizeof(speed); n.speed = speed; break;
        case BNP_NAV_TURN: if (l) n.turn = v[0]; break;
        case BNP_NAV_ACTIVE: if (l) n.active = v[0]; break;
        }
        if (dst) { size_t c = l < cap - 1 ? l : cap - 1; memcpy(dst, v, c); dst[c] = 0; }
        i += 2 + l;
    }
    disp_sink_nav(&n);
}

static void track_link(NimBLEConnInfo &ci)
{
    s_conn = ci.getConnHandle();
    uint16_t mtu = ci.getMTU();
    if (mtu != s_mtu) {
        s_mtu = mtu;
        bir_set_mtu(&s_rx, mtu);
    }
}

static void send_params()
{
    uint8_t p[sizeof(struct bnp_params)] = { BNP_RSP_PARAMS };
    uint16_t ci = 0, lat = 0, to = 0;
    uint8_t tx = 1, rx = 1;
    if (s_conn != BLE_HS_CONN_HANDLE_NONE && s_server->getConnectedCount()) {
        NimBLEConnInfo info = s_server->getPeerInfo(0);
        ci = info.getConnInterval(); lat = info.getConnLatency(); to = info.getConnTimeout();
        s_server->getPhy(s_conn, &tx, &rx);
    }
    p[1] = s_mtu; p[2] = s_mtu >> 8;
    p[3] = ci; p[4] = ci >> 8; p[5] = lat; p[6] = lat >> 8; p[7] = to; p[8] = to >> 8;
    p[9] = tx; p[10] = rx;
    p[11] = 251; p[12] = 0; p[13] = 251; p[14] = 0;   /* DLE requested on connect */
    p[15] = 0;                                         /* backend: vendor NimBLE */
    op_notify(nullptr, p, sizeof(p));
}

class CtrlCb : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &ci) override
    {
        track_link(ci);
        NimBLEAttValue v = c->getValue();
        const uint8_t *b = v.data();
        uint16_t n = v.size();
        if (!n) return;
        switch (b[0]) {
        case BNP_OP_GET_PARAMS: send_params(); break;
        case BNP_OP_REQ_PHY:
            if (n >= 2) s_server->updatePhy(s_conn, b[1], b[1], 0);
            break;
        case BNP_OP_REQ_CONN:
            if (n >= 9)
                s_server->updateConnParams(s_conn, b[1] | (b[2] << 8), b[3] | (b[4] << 8),
                                           b[5] | (b[6] << 8), b[7] | (b[8] << 8));
            break;
        default: bir_on_ctrl(&s_rx, b, n); break;
        }
    }
};

class DataCb : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &ci) override
    {
        track_link(ci);
        NimBLEAttValue v = c->getValue();
        bir_on_data(&s_rx, v.data(), v.size());
    }
};

static CtrlCb s_ctrl_cb;
static DataCb s_data_cb;

void bnp_service_attach(NimBLEServer *server)
{
    s_server = server;
    struct bir_ops ops = { op_notify, op_alloc, op_free, op_now, op_frame, op_nav, nullptr };
    bir_init(&s_rx, &ops, BLENAVI_MAX_FRAME_BYTES);

    NimBLEService *svc = server->createService(BNP_SVC_UUID_STR);
    s_ctrl = svc->createCharacteristic("d1330002-4e41-5649-b1e0-0000d1330000",
                                       NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
    s_data = svc->createCharacteristic("d1330003-4e41-5649-b1e0-0000d1330000",
                                       NIMBLE_PROPERTY::WRITE_NR, 517);
    s_stat = svc->createCharacteristic("d1330004-4e41-5649-b1e0-0000d1330000",
                                       NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    s_ctrl->setCallbacks(&s_ctrl_cb);
    s_data->setCallbacks(&s_data_cb);
    svc->start();
    NimBLEDevice::getAdvertising()->addServiceUUID(BNP_SVC_UUID_STR);
}

void bnp_service_on_connect(NimBLEServer *server)
{
    if (!server->getConnectedCount()) return;
    uint16_t h = server->getPeerInfo(0).getConnHandle();
    s_conn = h;
    server->setDataLen(h, 251);
    server->updatePhy(h, BLE_GAP_LE_PHY_2M_MASK, BLE_GAP_LE_PHY_2M_MASK, 0);
    uint16_t itvl = BLENAVI_CONN_INTERVAL_MS * 4 / 5;      /* 1.25 ms units */
    server->updateConnParams(h, itvl, itvl, 0, 400);
}

void bnp_service_on_disconnect()
{
    s_conn = BLE_HS_CONN_HANDLE_NONE;
    s_mtu = 23;
    bir_on_disconnect(&s_rx);           /* partial frame kept for resume */
}

extern "C" void blenavi_release(uint8_t layer) { bir_release(&s_rx, layer); }

void bnp_service_dump()
{
    const struct bir_stats *s = &s_rx.st;
    rt_kprintf("bnp: conn=0x%04x mtu=%u frames ok=%u crc_err=%u resumes=%u aborts=%u\n",
               s_conn, s_mtu, s->frames_ok, s->frames_crc_err, s->resumes, s->aborts);
    rt_kprintf("bnp: chunks rx=%u dup=%u bad=%u bytes=%u notify_fail=%u\n",
               s->chunks_rx, s->chunks_dup, s->chunks_bad, s->bytes_rx, s_notify_fail);
}

/*
 * ble_port_nimble.c - BLE Navi backend on RT-Thread NimBLE (host only).
 * Host = ArtInChip NimBLE local package (packages/third-party/nimble).
 * Controller = AIC8800 BT core (patch loaded by aicbt_init over SDIO), H4 HCI on
 * LPKG_NIMBLE_HCI_UART_DEVICE_NAME (demo88: UART2 PD.4/PD.5, HW CTS/RTS PA.2/PA.3).
 *
 * Link tuning follows NordicPlayground/nrf52-ble-image-transfer-demo main.c:
 *   ATT MTU 247, DLE 251 octets, 2M PHY preferred, conn interval 15 ms.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <rtthread.h>
#include <string.h>
#include "rtconfig.h"

#ifdef BLE_NAVI_BACKEND_NIMBLE

#if defined(LPKG_NIMBLE_CTLR) || defined(PKG_NIMBLE_CTLR)
#error "Disable LPKG_NIMBLE_CTLR (NimBLE nRF5x link layer): on D133 the AIC8800 is the controller, NimBLE must be host-only"
#endif

#include "nimble/ble.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "ble_service.h"

#ifdef BLE_NAVI_CONN_CE_FULL           /* Kconfig bool: defined (empty) when y */
#define BNI_MAX_CE_LEN 0xFFFF
#else
#define BNI_MAX_CE_LEN 0
#endif
#ifndef BLE_NAVI_PREF_MTU
#define BLE_NAVI_PREF_MTU 247
#endif
#ifndef BLE_NAVI_CONN_INTERVAL_MS
#define BLE_NAVI_CONN_INTERVAL_MS 15
#endif

extern void ble_hs_thread_startup(void);   /* porting/npl/rtthread */
extern void ble_store_config_init(void);

static uint8_t  s_own_addr_type;
static uint16_t s_ctrl_handle, s_data_handle, s_stat_handle;
static uint16_t s_chronos_tx_handle, s_chronos_rx_handle;
static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static uint8_t  s_tx_phy = 1, s_rx_phy = 1;
static const char *s_name;

#ifdef BLE_NAVI_ERYA
#ifndef BLE_NAVI_ERYA_SVC_UUID
#define BLE_NAVI_ERYA_SVC_UUID 0xAAA0
#endif
#ifndef BLE_NAVI_ERYA_NAME_PREFIX
#define BLE_NAVI_ERYA_NAME_PREFIX "EY_"
#endif
/* 尔雅: characteristics 0xAAA1..0xAAAA. The document does not say which
 * property each one has, so all of them allow read, write, write without
 * response and notify; the app picks what it uses. */
static const ble_uuid16_t UUID_ERYA_SVC = BLE_UUID16_INIT(BLE_NAVI_ERYA_SVC_UUID);
static const ble_uuid16_t UUID_ERYA[ERYA_NUM_CHR] = {
    BLE_UUID16_INIT(0xAAA1), BLE_UUID16_INIT(0xAAA2), BLE_UUID16_INIT(0xAAA3),
    BLE_UUID16_INIT(0xAAA4), BLE_UUID16_INIT(0xAAA5), BLE_UUID16_INIT(0xAAA6),
    BLE_UUID16_INIT(0xAAA7), BLE_UUID16_INIT(0xAAA8), BLE_UUID16_INIT(0xAAA9),
    BLE_UUID16_INIT(0xAAAA),
};
static uint16_t s_erya_handle[ERYA_NUM_CHR];
static char s_erya_name[32];
#define ERYA_CHR_FLAGS (BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | \
                        BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_NOTIFY)
#define ERYA_CHR(i) { .uuid = &UUID_ERYA[i].u, .access_cb = gatt_access, \
                      .arg = (void *)(uintptr_t)(0xAAA1 + (i)), \
                      .val_handle = &s_erya_handle[i], .flags = ERYA_CHR_FLAGS }
#endif

static const ble_uuid128_t UUID_SVC  = BLE_UUID128_INIT(BNP_UUID128(BNP_SVC_UUID16));
static const ble_uuid128_t UUID_CTRL = BLE_UUID128_INIT(BNP_UUID128(BNP_CTRL_UUID16));
static const ble_uuid128_t UUID_DATA = BLE_UUID128_INIT(BNP_UUID128(BNP_DATA_UUID16));
static const ble_uuid128_t UUID_STAT = BLE_UUID128_INIT(BNP_UUID128(BNP_STAT_UUID16));
/* Chronos / NUS: 6e400001-b5a3-f393-e0a9-e50e24dcca9e (little endian) */
#define NUS_UUID(b12) BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, \
                                       0x93, 0xf3, 0xa3, 0xb5, (b12), 0x00, 0x40, 0x6e)
static const ble_uuid128_t UUID_NUS    = NUS_UUID(0x01);
static const ble_uuid128_t UUID_NUS_RX = NUS_UUID(0x02);
static const ble_uuid128_t UUID_NUS_TX = NUS_UUID(0x03);

/* flatten an mbuf chain into a stack buffer; ATT payload <= MTU-3 <= 512 */
static int om_flat(struct os_mbuf *om, uint8_t *buf, uint16_t cap, uint16_t *len)
{
    return ble_hs_mbuf_to_flat(om, buf, cap, len);
}

static int gatt_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn;
    static uint8_t buf[520];   /* host thread only -> single instance is safe */
    uint16_t n = 0;
#ifdef BLE_NAVI_ERYA
    uint16_t erya_uuid = (uint16_t)(uintptr_t)arg;
    if (erya_uuid >= 0xAAA1 && erya_uuid <= 0xAAAA) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            n = ble_navi_erya_read(erya_uuid, buf, sizeof(buf));
            return os_mbuf_append(ctxt->om, buf, n) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
        }
        if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR)
            return BLE_ATT_ERR_UNLIKELY;
        if (om_flat(ctxt->om, buf, sizeof(buf), &n))
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        ble_navi_on_erya_write(erya_uuid, buf, n);
        return 0;
    }
#else
    (void)arg;
#endif

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        if (attr == s_stat_handle) {
            struct ble_navi_link l = g_ble_navi.link;
            ble_port_refresh_link(&l);
            int rc = os_mbuf_append(ctxt->om, &l, sizeof(l));
            return rc ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
        }
        return BLE_ATT_ERR_READ_NOT_PERMITTED;
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR)
        return BLE_ATT_ERR_UNLIKELY;
    if (om_flat(ctxt->om, buf, sizeof(buf), &n))
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;

    if (attr == s_data_handle)
        ble_navi_on_data_write(buf, n);
    else if (attr == s_ctrl_handle)
        ble_navi_on_ctrl_write(buf, n);
    else if (attr == s_chronos_rx_handle)
        ble_navi_on_chronos_write(buf, n);
    return 0;
}

static const struct ble_gatt_svc_def s_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &UUID_SVC.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = &UUID_CTRL.u, .access_cb = gatt_access, .val_handle = &s_ctrl_handle,
              .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY },
            { .uuid = &UUID_DATA.u, .access_cb = gatt_access, .val_handle = &s_data_handle,
              .flags = BLE_GATT_CHR_F_WRITE_NO_RSP },
            { .uuid = &UUID_STAT.u, .access_cb = gatt_access, .val_handle = &s_stat_handle,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY },
            { 0 },
        },
    },
#ifdef BLE_NAVI_CHRONOS_COMPAT
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &UUID_NUS.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = &UUID_NUS_RX.u, .access_cb = gatt_access, .val_handle = &s_chronos_rx_handle,
              .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP },
            { .uuid = &UUID_NUS_TX.u, .access_cb = gatt_access, .val_handle = &s_chronos_tx_handle,
              .flags = BLE_GATT_CHR_F_NOTIFY },
            { 0 },
        },
    },
#endif
#ifdef BLE_NAVI_ERYA
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &UUID_ERYA_SVC.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            ERYA_CHR(0), ERYA_CHR(1), ERYA_CHR(2), ERYA_CHR(3), ERYA_CHR(4),
            ERYA_CHR(5), ERYA_CHR(6), ERYA_CHR(7), ERYA_CHR(8), ERYA_CHR(9),
            { 0 },
        },
    },
#endif
    { 0 },
};

static int notify(uint16_t handle, const uint8_t *b, uint16_t n)
{
    if (s_conn == BLE_HS_CONN_HANDLE_NONE)
        return BLE_HS_ENOTCONN;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(b, n);
    if (!om)
        return BLE_HS_ENOMEM;   /* MSYS pool empty: raise MSYS_1_BLOCK_COUNT */
    return ble_gatts_notify_custom(s_conn, handle, om);
}

int ble_port_notify_ctrl(const uint8_t *b, uint16_t n) { return notify(s_ctrl_handle, b, n); }
int ble_port_notify_chronos(const uint8_t *b, uint16_t n) { return notify(s_chronos_tx_handle, b, n); }
int ble_port_notify_erya(uint16_t uuid16, const uint8_t *b, uint16_t n)
{
#ifdef BLE_NAVI_ERYA
    if (uuid16 >= 0xAAA1 && uuid16 <= 0xAAAA)
        return notify(s_erya_handle[uuid16 - 0xAAA1], b, n);
#endif
    (void)uuid16; (void)b; (void)n;
    return BLE_HS_EINVAL;
}

int ble_port_request_phy(uint8_t mask)
{
    if (s_conn == BLE_HS_CONN_HANDLE_NONE)
        return BLE_HS_ENOTCONN;
    return ble_gap_set_prefered_le_phy(s_conn, mask, mask, BLE_GAP_LE_PHY_CODED_ANY);
}

int ble_port_request_conn(uint16_t min_ci, uint16_t max_ci, uint16_t lat, uint16_t to)
{
    if (s_conn == BLE_HS_CONN_HANDLE_NONE)
        return BLE_HS_ENOTCONN;
    struct ble_gap_upd_params p = {
        .itvl_min = min_ci, .itvl_max = max_ci, .latency = lat,
        .supervision_timeout = to, .min_ce_len = 0,
        .max_ce_len = BNI_MAX_CE_LEN,       /* esp-idf throughput_app: 0xFFFF */
    };
    return ble_gap_update_params(s_conn, &p);
}

void ble_port_refresh_link(struct ble_navi_link *l)
{
    l->backend = 0;
    if (s_conn == BLE_HS_CONN_HANDLE_NONE)
        return;
    struct ble_gap_conn_desc d;
    if (ble_gap_conn_find(s_conn, &d) == 0) {
        l->conn_interval = d.conn_itvl;
        l->conn_latency = d.conn_latency;
        l->supervision_to = d.supervision_timeout;
    }
    l->att_mtu = ble_att_mtu(s_conn);
    uint8_t tx, rx;
    if (ble_gap_read_le_phy(s_conn, &tx, &rx) == 0) {
        s_tx_phy = tx; s_rx_phy = rx;
    }
    l->tx_phy = s_tx_phy;
    l->rx_phy = s_rx_phy;
}

static void advertise(void);

/*
 * Link tuning, staged. Phones run their own LL procedures right after the
 * connection (connection update, MTU exchange, discovery). Firing our PHY
 * update and connection parameter request at the same moment made two
 * "instant" procedures collide: with an Android phone the link died 1-2 s
 * after every connect (disconnect 0x208 = HCI 0x08 connection timeout,
 * "conn update st=520"), before the app read AAA5. Windows did not hit it.
 *   connect           : data length only (no instant)
 *   +LINK_TUNE_DELAY  : 2M PHY request
 *   PHY done / +1 s   : connection parameter request
 * At most one instant-based procedure of ours is pending at any time.
 */
#ifndef BLE_NAVI_LINK_TUNE_DELAY_MS
#define BLE_NAVI_LINK_TUNE_DELAY_MS 2000
#endif
enum { TUNE_IDLE, TUNE_WAIT, TUNE_PHY, TUNE_DONE };
static struct ble_npl_callout s_tune_co;
static int s_tune_stage = TUNE_IDLE;
static uint16_t s_tune_conn = BLE_HS_CONN_HANDLE_NONE;
static int s_tune_inited;

static void tune_conn_params(void)
{
    s_tune_stage = TUNE_DONE;
    uint16_t ci = BLE_NAVI_CONN_INTERVAL_MS * 4 / 5; /* ms -> 1.25ms units */
    int rc = ble_port_request_conn(ci, ci, 0, 400);
    rt_kprintf("[nimble] link tune: conn interval %u ms request rc=%d\n",
               BLE_NAVI_CONN_INTERVAL_MS, rc);
}

static void tune_step(struct ble_npl_event *ev)
{
    (void)ev;
    if (s_tune_conn == BLE_HS_CONN_HANDLE_NONE || s_conn != s_tune_conn)
        return;
    if (s_tune_stage == TUNE_WAIT) {
        s_tune_stage = TUNE_PHY;
        int rc = ble_gap_set_prefered_le_phy(s_tune_conn, BLE_GAP_LE_PHY_2M_MASK,
                                             BLE_GAP_LE_PHY_2M_MASK, BLE_GAP_LE_PHY_CODED_ANY);
        rt_kprintf("[nimble] link tune: 2M PHY request rc=%d\n", rc);
        if (rc) {
            tune_conn_params();
            return;
        }
        /* no PHY event within 1 s (peer ignores it): go on anyway */
        ble_npl_callout_reset(&s_tune_co, ble_npl_time_ms_to_ticks32(1000));
    } else if (s_tune_stage == TUNE_PHY) {
        tune_conn_params();
    }
}

static void tune_link(uint16_t conn)
{
    if (!s_tune_inited) {
        ble_npl_callout_init(&s_tune_co, nimble_port_get_dflt_eventq(), tune_step, NULL);
        s_tune_inited = 1;
    }
    /* DLE: 251 octets, 2120 us = 251B on 1M PHY (spec max). No instant. */
    int rc = ble_gap_set_data_len(conn, 251, 2120);
    if (rc) rt_kprintf("[nimble] set_data_len rc=%d\n", rc);
    g_ble_navi.link.max_tx_octets = 251;
    g_ble_navi.link.max_rx_octets = 251;

    s_tune_conn = conn;
    s_tune_stage = TUNE_WAIT;
    if (BLE_NAVI_LINK_TUNE_DELAY_MS == 0)
        tune_step(NULL);
    else
        ble_npl_callout_reset(&s_tune_co, ble_npl_time_ms_to_ticks32(BLE_NAVI_LINK_TUNE_DELAY_MS));
}

static void tune_stop(void)
{
    s_tune_conn = BLE_HS_CONN_HANDLE_NONE;
    s_tune_stage = TUNE_IDLE;
    if (s_tune_inited)
        ble_npl_callout_stop(&s_tune_co);
}

static int gap_event(struct ble_gap_event *ev, void *arg)
{
    (void)arg;
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status == 0) {
            s_conn = ev->connect.conn_handle;
            ble_navi_on_connect(s_conn);
            tune_link(s_conn);
        } else {
            advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        rt_kprintf("[nimble] disconnect reason=0x%x\n", ev->disconnect.reason);
        tune_stop();
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        ble_navi_on_disconnect();
        advertise();
        break;
    case BLE_GAP_EVENT_MTU:
        ble_navi_on_mtu(ev->mtu.value);
        break;
    case BLE_GAP_EVENT_CONN_UPDATE:
        ble_port_refresh_link(&g_ble_navi.link);
        rt_kprintf("[nimble] conn update st=%d ci=%u\n", ev->conn_update.status,
                   g_ble_navi.link.conn_interval);
        break;
    case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE:
        s_tx_phy = ev->phy_updated.tx_phy;
        s_rx_phy = ev->phy_updated.rx_phy;
        rt_kprintf("[nimble] phy tx=%u rx=%u (status %d)\n", s_tx_phy, s_rx_phy,
                   ev->phy_updated.status);
        if (s_tune_stage == TUNE_PHY) {           /* our PHY step finished */
            ble_npl_callout_stop(&s_tune_co);
            tune_conn_params();
        }
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (ev->subscribe.attr_handle == s_chronos_tx_handle && ev->subscribe.cur_notify)
            ble_navi_on_chronos_subscribe();
#ifdef BLE_NAVI_ERYA
        for (int i = 0; i < ERYA_NUM_CHR; i++)
            if (ev->subscribe.attr_handle == s_erya_handle[i] && ev->subscribe.cur_notify)
                ble_navi_on_erya_subscribe(0xAAA1 + i);
#endif
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        break;
    default:
        break;
    }
    return 0;
}

static void advertise(void)
{
    struct ble_hs_adv_fields f;
    memset(&f, 0, sizeof(f));
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    struct ble_hs_adv_fields r;
    memset(&r, 0, sizeof(r));
#ifdef BLE_NAVI_ERYA
    /* adv: flags(3) + uuid16 0xAAA0(4) + "EY_<MAC>"(17) = 24 <= 31
     * scan rsp: BNP 128-bit service(18). NUS is still served, not advertised. */
    f.uuids16 = (ble_uuid16_t *)&UUID_ERYA_SVC;
    f.num_uuids16 = 1;
    f.uuids16_is_complete = 0;
    f.name = (const uint8_t *)s_name;
    f.name_len = strlen(s_name);
    f.name_is_complete = 1;
    r.uuids128 = (ble_uuid128_t *)&UUID_SVC;
    r.num_uuids128 = 1;
    r.uuids128_is_complete = 0;
#else
    f.uuids128 = (ble_uuid128_t *)&UUID_SVC;
    f.num_uuids128 = 1;
    f.uuids128_is_complete = 0;
    r.name = (const uint8_t *)s_name;
    r.name_len = strlen(s_name);
    r.name_is_complete = 1;
#ifdef BLE_NAVI_CHRONOS_COMPAT
    r.uuids128 = (ble_uuid128_t *)&UUID_NUS;
    r.num_uuids128 = 1;
#endif
#endif
    int rc = ble_gap_adv_set_fields(&f);
    if (rc) { rt_kprintf("[nimble] adv fields rc=%d\n", rc); return; }
    rc = ble_gap_adv_rsp_set_fields(&r);
    if (rc) rt_kprintf("[nimble] scan rsp rc=%d (name too long?)\n", rc);

    struct ble_gap_adv_params p;
    memset(&p, 0, sizeof(p));
    p.conn_mode = BLE_GAP_CONN_MODE_UND;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    p.itvl_min = p.itvl_max = 64; /* 40 ms, as Nordic demo APP_ADV_INTERVAL */
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &p, gap_event, NULL);
    if (rc && rc != BLE_HS_EALREADY)
        rt_kprintf("[nimble] adv start rc=%d\n", rc);
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0)
        rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc) { rt_kprintf("[nimble] no identity address rc=%d\n", rc); return; }
#ifdef BLE_NAVI_ERYA
    {
        uint8_t addr[6];
        char mac[13];
        if (ble_hs_id_copy_addr(s_own_addr_type == BLE_OWN_ADDR_PUBLIC ? BLE_ADDR_PUBLIC
                                                                       : BLE_ADDR_RANDOM,
                                addr, NULL) == 0) {
            rt_snprintf(mac, sizeof(mac), "%02X%02X%02X%02X%02X%02X",
                        addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
            ble_navi_erya_set_mac(mac);
            rt_snprintf(s_erya_name, sizeof(s_erya_name), "%s%s", BLE_NAVI_ERYA_NAME_PREFIX, mac);
            s_name = s_erya_name;
            ble_svc_gap_device_name_set(s_name);
        } else {
            rt_kprintf("[erya] cannot read BT address, keeping name '%s'\n", s_name);
        }
    }
#endif
    /* default for all future links too */
    ble_gap_set_prefered_default_le_phy(BLE_GAP_LE_PHY_2M_MASK, BLE_GAP_LE_PHY_2M_MASK);
    rt_kprintf("[nimble] synced with controller, advertising as '%s'\n", s_name);
    advertise();
}

static void on_reset(int reason)
{
    rt_kprintf("[nimble] host reset, reason=%d (HCI link to AIC8800 lost?)\n", reason);
}

int ble_port_start(const char *name)
{
    s_name = name;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(s_svcs);
    if (rc == 0)
        rc = ble_gatts_add_svcs(s_svcs);
    if (rc) { rt_kprintf("[nimble] gatt svc rc=%d\n", rc); return rc; }
    ble_svc_gap_device_name_set(name);
    ble_att_set_preferred_mtu(BLE_NAVI_PREF_MTU);

    ble_hs_thread_startup();
    return 0;
}

#endif /* BLE_NAVI_BACKEND_NIMBLE */

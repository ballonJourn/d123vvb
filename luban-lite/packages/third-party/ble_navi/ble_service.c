/*
 * ble_service.c - glue: GATT writes -> BNP engine / Chronos parser -> disp_sink
 * SPDX-License-Identifier: Apache-2.0
 */
#include <rtthread.h>
#include <string.h>
#include "aic_core.h"
#include "ble_service.h"
#include "disp_sink.h"
#include <rtdevice.h>
#include <time.h>

#ifndef BLE_NAVI_MAX_FRAME_BYTES
#define BLE_NAVI_MAX_FRAME_BYTES (1024 * 600 * 2)  /* demo88 panel */
#endif
#ifndef BLE_NAVI_DEV_NAME
#define BLE_NAVI_DEV_NAME "D133-Navi"
#endif
/* ChronosESP32 default ctor: _infoTimer.duration = 3 * 1000 */
#define CHRONOS_INFO_DELAY_MS 3000

struct ble_navi g_ble_navi;
static rt_timer_t s_chronos_timer;

/* ---------------- bir_ops ---------------- */
static int ops_notify(void *ctx, const uint8_t *b, uint16_t n)
{
    (void)ctx;
    int rc = ble_port_notify_ctrl(b, n);
    if (rc)
        g_ble_navi.notify_fail++;
    return rc;
}

/* frame buffers: CMA so GE can blit RGB565 straight from them (phys addr),
 * 64B aligned for cache maintenance. JPEG is memcpy'd into the VE packet. */
static void *ops_alloc(void *ctx, size_t n)
{
    (void)ctx;
    return aicos_malloc_align(MEM_CMA, n, CACHE_LINE_SIZE);
}
static void ops_free(void *ctx, void *p)
{
    (void)ctx;
    aicos_free_align(MEM_CMA, p);
}
static uint32_t ops_now(void *ctx)
{
    (void)ctx;
    return (uint32_t)(rt_tick_get() * 1000 / RT_TICK_PER_SECOND);
}
static int ops_frame(void *ctx, const struct bir_frame *f)
{
    (void)ctx;
    return disp_sink_submit(f); /* queue only; never decode in BLE host thread */
}
static void ops_nav(void *ctx, const uint8_t *tlv, uint16_t n)
{
    (void)ctx;
    struct nav_state ns;
    nav_state_from_bnp_tlv(&ns, tlv, n);
    disp_sink_nav(&ns);
}

/* ---------------- 尔雅 BLE 导航协议 ---------------- */
#ifdef BLE_NAVI_ERYA
#ifndef BLE_NAVI_ERYA_MODEL_KEY
#define BLE_NAVI_ERYA_MODEL_KEY ""
#endif
#ifdef BLE_NAVI_ERYA_LOG
/* print a value the way the protocol document writes it:
 *   EY frame -> EY len=N {json}   EB frame -> EB json=N bin=M {json}
 *   text     -> "text"            other    -> hex */
static void erya_log_value(const char *what, uint16_t uuid, const uint8_t *b, uint16_t n)
{
    char t[200];
    uint16_t k = 0, i, start = 0;
    if (n >= 6 && b[0] == 'E' && b[1] == 'Y') {
        uint32_t jl = ((uint32_t)b[2] << 24) | (b[3] << 16) | (b[4] << 8) | b[5];
        rt_kprintf("[erya] %s %04X: EY len=%u ", what, uuid, jl);
        start = 6;
    } else if (n >= 10 && b[0] == 'E' && b[1] == 'B') {
        uint32_t jl = ((uint32_t)b[2] << 24) | (b[3] << 16) | (b[4] << 8) | b[5];
        uint32_t bl = ((uint32_t)b[6] << 24) | (b[7] << 16) | (b[8] << 8) | b[9];
        rt_kprintf("[erya] %s %04X: EB json=%u bin=%u ", what, uuid, jl, bl);
        start = 10;
        if (jl < (uint32_t)(n - 10))
            n = (uint16_t)(10 + jl);       /* do not print the JPEG bytes */
    } else {
        int text = 1;
        for (i = 0; i < n; i++)
            if (b[i] < 0x20 && b[i] != '\t') text = 0;
        if (!text || n == 0) {
            rt_kprintf("[erya] %s %04X: %u bytes hex", what, uuid, n);
            for (i = 0; i < n && i < 32; i++)
                rt_kprintf(" %02x", b[i]);
            rt_kprintf("\n");
            return;
        }
        rt_kprintf("[erya] %s %04X: ", what, uuid);
    }
    for (i = start; i < n && k < sizeof(t) - 1; i++)
        t[k++] = (char)b[i];
    t[k] = 0;
    rt_kprintf("%s%s\n", t, (n - start > k) ? " ..." : "");
}
#define ERYA_LOG(...) rt_kprintf(__VA_ARGS__)
#else
#define erya_log_value(w, u, b, n) do { } while (0)
#define ERYA_LOG(...) do { } while (0)
#endif

static int erya_notify(void *ctx, uint16_t uuid, const uint8_t *b, uint16_t n)
{
    (void)ctx;
    int rc = ble_port_notify_erya(uuid, b, n);
    if (rc)
        g_ble_navi.notify_fail++;
    erya_log_value(rc ? "notify FAILED" : "notify ->phone", uuid, b, n);
    return rc;
}
/* msh "ble_navi nodisp on": pictures are received and logged but not shown,
 * to tell the link speed from the cost of decoding / drawing them */
static volatile uint8_t s_nodisp;

static void erya_picture(void *ctx, uint8_t *buf, uint32_t len)
{
    (void)ctx;
    struct bir_frame f;
    memset(&f, 0, sizeof(f));
    f.fmt = BNP_FMT_JPEG;
    f.layer = BNP_LAYER_MAP;
    f.x = BLE_NAVI_ERYA_DISP_X;
    f.y = BLE_NAVI_ERYA_DISP_Y;
    f.width = BLE_NAVI_ERYA_DISP_W;     /* 0 = as decoded */
    f.height = BLE_NAVI_ERYA_DISP_H;
    f.data = buf;
    f.len = len;
    f.rx_ms = ops_now(NULL);
#ifdef BLE_NAVI_ERYA_LOG       /* ms / pk are only used by the log line */
    {
        /* receive speed of this picture: tells the link (phone + air + HCI UART)
         * from the display, which runs later in the "disp" / LVGL thread */
        uint32_t ms = g_ble_navi.erya.st.last_pic_ms, pk = g_ble_navi.erya.pic_sum;
        ERYA_LOG("[erya] AAA7 picture complete: %u bytes in %u ms (%u KB/s, %u pkts, %u us/pkt), JPEG %s -> display\n",
                 len, ms, ms ? len * 1000u / ms / 1024u : 0, pk, pk ? ms * 1000u / pk : 0,
                 (len > 3 && buf[0] == 0xFF && buf[1] == 0xD8) ? "SOI ok" : "NO SOI (not a JPEG?)");
    }
#endif
    if (s_nodisp) {
        ops_free(NULL, buf);
        return;
    }
    disp_sink_submit_owned(&f);         /* sink frees buf */
}
static void erya_navi(void *ctx, const struct nav_state *ns)
{
    (void)ctx;
    disp_sink_nav(ns);
}

/* {"fun":"time"}: UTC seconds + time zone hours -> RTC (UTC) */
static void erya_time(void *ctx, uint32_t utc, int tz)
{
    (void)ctx;
#ifdef RT_USING_RTC
    rt_err_t rc = set_timestamp((time_t)utc);
#else
    int rc = -1;
#endif
    time_t lt = (time_t)utc + (time_t)tz * 3600;
    struct tm tmv;
    gmtime_r(&lt, &tmv);
    rt_kprintf("[erya] time sync from phone: %04d-%02d-%02d %02d:%02d:%02d (UTC%+d), rtc %s\n",
               tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min,
               tmv.tm_sec, tz, rc == 0 ? "set" : "NOT set");
}

/* {"fun":"activate","type":n}: meaning not documented in Ver 1.2; with an
 * empty model key the app sent type 2 and showed "device not activated" */
static void erya_activate(void *ctx, int type)
{
    (void)ctx;
    rt_kprintf("[erya] ===== activate type=%d from the app%s =====\n", type,
               type == 2 ? " (seen with an empty/unknown key: not activated)" : "");
}

static void erya_setup(void)
{
    struct erya_cfg cfg;
    memset(&cfg, 0, sizeof(cfg));
    strcpy(cfg.mac, "000000000000");    /* replaced by the backend once synced */
    rt_strncpy(cfg.model_key, BLE_NAVI_ERYA_MODEL_KEY, sizeof(cfg.model_key) - 1);
    cfg.pic_w = BLE_NAVI_ERYA_PIC_W;
    cfg.pic_h = BLE_NAVI_ERYA_PIC_H;
    cfg.pic_q = BLE_NAVI_ERYA_PIC_Q;
    rt_strncpy(cfg.pic_type, BLE_NAVI_ERYA_PIC_TYPE, sizeof(cfg.pic_type) - 1);
    cfg.max_pic_bytes = BLE_NAVI_ERYA_MAX_PIC_BYTES;
    struct erya_ops ops = { .notify = erya_notify, .alloc = ops_alloc, .free = ops_free,
                            .now_ms = ops_now, .on_picture = erya_picture, .on_navi = erya_navi,
                            .on_time = erya_time, .on_activate = erya_activate, .ctx = NULL };
    erya_init(&g_ble_navi.erya, &cfg, &ops);
    rt_kprintf("[erya] model key %s (AAA5 = <MAC>&<key>)\n",
               cfg.model_key[0] ? "set" : "EMPTY");
    if (!cfg.model_key[0])
        rt_kprintf("[erya] BLE_NAVI_ERYA_MODEL_KEY is empty: the app may refuse the "
                   "device until 尔雅 issues a 车型 key\n");
}

void ble_navi_erya_set_mac(const char *mac12)
{
    rt_strncpy(g_ble_navi.erya.cfg.mac, mac12, 12);
    g_ble_navi.erya.cfg.mac[12] = 0;
}
void ble_navi_on_erya_write(uint16_t uuid, const uint8_t *b, uint16_t n)
{
    struct erya *e = &g_ble_navi.erya;
#ifdef BLE_NAVI_ERYA_LOG
    uint32_t lost0 = e->st.pics_lost, big0 = e->st.pics_too_big, jerr0 = e->st.json_err;
    int32_t next0 = e->pic_next;
    if (uuid != ERYA_UUID_AAA7)            /* AAA7 = JPEG packets: summarised below */
        erya_log_value("write <-phone", uuid, b, n);
#endif
    erya_on_write(e, uuid, b, n);
#ifdef BLE_NAVI_ERYA_LOG
    if (uuid == ERYA_UUID_AAA5)
        rt_kprintf("[erya] phone type = %d (0 Android, 1 iOS)\n", e->phone);
    if (next0 < 1 && e->pic_next >= 1)
        rt_kprintf("[erya] AAA7 picture start: sum=%u size=%u (first packet %u bytes)\n",
                   e->pic_sum, e->pic_size, e->pic_got);
    if (e->st.pics_lost != lost0)
        rt_kprintf("[erya] AAA7 picture dropped (packet gap/out of order), lost=%u\n",
                   e->st.pics_lost);
    if (e->st.pics_too_big != big0)
        rt_kprintf("[erya] AAA7 picture too big (> BLE_NAVI_ERYA_MAX_PIC_BYTES)\n");
    if (e->st.json_err != jerr0)
        rt_kprintf("[erya] %04X JSON not understood, json_err=%u\n", uuid, e->st.json_err);
#endif
}
void ble_navi_on_erya_subscribe(uint16_t uuid)
{
    erya_on_subscribe(&g_ble_navi.erya, uuid);
}
uint16_t ble_navi_erya_read(uint16_t uuid, uint8_t *out, uint16_t cap)
{
    uint16_t n = erya_read(&g_ble_navi.erya, uuid, out, cap);
#ifdef BLE_NAVI_ERYA_LOG
    /* the app polls AAA5 until it gets the address: log the first reads only */
    static uint32_t reads[ERYA_NUM_CHR];
    uint32_t c = (uuid >= 0xAAA1 && uuid <= 0xAAAA) ? ++reads[uuid - 0xAAA1] : 0;
    if (c <= 3 || c % 50 == 0) {
        char what[24];
        rt_snprintf(what, sizeof(what), "read #%u ->phone", c);
        erya_log_value(what, uuid, out, n);
    }
#endif
    return n;
}
#endif

/* ---------------- chronos hooks ---------------- */
static int chronos_notify(void *ctx, const uint8_t *b, uint16_t n)
{
    (void)ctx;
    return ble_port_notify_chronos(b, n);
}
static void chronos_event(void *ctx, enum chronos_evt e, int arg, const struct nav_state *ns)
{
    (void)ctx;
    if (e == CHRONOS_EVT_NAV_DATA)
        disp_sink_nav(ns);
    else if (e == CHRONOS_EVT_NAV_ICON && ns->icon_parts == 0x7)
        disp_sink_turn_icon_1bpp(ns->icon, CHRONOS_ICON_SIZE, CHRONOS_ICON_SIZE);
    (void)arg;
}
static void chronos_timer_cb(void *p)
{
    (void)p;
    chronos_nav_on_subscribe(&g_ble_navi.chronos);
}

/* ---------------- BT power ---------------- */
/* BT is brought up entirely by the AIC8800 WiFi driver on this SDK:
 *  - wifi_port_rtx.c pulses AIC_DEV_AIC8800_BT_RST_GPIO (demo88: PD.6) low->high
 *    right after WiFi power-on;
 *  - rwnx_fdrv_init() -> aicbt_init() (CONFIG_BT_SUPPORT, from
 *    CONFIG_AIC8800_BT_SUPPORT) loads the BT patch over SDIO and sets the BT
 *    UART to 1.5 Mbps + flow control.
 * Do not reset BT again here: that would discard the downloaded patch.
 * The driver exposes no "done" flag, so `ble_navi start` is run by hand after
 * "rwnx_fdrv_init exit" has been printed. */
int ble_navi_bt_power_on(void)
{
    return ble_navi_bt_vendor_init();
}

RT_WEAK int ble_navi_bt_vendor_init(void)
{
    /* nothing to do: aicbt_init ran inside the AIC8800 WiFi driver init.
     * Check with the vendor tool: `bt_test -c 01 03 0c 00` (HCI_Reset). */
    return 0;
}

/* ---------------- public ---------------- */
int ble_navi_init(void)
{
    struct ble_navi *b = &g_ble_navi;
    memset(&b->link, 0, sizeof(b->link));
    b->link.conn_handle = 0xFFFF;
    b->link.att_mtu = 23;

    struct bir_ops ops = { ops_notify, ops_alloc, ops_free, ops_now, ops_frame, ops_nav, b };
    bir_init(&b->rx, &ops, BLE_NAVI_MAX_FRAME_BYTES);

    memset(&b->chronos, 0, sizeof(b->chronos));
    b->chronos.notify = chronos_notify;
    b->chronos.on_event = chronos_event;
    b->chronos.screen_conf = 0; /* CS_0x0_000_CFF: let the app not assume a watch face */
    chronos_nav_init(&b->chronos);

    s_chronos_timer = rt_timer_create("chrinfo", chronos_timer_cb, RT_NULL,
                                      rt_tick_from_millisecond(CHRONOS_INFO_DELAY_MS),
                                      RT_TIMER_FLAG_ONE_SHOT | RT_TIMER_FLAG_SOFT_TIMER);
#ifdef BLE_NAVI_ERYA
    erya_setup();
#endif
    ble_navi_bt_power_on();
    if (disp_sink_init() != 0)
        rt_kprintf("[ble_navi] display sink init failed, frames will be dropped\n");
    return ble_port_start(BLE_NAVI_DEV_NAME);
}

void ble_navi_on_connect(uint16_t conn)
{
    g_ble_navi.link.conn_handle = conn;
    rt_kprintf("[ble_navi] connected handle=%u\n", conn);
#ifdef BLE_NAVI_ERYA
    erya_on_connect(&g_ble_navi.erya);
#endif
}

void ble_navi_on_disconnect(void)
{
    g_ble_navi.link.conn_handle = 0xFFFF;
    g_ble_navi.link.att_mtu = 23;
    bir_on_disconnect(&g_ble_navi.rx);      /* partial frames kept for resume */
    chronos_nav_on_disconnect(&g_ble_navi.chronos);
#ifdef BLE_NAVI_ERYA
    erya_on_disconnect(&g_ble_navi.erya);
#endif
    if (s_chronos_timer)
        rt_timer_stop(s_chronos_timer);
}

void ble_navi_on_mtu(uint16_t mtu)
{
    g_ble_navi.link.att_mtu = mtu;
    bir_set_mtu(&g_ble_navi.rx, mtu);
    rt_kprintf("[ble_navi] ATT MTU %u -> chunk %u\n", mtu, g_ble_navi.rx.max_chunk);
}

static void send_params(void)
{
    struct ble_navi_link *l = &g_ble_navi.link;
    ble_port_refresh_link(l);
    uint8_t p[sizeof(struct bnp_params)];
    p[0] = BNP_RSP_PARAMS;
    p[1] = l->att_mtu; p[2] = l->att_mtu >> 8;
    p[3] = l->conn_interval; p[4] = l->conn_interval >> 8;
    p[5] = l->conn_latency; p[6] = l->conn_latency >> 8;
    p[7] = l->supervision_to; p[8] = l->supervision_to >> 8;
    p[9] = l->tx_phy; p[10] = l->rx_phy;
    p[11] = l->max_tx_octets; p[12] = l->max_tx_octets >> 8;
    p[13] = l->max_rx_octets; p[14] = l->max_rx_octets >> 8;
    p[15] = l->backend;
    ops_notify(NULL, p, sizeof(p));
}

void ble_navi_on_ctrl_write(const uint8_t *b, uint16_t n)
{
    if (!n)
        return;
    switch (b[0]) {
    case BNP_OP_GET_PARAMS:
        send_params();
        break;
    case BNP_OP_REQ_PHY:
        if (n >= 2)
            ble_port_request_phy(b[1]);
        break;
    case BNP_OP_REQ_CONN:
        if (n >= 9)
            ble_port_request_conn(b[1] | (b[2] << 8), b[3] | (b[4] << 8),
                                  b[5] | (b[6] << 8), b[7] | (b[8] << 8));
        break;
    default:
        bir_on_ctrl(&g_ble_navi.rx, b, n);
        break;
    }
}

void ble_navi_on_data_write(const uint8_t *b, uint16_t n)
{
    bir_on_data(&g_ble_navi.rx, b, n);
}

void ble_navi_on_chronos_write(const uint8_t *b, uint16_t n)
{
    chronos_nav_on_write(&g_ble_navi.chronos, b, n);
}

void ble_navi_on_chronos_subscribe(void)
{
    if (s_chronos_timer)
        rt_timer_start(s_chronos_timer);
}

void ble_navi_release(uint8_t layer)
{
    bir_release(&g_ble_navi.rx, layer);
}

/* ---------------- msh ---------------- */
/*
 * Read-only check whether the PSRAM above AIC_PSRAM_SW_SIZE (code, data, CMA)
 * and the heap_psram_sw region below it are the same physical memory, i.e.
 * the board has less PSRAM than AIC_PSRAM_SIZE says. Compares 1024 words of
 * this module's code with the words AIC_PSRAM_SW_SIZE lower. Nothing is
 * written.
 */
static void psram_alias_check(void)
{
#if defined(AIC_PSRAM_SIZE) && defined(AIC_PSRAM_SW_SIZE) && AIC_PSRAM_SW_SIZE > 0
    uintptr_t code = (uintptr_t)&ble_navi_init & ~(uintptr_t)0xFFF;
    uintptr_t sw_lo = 0x40000000u, sw_hi = 0x40000000u + AIC_PSRAM_SW_SIZE;
    rt_kprintf("AIC_PSRAM_SIZE=0x%x AIC_PSRAM_SW_SIZE=0x%x, code at 0x%08lx\n",
               (unsigned)AIC_PSRAM_SIZE, (unsigned)AIC_PSRAM_SW_SIZE, (unsigned long)code);
    if (code < sw_hi || code - AIC_PSRAM_SW_SIZE < sw_lo) {
        rt_kprintf("code is not above the psram_sw region, nothing to compare\n");
        return;
    }
    const volatile uint32_t *a = (const volatile uint32_t *)code;
    const volatile uint32_t *b = (const volatile uint32_t *)(code - AIC_PSRAM_SW_SIZE);
    int same = 0, n = 1024;
    for (int i = 0; i < n; i++)
        same += (a[i] == b[i]);
    rt_kprintf("0x%08lx vs 0x%08lx: %d/%d words identical\n", (unsigned long)code,
               (unsigned long)(code - AIC_PSRAM_SW_SIZE), same, n);
    if (same == n)
        rt_kprintf("=> ALIASED: the board has %u MB PSRAM or less. heap_psram_sw overlaps the "
                   "code/CMA region: nothing may allocate MEM_PSRAM_SW. Set AIC_PSRAM_SIZE to the "
                   "real size in the defconfig.\n", (unsigned)(AIC_PSRAM_SW_SIZE >> 20));
    else
        rt_kprintf("=> not aliased: heap_psram_sw is separate memory\n");
#else
    rt_kprintf("no PSRAM software region configured\n");
#endif
}

static int cmd_ble_navi(int argc, char **argv)
{
    struct ble_navi *b = &g_ble_navi;
    if (argc >= 2 && !strcmp(argv[1], "start"))
        return ble_navi_init();
    if (argc >= 2 && !strcmp(argv[1], "psram")) {
        psram_alias_check();
        return 0;
    }
#ifdef BLE_NAVI_ERYA
    if (argc >= 3 && !strcmp(argv[1], "nodisp")) {
        s_nodisp = !strcmp(argv[2], "on");
        rt_kprintf("erya pictures are %s\n", s_nodisp ? "received only, NOT shown" : "shown");
        return 0;
    }
#endif
    struct bir_stats *s = &b->rx.st;
    ble_port_refresh_link(&b->link);
    rt_kprintf("conn=0x%04x mtu=%u ci=%u*1.25ms lat=%u to=%u*10ms phy=%u/%u dle=%u/%u backend=%u\n",
               b->link.conn_handle, b->link.att_mtu, b->link.conn_interval, b->link.conn_latency,
               b->link.supervision_to, b->link.tx_phy, b->link.rx_phy,
               b->link.max_tx_octets, b->link.max_rx_octets, b->link.backend);
    rt_kprintf("frames ok=%u crc_err=%u resumes=%u aborts=%u\n",
               s->frames_ok, s->frames_crc_err, s->resumes, s->aborts);
    rt_kprintf("chunks rx=%u dup=%u bad=%u bytes=%u notify_fail=%u\n",
               s->chunks_rx, s->chunks_dup, s->chunks_bad, s->bytes_rx, b->notify_fail);
#ifdef BLE_NAVI_ERYA
    {
        struct erya *e = &b->erya;
        rt_kprintf("erya mac=%s phone=%d pics ok=%u lost=%u big=%u alloc_fail=%u pkts=%u "
                   "last=%uB/%ums navi=%u ey=%u eb=%u bad_magic=%u json_err=%u\n",
                   e->cfg.mac, e->phone, e->st.pics_ok, e->st.pics_lost, e->st.pics_too_big,
                   e->st.pics_alloc_fail, e->st.pkts, e->st.last_pic_bytes, e->st.last_pic_ms,
                   e->st.navi_msgs, e->st.frames_ey, e->st.frames_eb, e->st.bad_magic,
                   e->st.json_err);
        rt_kprintf("erya display: %s\n", s_nodisp ? "OFF (ble_navi nodisp off to restore)" : "on");
        rt_kprintf("erya activate=%d (msgs %u) time_utc=%u tz=%d (msgs %u) unknown_fun=%u key=%s\n",
                   e->activate, e->st.activate_msgs, e->time_utc, e->time_tz, e->st.time_msgs,
                   e->st.unknown_fun, e->cfg.model_key[0] ? "set" : "EMPTY");
    }
#endif
#if defined(LPKG_NIMBLE_HCI_H4)
    {
        extern volatile uint32_t hci_h4_resync_count;
        rt_kprintf("hci h4 resync (uart bytes lost, frame dropped): %u\n", hci_h4_resync_count);
    }
#endif
    disp_sink_dump_stats();
    return 0;
}
MSH_CMD_EXPORT_ALIAS(cmd_ble_navi, ble_navi, ble_navi [start|psram|nodisp on|off] - BLE navigation image receiver);

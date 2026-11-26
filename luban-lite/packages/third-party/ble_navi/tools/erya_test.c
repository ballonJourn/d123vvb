/*
 * erya_test.c - host test of erya_proto.c + erya_json.c (SDK cJSON), playing
 * the phone side of 尔雅 BLE 导航协议 Ver 1.2.
 *   make -C tools erya   (ASan + UBSan)
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "erya_proto.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---------------- device side hooks ---------------- */
static int allocs, frees;
static uint8_t *got_pic; static uint32_t got_len; static int pics;
static struct nav_state got_nav; static int navs;
static uint8_t last_notify[16][256]; static uint16_t last_notify_len[16];

static unsigned notify_count[16];
static int d_notify(void *c, uint16_t u, const uint8_t *b, uint16_t n)
{ (void)c; int i = u - 0xAAA0; memcpy(last_notify[i], b, n); last_notify_len[i] = n; notify_count[i]++; return 0; }
static void *d_alloc(void *c, size_t n) { (void)c; allocs++; return malloc(n); }
static void d_free(void *c, void *p) { (void)c; if (p) frees++; free(p); }
static uint32_t d_now(void *c) { (void)c; return 0; }
static void d_pic(void *c, uint8_t *b, uint32_t n)
{ (void)c; if (got_pic) frees++; free(got_pic); got_pic = b; got_len = n; pics++; }
static void d_navi(void *c, const struct nav_state *ns) { (void)c; got_nav = *ns; navs++; }

static uint32_t got_utc; static int got_tz = -99, got_act = -1, times, acts;
static void d_time(void *c, uint32_t u, int tz) { (void)c; got_utc = u; got_tz = tz; times++; }
static void d_act(void *c, int t) { (void)c; got_act = t; acts++; }

static struct erya E;
static void dev_init(void)
{
    struct erya_cfg cfg = { .pic_w = 800, .pic_h = 480, .pic_q = 160, .max_pic_bytes = 512 * 1024 };
    strcpy(cfg.mac, "CCC4B2B64DA0");
    strcpy(cfg.model_key, "EYMOTOR12FCB99FD224DB2B22F5A7703");
    strcpy(cfg.pic_type, "420");
    struct erya_ops ops = { .notify = d_notify, .alloc = d_alloc, .free = d_free, .now_ms = d_now,
                            .on_picture = d_pic, .on_navi = d_navi, .on_time = d_time,
                            .on_activate = d_act, .ctx = NULL };
    erya_init(&E, &cfg, &ops);
}

/* ---------------- phone side helpers ---------------- */
static uint32_t put_be32(uint8_t *p, uint32_t v)
{ p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; return 4; }

static uint32_t mk_ey(const char *js, uint8_t *o)
{ uint32_t n = strlen(js); o[0]='E'; o[1]='Y'; put_be32(o+2, n); memcpy(o+6, js, n); return n+6; }

static uint32_t mk_eb(const char *js, const uint8_t *bin, uint32_t m, uint8_t *o)
{ uint32_t n = strlen(js); o[0]='E'; o[1]='B'; put_be32(o+2, n); put_be32(o+6, m);
  memcpy(o+10, js, n); memcpy(o+10+n, bin, m); return 10+n+m; }

/* write a byte stream to AAAx in writes of `w` bytes (w=0: random 1..300) */
static void write_stream(uint16_t uuid, const uint8_t *b, uint32_t n, uint32_t w)
{
    while (n) {
        uint32_t k = w ? w : 1 + (uint32_t)(rand() % 300);
        if (k > n) k = n;
        erya_on_write(&E, uuid, b, (uint16_t)k);
        b += k; n -= k;
    }
}

/* EB packets: binary piece per packet so one EB fits in mtu-3 bytes */
static uint8_t *pic_stream(const uint8_t *jpg, uint32_t len, uint16_t mtu, int drop,
                           uint32_t *out_len, uint32_t *out_pkts)
{
    uint8_t *o = malloc(len * 2 + 100000); uint32_t p = 0;
    char js[96];
    snprintf(js, sizeof(js), "{\"fun\":\"pic\",\"num\":%u,\"sum\":%u,\"size\":%u}", 99999u, 99999u, len);
    uint32_t piece = (mtu - 3) - 10 - (uint32_t)strlen(js);
    uint32_t sum = (len + piece - 1) / piece;
    for (uint32_t i = 0; i < sum; i++) {
        uint32_t k = (i == sum - 1) ? len - i * piece : piece;
        snprintf(js, sizeof(js), "{\"fun\":\"pic\",\"num\":%u,\"sum\":%u,\"size\":%u}", i, sum, len);
        if ((int)i == drop) continue;
        p += mk_eb(js, jpg + i * piece, k, o + p);
    }
    *out_len = p; *out_pkts = sum;
    return o;
}

static uint8_t *load(const char *path, uint32_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*n);
    if (fread(b, 1, *n, f) != *n) { fclose(f); free(b); return NULL; }
    fclose(f); return b;
}

int main(int argc, char **argv)
{
    srand(7);
    uint32_t jl = 0;
    uint8_t *jpg = argc > 1 ? load(argv[1], &jl) : NULL;
    if (!jpg) { jl = 60000; jpg = malloc(jl); for (uint32_t i = 0; i < jl; i++) jpg[i] = i * 31 + 7; }
    uint8_t buf[400];

    /* 1. handshake */
    dev_init();
    uint16_t n = erya_read(&E, 0xAAA5, buf, sizeof(buf));
    CHECK(n == 1 && buf[0] == '0', "AAA5 before connect = '0'");
    erya_on_connect(&E);
    n = erya_read(&E, 0xAAA5, buf, sizeof(buf)); buf[n] = 0;
    CHECK(!strcmp((char *)buf, "CCC4B2B64DA0&EYMOTOR12FCB99FD224DB2B22F5A7703"), "AAA5 mac&key: %s", buf);
    uint8_t android = 0x00;
    erya_on_write(&E, 0xAAA5, &android, 1);
    CHECK(E.phone == ERYA_PHONE_ANDROID, "phone type android");
    CHECK(last_notify_len[4] == 2 && !memcmp(last_notify[4], "-3", 2), "AAA4 '-3' notified");
    uint8_t *f6 = last_notify[6]; uint16_t n6 = last_notify_len[6];
    uint32_t jn = (f6[2] << 24) | (f6[3] << 16) | (f6[4] << 8) | f6[5];
    CHECK(n6 > 6 && f6[0] == 'E' && f6[1] == 'Y' && jn == (uint32_t)n6 - 6, "AAA6 EY frame");
    char js6[200]; memcpy(js6, f6 + 6, jn); js6[jn] = 0;
    CHECK(!strcmp(js6, "{\"fun\":\"pic\",\"w\":800,\"h\":480,\"q\":160,\"type\":\"420\"}"), "AAA6 json %s", js6);

    /* 1b. change the requested format at run time (msh "ble_navi pic") */
    {
        static const char want[] = "{\"fun\":\"pic\",\"w\":320,\"h\":192,\"q\":140,\"type\":\"422\"}";
        CHECK(erya_set_pic(&E, 320, 192, 140, "422") == 0, "set_pic accepts 320x192 q140 422");
        uint8_t *g = last_notify[6]; uint16_t gn = last_notify_len[6];
        uint32_t gj = (g[2] << 24) | (g[3] << 16) | (g[4] << 8) | g[5];
        char gs[200]; memcpy(gs, g + 6, gj); gs[gj] = 0;
        CHECK(gn > 6 && g[0] == 'E' && g[1] == 'Y' && !strcmp(gs, want), "set_pic notifies AAA6: %s", gs);
        n = erya_read(&E, 0xAAA6, buf, sizeof(buf));
        CHECK(n > 6 && !memcmp(buf + 6, want, sizeof(want) - 1), "AAA6 read returns the new format");

        uint8_t keep[256]; uint16_t keepn = last_notify_len[6]; memcpy(keep, last_notify[6], keepn);
        unsigned keepc = notify_count[6];
        /* every refused call, on its own: nothing may change, not even for a moment
         * (a version that stored w before validating passed a check made only at the end) */
#define REFUSED(call, what) do { \
            CHECK((call) == -1, what " refused"); \
            CHECK(E.cfg.pic_w == 320 && E.cfg.pic_h == 192 && E.cfg.pic_q == 140 && \
                  !strcmp(E.cfg.pic_type, "422"), what ": config unchanged"); \
            CHECK(notify_count[6] == keepc && last_notify_len[6] == keepn && !memcmp(last_notify[6], keep, keepn), \
                  what ": nothing notified"); \
        } while (0)
        REFUSED(erya_set_pic(&E, 321, 192, 140, "420"), "width not a multiple of 16");
        REFUSED(erya_set_pic(&E, 320, 190, 140, "420"), "height not a multiple of 16");
        REFUSED(erya_set_pic(&E, 0, 192, 140, "420"), "width 0");
        REFUSED(erya_set_pic(&E, 640, 2064, 140, "420"), "height over 2048");
        REFUSED(erya_set_pic(&E, 2064, 192, 140, "420"), "width over 2048");
        REFUSED(erya_set_pic(&E, 640, 192, 119, "420"), "q 119");
        REFUSED(erya_set_pic(&E, 640, 192, 201, "420"), "q 201");
        REFUSED(erya_set_pic(&E, 640, 192, 140, "444"), "type 444");
        REFUSED(erya_set_pic(&E, 640, 192, 140, NULL), "type NULL");
#undef REFUSED

        CHECK(erya_set_pic(&E, 800, 480, 160, "420") == 0, "restore 800x480 q160 420");
    }
    uint8_t ios = 0x01;
    erya_on_write(&E, 0xAAA5, &ios, 1);
    CHECK(E.phone == ERYA_PHONE_IOS, "phone type ios");

    /* 2. navi, byte by byte, both string and number fields */
    const char *navi = "{\"fun\":\"navi\",\"type\":\"0\",\"data\":{\"curStepRetainDistance\":\"100\","
        "\"nextRoadName\":\"世纪路\",\"pathRetainTime\":\"3700\",\"pathRetainDistance\":12345,"
        "\"iconType\":\"3\",\"arriveTime\":\"13:40\"}}";
    uint32_t L = mk_ey(navi, buf);
    write_stream(0xAAA8, buf, L, 1);
    CHECK(navs == 1 && got_nav.active, "navi start");
    CHECK(!strcmp(got_nav.title, "100m"), "step dist %s", got_nav.title);
    CHECK(!strcmp(got_nav.directions, "世纪路"), "road %s", got_nav.directions);
    CHECK(!strcmp(got_nav.duration, "1h01min"), "time %s", got_nav.duration);
    CHECK(!strcmp(got_nav.distance, "12.3km"), "dist %s", got_nav.distance);
    CHECK(!strcmp(got_nav.eta, "13:40") && got_nav.turn == 3, "eta/icon");
    L = mk_ey("{\"fun\":\"navi\",\"type\":2}", buf);
    erya_on_write(&E, 0xAAA8, buf, L);
    CHECK(navs == 2 && !got_nav.active, "navi end");

    /* 2b. undocumented app messages on AAA6, as sent by the iPhone app */
    L = mk_ey("{\"fun\":\"time\",\"data\":{\"timeZone\":\"8\",\"timestamp\":\"1795140207\"}}", buf);
    write_stream(0xAAA6, buf, L, 1);
    CHECK(times == 1 && got_utc == 1795140207u && got_tz == 8, "time sync %u tz %d", got_utc, got_tz);
    L = mk_ey("{\"fun\":\"activate\",\"type\":\"2\"}", buf);
    erya_on_write(&E, 0xAAA6, buf, L);
    CHECK(acts == 1 && got_act == 2 && E.activate == 2, "activate type %d", got_act);
    L = mk_ey("{\"fun\":\"whatever\"}", buf);
    erya_on_write(&E, 0xAAA6, buf, L);
    CHECK(E.st.unknown_fun == 1 && E.st.json_err == 0, "unknown fun counted, not an error");

    /* 3. picture, one EB per write at MTU 247 */
    uint32_t sl, pk;
    uint8_t *s = pic_stream(jpg, jl, 247, -1, &sl, &pk);
    uint8_t *p = s;
    while (p < s + sl) {   /* each EB as one write */
        uint32_t jn2 = (p[2]<<24)|(p[3]<<16)|(p[4]<<8)|p[5], bn = (p[6]<<24)|(p[7]<<16)|(p[8]<<8)|p[9];
        erya_on_write(&E, 0xAAA7, p, (uint16_t)(10 + jn2 + bn)); p += 10 + jn2 + bn;
    }
    CHECK(pics == 1 && got_len == jl && !memcmp(got_pic, jpg, jl), "pic mtu247 (%u pkts)", pk);
    free(s);

    /* 4. same stream, random write boundaries + garbage before */
    s = pic_stream(jpg, jl, 185, -1, &sl, &pk);
    erya_on_write(&E, 0xAAA7, (const uint8_t *)"xxEzzE", 6);
    write_stream(0xAAA7, s, sl, 0);
    CHECK(pics == 2 && got_len == jl && !memcmp(got_pic, jpg, jl), "pic random split + garbage");
    free(s);

    /* 5. dropped packet -> lost, next picture fine */
    s = pic_stream(jpg, jl, 247, 17, &sl, &pk);
    write_stream(0xAAA7, s, sl, 244);
    CHECK(pics == 2 && E.st.pics_lost == 1, "drop -> lost (%u)", E.st.pics_lost);
    free(s);
    s = pic_stream(jpg, jl, 247, -1, &sl, &pk);
    write_stream(0xAAA7, s, sl, 244);
    CHECK(pics == 3 && !memcmp(got_pic, jpg, jl), "recovers after loss");
    free(s);

    /* 6. whole picture in one EB, streamed in 244-byte writes */
    uint8_t *one = malloc(jl + 200);
    char js[96]; snprintf(js, sizeof(js), "{\"fun\":\"pic\",\"num\":0,\"sum\":1,\"size\":%u}", jl);
    L = mk_eb(js, jpg, jl, one);
    write_stream(0xAAA7, one, L, 244);
    CHECK(pics == 4 && got_len == jl && !memcmp(got_pic, jpg, jl), "single EB picture");
    free(one);

    /* 7. too big */
    snprintf(js, sizeof(js), "{\"fun\":\"pic\",\"num\":0,\"sum\":1,\"size\":%u}", 900000u);
    L = mk_eb(js, jpg, 10, buf);
    erya_on_write(&E, 0xAAA7, buf, L);
    CHECK(E.st.pics_too_big == 1 && pics == 4, "too big rejected");

    /* 8. disconnect mid-picture, then a clean picture after reconnect */
    s = pic_stream(jpg, jl, 247, -1, &sl, &pk);
    write_stream(0xAAA7, s, sl / 2, 100);
    erya_on_disconnect(&E);
    CHECK(E.pic == NULL, "buffer freed on disconnect");
    erya_on_connect(&E);
    write_stream(0xAAA7, s, sl, 0);
    CHECK(pics == 5 && !memcmp(got_pic, jpg, jl), "picture after reconnect");
    free(s);

    free(got_pic); frees++;
    erya_on_disconnect(&E);
    CHECK(allocs == frees, "alloc %d == free %d", allocs, frees);
    printf("stats: ey=%u eb=%u ok=%u lost=%u big=%u bad_magic=%u json_err=%u navi=%u\n",
           E.st.frames_ey, E.st.frames_eb, E.st.pics_ok, E.st.pics_lost, E.st.pics_too_big,
           E.st.bad_magic, E.st.json_err, E.st.navi_msgs);
    printf(fails ? "ERYA TESTS FAILED: %d\n" : "ALL ERYA TESTS PASSED\n", fails);
    free(jpg);
    return fails != 0;
}

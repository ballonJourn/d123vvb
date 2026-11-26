/*
 * erya_replay.c - feed a recorded phone write sequence into the firmware's
 * erya_proto.c / erya_json.c and report what the device would display.
 * Record file: repeated { u16 LE uuid16, u16 LE len, len bytes }.
 *   ./erya_replay writes.bin [expected.jpg]
 * Used by tools/erya_js_test.mjs to prove web/erya_sender.js <-> firmware.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "erya_proto.h"

static uint8_t *last_pic; static uint32_t last_len; static int pics, navis;
static int n_notify(void *c, uint16_t u, const uint8_t *b, uint16_t n)
{ (void)c; printf("notify %04X %u bytes\n", u, n); (void)b; return 0; }
static void *n_alloc(void *c, size_t n) { (void)c; return malloc(n); }
static void n_free(void *c, void *p) { (void)c; free(p); }
static uint32_t n_now(void *c) { (void)c; return 0; }
static void n_pic(void *c, uint8_t *b, uint32_t n)
{ (void)c; free(last_pic); last_pic = b; last_len = n; pics++;
  printf("picture %d: %u bytes, SOI %s\n", pics, n, (n > 2 && b[0] == 0xFF && b[1] == 0xD8) ? "ok" : "missing"); }
static void n_navi(void *c, const struct nav_state *ns)
{ (void)c; navis++;
  printf("navi %d: active=%u step=%s road=%s left=%s dist=%s eta=%s icon=%u\n", navis, ns->active,
         ns->title, ns->directions, ns->duration, ns->distance, ns->eta, ns->turn); }

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s writes.bin [expected.jpg]\n", argv[0]); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    struct erya e;
    struct erya_cfg cfg = { .pic_w = 800, .pic_h = 480, .pic_q = 160, .max_pic_bytes = 512 * 1024 };
    strcpy(cfg.mac, "CCC4B2B64DA0"); strcpy(cfg.pic_type, "420");
    struct erya_ops ops = { .notify = n_notify, .alloc = n_alloc, .free = n_free, .now_ms = n_now,
                            .on_picture = n_pic, .on_navi = n_navi, .ctx = NULL };
    erya_init(&e, &cfg, &ops);
    erya_on_connect(&e);
    uint8_t hdr[4], buf[65536]; unsigned writes = 0, maxw = 0;
    while (fread(hdr, 1, 4, f) == 4) {
        uint16_t u = hdr[0] | (hdr[1] << 8), n = hdr[2] | (hdr[3] << 8);
        if (fread(buf, 1, n, f) != n) { fprintf(stderr, "truncated record\n"); return 2; }
        erya_on_write(&e, u, buf, n);
        writes++; if (n > maxw) maxw = n;
    }
    fclose(f);
    printf("writes=%u max_write=%u phone=%d pics_ok=%u lost=%u json_err=%u bad_magic=%u\n",
           writes, maxw, e.phone, e.st.pics_ok, e.st.pics_lost, e.st.json_err, e.st.bad_magic);
    int rc = 0;
    if (argc > 2) {
        FILE *j = fopen(argv[2], "rb");
        if (!j) { perror(argv[2]); return 2; }
        fseek(j, 0, SEEK_END); long jl = ftell(j); fseek(j, 0, SEEK_SET);
        uint8_t *exp = malloc(jl);
        if (fread(exp, 1, jl, j) != (size_t)jl) return 2;
        fclose(j);
        rc = !(last_pic && last_len == (uint32_t)jl && !memcmp(last_pic, exp, jl));
        printf("last picture %s expected JPEG (%ld bytes)\n", rc ? "DIFFERS FROM" : "IDENTICAL to", jl);
        free(exp);
    }
    if (e.st.json_err || e.st.bad_magic || e.st.pics_lost) rc = 1;
    free(last_pic);
    erya_on_disconnect(&e);
    return rc;
}

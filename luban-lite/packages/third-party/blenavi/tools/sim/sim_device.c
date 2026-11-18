/* Host stand-in for the D133: the branch's own ble_img_rx.c behind a pipe.
 * stdin : [type u8][len u16 LE][bytes]  1=CTRL write 2=DATA write 3=disconnect 4=set mtu(u16)
 * stdout: [len u16 LE][bytes]           CTRL notifications
 * stderr: "FRAME id fmt len crc" for every frame handed to the display */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ble_img_rx.h"

static struct bir rx;
static uint16_t mtu = 247;

static int n_notify(void *c, const uint8_t *b, uint16_t n)
{
    (void)c;
    if (n > mtu - 3) { fprintf(stderr, "NOTIFY_TOO_LONG %u > %u\n", n, mtu - 3); return -1; }
    uint8_t h[2] = { n & 0xFF, n >> 8 };
    fwrite(h, 1, 2, stdout); fwrite(b, 1, n, stdout); fflush(stdout);
    return 0;
}
static void *n_alloc(void *c, size_t n) { (void)c; return malloc(n); }
static void n_free(void *c, void *p) { (void)c; free(p); }
static uint32_t n_now(void *c)
{
    (void)c; struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}
static int n_frame(void *c, const struct bir_frame *f)
{
    (void)c;
    fprintf(stderr, "FRAME %u %u %u %08x\n", f->frame_id, f->fmt, f->len,
            bnp_crc32(0, f->data, f->len));
    bir_release(&rx, f->layer);            /* disp_sink does this after the blit */
    return 0;
}
static void n_nav(void *c, const uint8_t *t, uint16_t n) { (void)c; fprintf(stderr, "NAV %u\n", n); }

int main(void)
{
    struct bir_ops ops = { n_notify, n_alloc, n_free, n_now, n_frame, n_nav, 0 };
    bir_init(&rx, &ops, 1024 * 600 * 2);
    bir_set_mtu(&rx, mtu);
    static uint8_t buf[65536];
    for (;;) {
        uint8_t h[3];
        if (fread(h, 1, 3, stdin) != 3) break;
        uint16_t n = h[1] | (h[2] << 8);
        if (n && fread(buf, 1, n, stdin) != n) break;
        if (h[0] == 1) {
            if (n && buf[0] == 0x20) {     /* GET_PARAMS lives in bnp_service.cpp */
                uint8_t p[16] = { 0xA0, mtu & 0xFF, mtu >> 8, 12, 0, 0, 0, 40, 1, 2, 2, 251, 0, 251, 0, 0 };
                n_notify(0, p, sizeof p);
            } else bir_on_ctrl(&rx, buf, n);
        } else if (h[0] == 2) {
            if (n > mtu - 3) { fprintf(stderr, "WRITE_TOO_LONG %u\n", n); continue; }
            bir_on_data(&rx, buf, n);
        } else if (h[0] == 3) bir_on_disconnect(&rx);
        else if (h[0] == 4) { mtu = buf[0] | (buf[1] << 8); bir_set_mtu(&rx, mtu); }
    }
    fprintf(stderr, "STATS ok=%u crc_err=%u resumes=%u aborts=%u rx=%u dup=%u bad=%u\n",
            rx.st.frames_ok, rx.st.frames_crc_err, rx.st.resumes, rx.st.aborts,
            rx.st.chunks_rx, rx.st.chunks_dup, rx.st.chunks_bad);
    return 0;
}

/* host fuzz of the patched hci_h4.c: byte loss + random chunking */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "os/os_mbuf.h"
#include "nimble/transport.h"
#include "nimble/transport/hci_h4.h"

/* pools sized like the board: ACL_FROM_LL 10, EVT 4 + 16 discardable */
static int acl_live, evt_live, acl_max = 10, evt_max = 20, acl_fail, evt_fail;
struct os_mbuf *ble_transport_alloc_acl_from_ll(void)
{ if (acl_live >= acl_max) { acl_fail++; return NULL; } acl_live++;
  struct os_mbuf *m = calloc(1, sizeof(*m)); m->cap = 292; m->data = malloc(m->cap); return m; }
struct os_mbuf *ble_transport_alloc_acl_from_hs(void) { return NULL; }
struct os_mbuf *ble_transport_alloc_iso_from_hs(void) { return NULL; }
void *ble_transport_alloc_evt(int d) { (void)d; if (evt_live >= evt_max) { evt_fail++; return NULL; } evt_live++; return malloc(70); }
void *ble_transport_alloc_cmd(void) { return NULL; }
void ble_transport_free(void *b) { if (b) { evt_live--; free(b); } }
int os_mbuf_append(struct os_mbuf *om, const void *d, uint16_t n)
{ if (om->len + n > om->cap) { uint16_t k = om->cap - om->len; memcpy(om->data + om->len, d, k); om->len += k; return 1; }
  memcpy(om->data + om->len, d, n); om->len += n; return 0; }
int os_mbuf_free_chain(struct os_mbuf *om) { if (om) { acl_live--; free(om->data); free(om); } return 0; }

static int good_acl, good_evt, bad;
static int frame_cb(uint8_t t, void *data)
{
    /* host consumes immediately (worst case would be slower) */
    if (t == HCI_H4_ACL) {
        struct os_mbuf *m = data;
        /* our generator writes payload byte i = (seq + i) & 0xff, seq in hdr handle */
        uint16_t L = m->data[2] | (m->data[3] << 8);
        uint8_t seq = m->data[0]; int ok = (m->len == L + 4);
        for (int i = 0; ok && i < L; i++) ok = m->data[4 + i] == (uint8_t)(seq + i);
        if (ok) good_acl++; else bad++;
        os_mbuf_free_chain(m);
    } else {
        uint8_t *e = data; if (e[0] == 0x13) good_evt++; else bad++;
        ble_transport_free(e);
    }
    return 0;
}

static size_t gen(uint8_t *o, int nframes, unsigned *seed)
{
    size_t p = 0;
    for (int f = 0; f < nframes; f++) {
        if (rand_r(seed) % 5 == 0) {          /* Number Of Completed Packets event */
            o[p++] = 0x04; o[p++] = 0x13; o[p++] = 5; o[p++] = 1; o[p++] = 0x10; o[p++] = 0; o[p++] = 1; o[p++] = 0;
        } else {
            uint16_t L = 27 + rand_r(seed) % 225;   /* ACL data up to 251 */
            uint8_t seq = rand_r(seed);
            o[p++] = 0x02; o[p++] = seq; o[p++] = 0x20; o[p++] = L & 0xff; o[p++] = L >> 8;
            for (int i = 0; i < L; i++) o[p++] = (uint8_t)(seq + i);
        }
    }
    return p;
}

int main(void)
{
    static uint8_t s[4 << 20];
    unsigned seed = 1234;
    struct hci_h4_sm sm;
    hci_h4_sm_init(&sm, &hci_h4_allocs_from_ll, frame_cb);
    size_t n = gen(s, 20000, &seed);
    /* phase 1: lossy stream: drop 1..300 byte runs ~ every 3 KB */
    size_t i = 0, dropped_runs = 0;
    while (i < n) {
        size_t k = 1 + rand_r(&seed) % 700;
        if (k > n - i) k = n - i;
        if (rand_r(&seed) % 5 == 0) { i += 1 + rand_r(&seed) % 300; dropped_runs++; continue; }
        hci_h4_sm_rx(&sm, s + i, (uint16_t)k);
        i += k;
    }
    int g1 = good_acl, e1 = good_evt;
    /* phase 2: clean stream must be parsed after resync */
    good_acl = good_evt = 0; int bad1 = bad; bad = 0;
    size_t n2 = gen(s, 2000, &seed);
    for (i = 0; i < n2; ) { size_t k = 1 + rand_r(&seed) % 700; if (k > n2 - i) k = n2 - i; hci_h4_sm_rx(&sm, s + i, (uint16_t)k); i += k; }
    printf("lossy: %zu drop runs, good acl %d evt %d, corrupt delivered %d, resyncs %u, acl alloc fail %d evt alloc fail %d\n",
           dropped_runs, g1, e1, bad1, hci_h4_resync_count, acl_fail, evt_fail);
    printf("clean after loss: good acl %d evt %d (bad %d) of 2000 frames\n", good_acl, good_evt, bad);
    printf("live buffers at end: acl %d evt %d\n", acl_live, evt_live);
    int ok = (good_acl + good_evt >= 1995) && bad == 0 && acl_live <= 1 && evt_live <= 1;
    printf(ok ? "H4 FUZZ PASSED\n" : "H4 FUZZ FAILED\n");
    return !ok;
}

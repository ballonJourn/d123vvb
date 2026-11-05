/*
 * dsstat: one line of JSON per TCP connection, see dsstat.h.
 * Same source for the board (RT-Thread thread, lwIP via SAL) and the host
 * test build (pthread). Uses no pthread slot on the board: dataServer already
 * takes up to 1 + POOL_SIZE + clients of PTHREAD_NUM_MAX.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dsstat.h"

#ifdef DATASERVER_RTTHREAD
#include <rtthread.h>
#define st_log(fmt, ...)  rt_kprintf("[dsstat] " fmt "\n", ##__VA_ARGS__)
#else
#include <pthread.h>
#include <time.h>
#define st_log(fmt, ...)  fprintf(stderr, "[dsstat] " fmt "\n", ##__VA_ARGS__)
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

static struct {
    int                 port;
    int                 fd;
    volatile int        running;
    netmon_snapshot_fn  ifs;
    netmon_forwarded_fn fw;
    netmon_lwip_fn      lw;
} S = { 0, -1, 0, NULL, NULL, NULL };

/* ------------------------------------------------------------ JSON ---- */
/* Interface names come from lwIP (two letters + digits) or /proc on the host;
   anything outside [A-Za-z0-9._-] is replaced so the output stays valid JSON. */
static void safe_name(char *dst, size_t len, const char *src)
{
    size_t i;
    for (i = 0; i + 1 < len && src[i]; i++) {
        char c = src[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        dst[i] = ok ? c : '_';
    }
    dst[i] = '\0';
}

/* newlib-nano may lack %llu: print 64-bit values by hand */
static const char *u64s(char *b, uint64_t v)
{
    char *p = b + 20;
    *p = '\0';
    do { *--p = (char)('0' + v % 10u); v /= 10u; } while (v);
    return p;
}

#define PUT(...) do {                                             \
        int n_ = snprintf(buf + pos, len - pos, __VA_ARGS__);     \
        if (n_ < 0 || (size_t)n_ >= len - pos) return -1;         \
        pos += (size_t)n_;                                        \
    } while (0)

int dsstat_format(char *buf, size_t len, uint64_t uptime_ms,
                  const netmon_if_t *ifs, int nif, int64_t ip_forwarded,
                  int ds_running, const dataserver_stats_t *ds,
                  const netmon_lwip_t *lw)
{
    size_t pos = 0;
    char name[sizeof(ifs[0].name)];
    char n1[21], n2[21];
    int i;

    if (buf == NULL || len == 0)
        return -1;
    PUT("{\"v\":1,\"uptime_ms\":%s,\"ifs\":[", u64s(n1, uptime_ms));
    for (i = 0; i < nif; i++) {
        safe_name(name, sizeof(name), ifs[i].name);
        PUT("%s{\"name\":\"%s\",\"ip\":\"%u.%u.%u.%u\",\"up\":%u,"
            "\"rx_bytes\":%lu,\"tx_bytes\":%lu,\"rx_pkts\":%lu,\"tx_pkts\":%lu,"
            "\"rx_drop\":%lu,\"tx_err\":%lu}",
            i ? "," : "", name, ifs[i].ip[0], ifs[i].ip[1], ifs[i].ip[2], ifs[i].ip[3],
            ifs[i].up ? 1u : 0u,
            (unsigned long)ifs[i].rx_bytes, (unsigned long)ifs[i].tx_bytes,
            (unsigned long)ifs[i].rx_pkts, (unsigned long)ifs[i].tx_pkts,
            (unsigned long)ifs[i].rx_dropped, (unsigned long)ifs[i].tx_errors);
    }
    PUT("],\"ip_forwarded\":%s%s,\"lwip\":", ip_forwarded < 0 ? "-" : "",
        u64s(n1, ip_forwarded < 0 ? 1u : (uint64_t)ip_forwarded));
    if (lw) {
        PUT("{\"stats\":%u", lw->stats ? 1u : 0u);
        if (lw->stats)
            PUT(",\"ip_drop\":%lu,\"ip_rterr\":%lu,\"ip_chkerr\":%lu,\"ip_lenerr\":%lu,\"ip_memerr\":%lu,"
                "\"link_drop\":%lu,\"link_memerr\":%lu,\"link_chkerr\":%lu,\"link_lenerr\":%lu,"
                "\"pbuf_err\":%lu,\"pbuf_used\":%lu,\"pbuf_max\":%lu,"
                "\"inpkt_err\":%lu,\"inpkt_used\":%lu,\"inpkt_max\":%lu",
                (unsigned long)lw->ip_drop, (unsigned long)lw->ip_rterr, (unsigned long)lw->ip_chkerr,
                (unsigned long)lw->ip_lenerr, (unsigned long)lw->ip_memerr,
                (unsigned long)lw->link_drop, (unsigned long)lw->link_memerr,
                (unsigned long)lw->link_chkerr, (unsigned long)lw->link_lenerr,
                (unsigned long)lw->pbuf_err, (unsigned long)lw->pbuf_used, (unsigned long)lw->pbuf_max,
                (unsigned long)lw->inpkt_err, (unsigned long)lw->inpkt_used, (unsigned long)lw->inpkt_max);
        PUT(",\"limits\":{\"pbuf_pool\":%lu,\"inpkt_pool\":%lu,\"mbox\":%lu}}",
            (unsigned long)lw->pbuf_pool_size, (unsigned long)lw->inpkt_pool_size,
            (unsigned long)lw->mbox_size);
    } else
        PUT("null");
    PUT(",\"dataserver\":");
    if (ds)
        PUT("{\"running\":%d,\"connections\":%lu,\"requests_done\":%lu,"
            "\"files_sent\":%lu,\"rejected\":%lu,\"queued\":%lu,\"bytes_sent\":%s}}",
            ds_running ? 1 : 0, (unsigned long)ds->connections,
            (unsigned long)ds->requests_done, (unsigned long)ds->files_sent,
            (unsigned long)ds->rejected, (unsigned long)ds->queued,
            u64s(n2, ds->bytes_sent));
    else
        PUT("null}");
    return (int)pos;
}

/* ---------------------------------------------------------- server ---- */
static uint64_t uptime_ms(void)
{
#ifdef DATASERVER_RTTHREAD
    return (uint64_t)rt_tick_get() * 1000u / RT_TICK_PER_SECOND;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
#endif
}

static void serve_one(int c, char *json)
{
    netmon_if_t ifs[NETMON_MAX_IF];
    dataserver_stats_t ds;
    netmon_lwip_t lw;
    struct timeval tv = { 2, 0 };
    int nif = S.ifs ? S.ifs(ifs, NETMON_MAX_IF) : 0;
    int64_t fw = S.fw ? S.fw() : -1;
    int n, off = 0;

    if (S.lw)
        S.lw(&lw);
    dataserver_get_stats(&ds);
    n = dsstat_format(json, DSSTAT_JSON_MAX - 1, uptime_ms(), ifs, nif, fw,
                      dataserver_running(), &ds, S.lw ? &lw : NULL);
    if (n < 0)
        n = snprintf(json, DSSTAT_JSON_MAX - 1, "{\"v\":1,\"error\":\"overflow\"}");
    json[n++] = '\n';
    setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    while (off < n) {
        int k = (int)send(c, json + off, n - off, MSG_NOSIGNAL);
        if (k <= 0)
            break;
        off += k;
    }
}

static void accept_loop(void)
{
    char *json = (char *)malloc(DSSTAT_JSON_MAX);

    if (json == NULL) {
        st_log("out of memory");
        S.running = 0;
        return;
    }
    while (S.running) {
        int c = accept(S.fd, NULL, NULL);
        if (c < 0) {
            if (errno == EINTR)
                continue;
#ifdef DATASERVER_RTTHREAD
            rt_thread_mdelay(100);   /* e.g. out of sockets: retry later */
#else
            usleep(100 * 1000);
#endif
            continue;
        }
        serve_one(c, json);
        close(c);
    }
    free(json);
}

#ifdef DATASERVER_RTTHREAD
static void thread_entry(void *arg) { (void)arg; accept_loop(); }
#else
static void *thread_entry(void *arg) { (void)arg; accept_loop(); return NULL; }
#endif

int dsstat_start(int port, netmon_snapshot_fn ifs, netmon_forwarded_fn fw, netmon_lwip_fn lw)
{
    struct sockaddr_in a;
    int opt = 1;

    if (S.running)
        return 0;
    S.port = port;
    S.ifs = ifs;
    S.fw = fw;
    S.lw = lw;
    S.fd = socket(AF_INET, SOCK_STREAM, 0);
    if (S.fd < 0) {
        st_log("socket failed");
        return -1;
    }
    setsockopt(S.fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((uint16_t)port);
    if (bind(S.fd, (struct sockaddr *)&a, sizeof(a)) < 0 || listen(S.fd, 2) < 0) {
        st_log("bind/listen on port %d failed", port);
        close(S.fd);
        S.fd = -1;
        return -1;
    }
    S.running = 1;
#ifdef DATASERVER_RTTHREAD
    {
        rt_thread_t t = rt_thread_create("dsstat", thread_entry, RT_NULL, 4096,
                                         RT_THREAD_PRIORITY_MAX / 2 + 2, 10);
        if (t == RT_NULL || rt_thread_startup(t) != RT_EOK) {
            st_log("thread create failed");
            S.running = 0;
            close(S.fd);
            S.fd = -1;
            return -1;
        }
    }
#else
    {
        pthread_t t;
        if (pthread_create(&t, NULL, thread_entry, NULL) != 0) {
            S.running = 0;
            close(S.fd);
            S.fd = -1;
            return -1;
        }
        pthread_detach(t);
    }
#endif
    st_log("stats on port %d", port);
    return 0;
}

int dsstat_running(void)
{
    return S.running;
}

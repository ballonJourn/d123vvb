/*
 * netmon: per network interface byte/packet counters for the board's
 * Ethernet (e0) and WiFi (w0/w1) interfaces, so the PC dashboard can show the
 * traffic of the dataServer, the FTP server and e0 <-> w1 IP forwarding.
 *
 * Counters are 32-bit and wrap; readers take differences modulo 2^32.
 */
#ifndef NETMON_H
#define NETMON_H

#include <stdint.h>

#define NETMON_MAX_IF  6

typedef struct {
    char     name[8];      /* lwIP name + number: "e0", "w1", "lo"... */
    uint8_t  ip[4];        /* IPv4 address, dotted order */
    uint8_t  up;           /* interface and link up */
    uint32_t rx_bytes;     /* frames the stack accepted (netif->input returned ERR_OK) */
    uint32_t tx_bytes;     /* frames the driver accepted (netif->linkoutput returned ERR_OK) */
    uint32_t rx_pkts;
    uint32_t tx_pkts;
    uint32_t rx_dropped;   /* frames the stack refused: netif->input returned an error. This is where
                              tcpip_input() drops when the tcpip mailbox or its message pool is full,
                              and lwIP keeps no statistic for that case. */
    uint32_t tx_errors;    /* frames the driver refused: netif->linkoutput returned an error */
} netmon_if_t;

/* lwIP internal counters that explain where a forwarded stream loses packets, plus the compile-time
   limits those counters have to be read against. The counters need RT_LWIP_STATS (stats = 1). */
typedef struct {
    uint8_t  stats;                    /* 1: counters below are valid */
    uint32_t ip_drop, ip_rterr, ip_chkerr, ip_lenerr, ip_memerr;
    uint32_t link_drop, link_memerr, link_chkerr, link_lenerr;
    uint32_t pbuf_err, pbuf_used, pbuf_max;      /* PBUF_POOL: receive buffers */
    uint32_t inpkt_err, inpkt_used, inpkt_max;   /* MEMP_TCPIP_MSG_INPKT: messages queued to the tcpip thread */
    uint32_t pbuf_pool_size;           /* PBUF_POOL_SIZE */
    uint32_t inpkt_pool_size;          /* MEMP_NUM_TCPIP_MSG_INPKT */
    uint32_t mbox_size;                /* TCPIP_MBOX_SIZE */
} netmon_lwip_t;

/* Interface counters provider: fills at most max entries, returns the count. */
typedef int (*netmon_snapshot_fn)(netmon_if_t *out, int max);
/* Forwarded IP packet counter provider, -1 when not available. */
typedef int64_t (*netmon_forwarded_fn)(void);
/* lwIP counters and limits provider. */
typedef void (*netmon_lwip_fn)(netmon_lwip_t *out);

#ifdef DATASERVER_RTTHREAD
/* Board implementation (netmon.c): hooks every lwIP netif not hooked yet. */
int     netmon_hook_all(void);
int     netmon_snapshot(netmon_if_t *out, int max);
int64_t netmon_ip_forwarded(void);
void    netmon_lwip(netmon_lwip_t *out);
#endif

#endif

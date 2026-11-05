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
    uint32_t rx_bytes;     /* frames handed to the stack (input) */
    uint32_t tx_bytes;     /* frames handed to the driver (linkoutput) */
    uint32_t rx_pkts;
    uint32_t tx_pkts;
} netmon_if_t;

/* Interface counters provider: fills at most max entries, returns the count. */
typedef int (*netmon_snapshot_fn)(netmon_if_t *out, int max);
/* Forwarded IP packet counter provider, -1 when not available. */
typedef int64_t (*netmon_forwarded_fn)(void);

#ifdef DATASERVER_RTTHREAD
/* Board implementation (netmon.c): hooks every lwIP netif not hooked yet. */
int     netmon_hook_all(void);
int     netmon_snapshot(netmon_if_t *out, int max);
int64_t netmon_ip_forwarded(void);
#endif

#endif

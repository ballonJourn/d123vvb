/*
 * netmon: byte/packet counters on every lwIP netif (e0, w0, w1).
 *
 * The hook technique (save netif->input / netif->linkoutput, swap in a wrapper
 * with interrupts disabled, call the saved function from the wrapper) is taken
 * from RT-Thread-packages/netutils tcpdump/tcpdump.c:
 *
 *   Copyright (c) 2006-2022, RT-Thread Development Team
 *   SPDX-License-Identifier: Apache-2.0
 *   https://github.com/RT-Thread-packages/netutils (b77ba3f)
 *
 * Changes from tcpdump: every netif instead of one, no packet copy, no
 * mailbox and no malloc on the data path, only counters; a netif whose
 * input/linkoutput was replaced again (driver re-init) is hooked again.
 *
 * RX is counted in netif->input (called by the RT-Thread eth rx thread,
 * see port/ethernetif.c), TX in netif->linkoutput (called by etharp from
 * the tcpip thread). Forwarded e0 <-> w1 packets are counted on both
 * interfaces, plus lwip_stats.ip.fw when RT_LWIP_STATS is enabled.
 */
#ifdef DATASERVER_RTTHREAD

#include <rtthread.h>
#include <rthw.h>
#include <stdio.h>
#include <string.h>
#include "lwip/opt.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/stats.h"
#include "netmon.h"

typedef struct {
    struct netif           *nif;      /* NULL: free slot */
    netif_input_fn          input;    /* saved originals */
    netif_linkoutput_fn     linkoutput;
    volatile uint32_t       rx_bytes, tx_bytes, rx_pkts, tx_pkts;
} netmon_entry_t;

static netmon_entry_t s_tab[NETMON_MAX_IF];

static netmon_entry_t *find(const struct netif *nif)
{
    int i;
    for (i = 0; i < NETMON_MAX_IF; i++)
        if (s_tab[i].nif == nif)
            return &s_tab[i];
    return RT_NULL;
}

/* rx: same place tcpdump's _netif_input taps */
static err_t netmon_input(struct pbuf *p, struct netif *inp)
{
    netmon_entry_t *e = find(inp);

    if (e == RT_NULL || e->input == RT_NULL)
        return ERR_IF;                 /* caller frees p */
    e->rx_bytes += p->tot_len;         /* read before input() may free p */
    e->rx_pkts++;
    return e->input(p, inp);
}

/* tx: same place tcpdump's _netif_linkoutput taps */
static err_t netmon_linkoutput(struct netif *nif, struct pbuf *p)
{
    netmon_entry_t *e = find(nif);

    if (e == RT_NULL || e->linkoutput == RT_NULL)
        return ERR_IF;
    e->tx_bytes += p->tot_len;
    e->tx_pkts++;
    return e->linkoutput(nif, p);
}

int netmon_hook_all(void)
{
    struct netif *n;
    netmon_entry_t *e;
    rt_base_t level;
    int hooked = 0, i;

    level = rt_hw_interrupt_disable();
    for (n = netif_list; n != RT_NULL; n = n->next) {
        if (n->name[0] == 'l' && n->name[1] == 'o')
            continue;                  /* loopback: no link layer */
        if (n->input == RT_NULL || n->linkoutput == RT_NULL)
            continue;
        if (n->input == netmon_input && n->linkoutput == netmon_linkoutput)
            continue;                  /* already ours */
        e = find(n);
        if (e == RT_NULL) {
            for (i = 0; i < NETMON_MAX_IF && s_tab[i].nif != RT_NULL; i++)
                ;
            if (i == NETMON_MAX_IF)
                break;                 /* table full */
            e = &s_tab[i];
            memset(e, 0, sizeof(*e));
        }
        /* fill the entry before the wrappers can see it, as tcpdump does */
        if (n->input != netmon_input)
            e->input = n->input;
        if (n->linkoutput != netmon_linkoutput)
            e->linkoutput = n->linkoutput;
        e->nif = n;
        n->input = netmon_input;
        n->linkoutput = netmon_linkoutput;
        hooked++;
    }
    rt_hw_interrupt_enable(level);
    return hooked;
}

int netmon_snapshot(netmon_if_t *out, int max)
{
    struct netif *n;
    netmon_entry_t *e;
    int cnt = 0;

    netmon_hook_all();                 /* pick up interfaces created later */
    /* walk netif_list, never dereference a table pointer: a removed netif
       simply stops being listed */
    for (n = netif_list; n != RT_NULL && cnt < max; n = n->next) {
        e = find(n);
        if (e == RT_NULL)
            continue;
        memset(&out[cnt], 0, sizeof(out[cnt]));
        snprintf(out[cnt].name, sizeof(out[cnt].name), "%c%c%u",
                 n->name[0], n->name[1], (unsigned)n->num);
#if LWIP_VERSION_MAJOR >= 2
        out[cnt].ip[0] = ip4_addr1(netif_ip4_addr(n));
        out[cnt].ip[1] = ip4_addr2(netif_ip4_addr(n));
        out[cnt].ip[2] = ip4_addr3(netif_ip4_addr(n));
        out[cnt].ip[3] = ip4_addr4(netif_ip4_addr(n));
#else
        out[cnt].ip[0] = ip4_addr1(&n->ip_addr);
        out[cnt].ip[1] = ip4_addr2(&n->ip_addr);
        out[cnt].ip[2] = ip4_addr3(&n->ip_addr);
        out[cnt].ip[3] = ip4_addr4(&n->ip_addr);
#endif
        out[cnt].up = netif_is_up(n) && netif_is_link_up(n);
        out[cnt].rx_bytes = e->rx_bytes;
        out[cnt].tx_bytes = e->tx_bytes;
        out[cnt].rx_pkts = e->rx_pkts;
        out[cnt].tx_pkts = e->tx_pkts;
        cnt++;
    }
    return cnt;
}

int64_t netmon_ip_forwarded(void)
{
#if LWIP_STATS && IP_STATS
    return (int64_t)lwip_stats.ip.fw;
#else
    return -1;
#endif
}

static int netmon(int argc, char **argv)
{
    netmon_if_t t[NETMON_MAX_IF];
    int n, i;
    int64_t fw;

    (void)argc; (void)argv;
    n = netmon_snapshot(t, NETMON_MAX_IF);
    rt_kprintf("%-4s %-15s %-4s %12s %10s %12s %10s\n",
               "if", "ip", "up", "rx bytes", "rx pkts", "tx bytes", "tx pkts");
    for (i = 0; i < n; i++)
        rt_kprintf("%-4s %3u.%u.%u.%-7u %-4s %12u %10u %12u %10u\n", t[i].name,
                   t[i].ip[0], t[i].ip[1], t[i].ip[2], t[i].ip[3],
                   t[i].up ? "yes" : "no",
                   (unsigned)t[i].rx_bytes, (unsigned)t[i].rx_pkts,
                   (unsigned)t[i].tx_bytes, (unsigned)t[i].tx_pkts);
    fw = netmon_ip_forwarded();
    if (fw >= 0)
        rt_kprintf("ip forwarded packets: %u\n", (unsigned)fw);
    else
        rt_kprintf("ip forwarded packets: n/a (enable RT_LWIP_STATS)\n");
    return 0;
}
MSH_CMD_EXPORT(netmon, per-interface traffic counters (e0/w0/w1));

#endif /* DATASERVER_RTTHREAD */

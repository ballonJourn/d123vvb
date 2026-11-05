/*
 * dsstat: live statistics endpoint for the PC dashboard.
 *
 * A client connects to the stats port, the board writes ONE line of JSON and
 * closes the socket. No request is read, so `nc <board> 9001` works too.
 *
 * {"v":1,"uptime_ms":123456,
 *  "ifs":[{"name":"e0","ip":"192.168.1.1","up":1,
 *          "rx_bytes":1,"tx_bytes":2,"rx_pkts":3,"tx_pkts":4}, ...],
 *          ...,"rx_drop":0,"tx_err":0}, ...],   (frames the stack / the driver refused)
 *  "ip_forwarded":17,                     (-1: RT_LWIP_STATS disabled)
 *  "lwip":{"stats":1,"ip_drop":0,"ip_rterr":0,"ip_chkerr":0,"ip_lenerr":0,"ip_memerr":0,
 *          "link_drop":0,"link_memerr":0,"link_chkerr":0,"link_lenerr":0,
 *          "pbuf_err":0,"pbuf_used":3,"pbuf_max":40,"inpkt_err":0,"inpkt_used":1,"inpkt_max":9,
 *          "limits":{"pbuf_pool":64,"inpkt_pool":128,"mbox":8}},   (null on the host build;
 *          counters are present only with "stats":1, the limits always)
 *  "dataserver":{"running":1,"connections":3,"requests_done":3,
 *                "files_sent":41,"rejected":0,"queued":0,"bytes_sent":831488}}
 *
 * Interface counters are uint32 and wrap; take differences modulo 2^32.
 */
#ifndef DSSTAT_H
#define DSSTAT_H

#include <stddef.h>
#include <stdint.h>
#include "dataserver.h"
#include "netmon.h"

#define DSSTAT_JSON_MAX  2560

/* Returns the JSON length (without the newline), or -1 if buf is too small. */
int dsstat_format(char *buf, size_t len, uint64_t uptime_ms,
                  const netmon_if_t *ifs, int nif, int64_t ip_forwarded,
                  int ds_running, const dataserver_stats_t *ds,
                  const netmon_lwip_t *lw);

/* Start the endpoint thread. Providers may be NULL (no interfaces / -1 / "lwip":null). */
int dsstat_start(int port, netmon_snapshot_fn ifs, netmon_forwarded_fn fw, netmon_lwip_fn lw);
int dsstat_running(void);

#endif

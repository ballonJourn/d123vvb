/*
 * dsstat: live statistics endpoint for the PC dashboard.
 *
 * A client connects to the stats port, the board writes ONE line of JSON and
 * closes the socket. No request is read, so `nc <board> 9001` works too.
 *
 * {"v":1,"uptime_ms":123456,
 *  "ifs":[{"name":"e0","ip":"192.168.1.1","up":1,
 *          "rx_bytes":1,"tx_bytes":2,"rx_pkts":3,"tx_pkts":4}, ...],
 *  "ip_forwarded":17,                     (-1: RT_LWIP_STATS disabled)
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

#define DSSTAT_JSON_MAX  1536

/* Returns the JSON length (without the newline), or -1 if buf is too small. */
int dsstat_format(char *buf, size_t len, uint64_t uptime_ms,
                  const netmon_if_t *ifs, int nif, int64_t ip_forwarded,
                  int ds_running, const dataserver_stats_t *ds);

/* Start the endpoint thread. Providers may be NULL (no interfaces / -1). */
int dsstat_start(int port, netmon_snapshot_fn ifs, netmon_forwarded_fn fw);
int dsstat_running(void);

#endif

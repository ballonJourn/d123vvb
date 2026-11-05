/*
 * Worst case for dsstat_format(): NETMON_MAX_IF interfaces and every counter at its maximum.
 * Prints the JSON line and exits non-zero when it does not fit DSSTAT_JSON_MAX, so the stats
 * endpoint can never send a truncated (invalid) line. Run by tests/test_dsstat.py.
 */
#include <stdio.h>
#include <string.h>
#include "dsstat.h"

int main(void)
{
    static char buf[DSSTAT_JSON_MAX];
    netmon_if_t ifs[NETMON_MAX_IF];
    netmon_lwip_t lw;
    dataserver_stats_t ds;
    int i, n;

    memset(ifs, 0, sizeof(ifs));
    for (i = 0; i < NETMON_MAX_IF; i++) {
        strcpy(ifs[i].name, "w1");
        ifs[i].ip[0] = ifs[i].ip[1] = ifs[i].ip[2] = ifs[i].ip[3] = 255;
        ifs[i].up = 1;
        ifs[i].rx_bytes = ifs[i].tx_bytes = ifs[i].rx_pkts = ifs[i].tx_pkts = 0xffffffffu;
        ifs[i].rx_dropped = ifs[i].tx_errors = 0xffffffffu;
    }
    memset(&lw, 0xff, sizeof(lw));
    lw.stats = 1;
    memset(&ds, 0xff, sizeof(ds));
    n = dsstat_format(buf, sizeof(buf) - 1, 0xffffffffffffffffULL, ifs, NETMON_MAX_IF,
                      0x7fffffffffffffffLL, 1, &ds, &lw);
    if (n < 0) {
        fprintf(stderr, "does not fit in %d bytes\n", DSSTAT_JSON_MAX);
        return 1;
    }
    printf("%s\n", buf);
    fprintf(stderr, "%d of %d bytes\n", n, DSSTAT_JSON_MAX - 1);
    return 0;
}

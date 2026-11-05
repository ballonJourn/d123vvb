/*
 * Host runner for the C port, same command line as upstream dataServer:
 *   dataserver_c -p <port> -s <thread_pool_size> -q <queue_size> -b <block_size> [-r <root>]
 *                [-S <stats_port>]
 * Used to test the board code on a PC against the upstream remoteClient.
 * -S starts the dsstat endpoint; interface counters come from /proc/net/dev
 * (the board uses netmon.c hooks on lwIP instead).
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "dataserver.h"
#include "dsstat.h"

/* /proc/net/dev: "  name: rx_bytes rx_pkts e d f f c m tx_bytes tx_pkts ..." */
static int proc_snapshot(netmon_if_t *out, int max)
{
    FILE *f = fopen("/proc/net/dev", "r");
    char line[512];
    int n = 0;

    if (f == NULL)
        return 0;
    while (n < max && fgets(line, sizeof(line), f)) {
        char *colon = strchr(line, ':'), *name = line;
        unsigned long long v[10];
        if (colon == NULL)
            continue;                 /* header lines */
        *colon = '\0';
        while (*name == ' ')
            name++;
        if (sscanf(colon + 1, "%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9]) != 10)
            continue;
        memset(&out[n], 0, sizeof(out[n]));
        {   /* copy, truncating long names */
            size_t k;
            for (k = 0; k + 1 < sizeof(out[n].name) && name[k]; k++)
                out[n].name[k] = name[k];
        }
        if (strcmp(name, "lo") == 0) {
            out[n].ip[0] = 127; out[n].ip[3] = 1;
        }
        out[n].up = 1;
        out[n].rx_bytes = (uint32_t)v[0];
        out[n].rx_pkts  = (uint32_t)v[1];
        out[n].tx_bytes = (uint32_t)v[8];
        out[n].tx_pkts  = (uint32_t)v[9];
        n++;
    }
    fclose(f);
    return n;
}

int main(int argc, char **argv)
{
    dataserver_config_t cfg;
    int i, stat_port = 0;

    dataserver_default_config(&cfg);
    cfg.root = ".";
    for (i = 1; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], "-p") == 0)      cfg.port = atoi(argv[i + 1]);
        else if (strcmp(argv[i], "-s") == 0) cfg.thread_pool_size = atoi(argv[i + 1]);
        else if (strcmp(argv[i], "-q") == 0) cfg.queue_size = atoi(argv[i + 1]);
        else if (strcmp(argv[i], "-b") == 0) cfg.block_size = atoi(argv[i + 1]);
        else if (strcmp(argv[i], "-r") == 0) cfg.root = argv[i + 1];
        else if (strcmp(argv[i], "-S") == 0) stat_port = atoi(argv[i + 1]);
        else {
            fprintf(stderr, "usage: %s -p port -s pool -q queue -b block [-r root] [-S stats_port]\n", argv[0]);
            return 2;
        }
    }
    signal(SIGPIPE, SIG_IGN);
    if (dataserver_start(&cfg) != 0)
        return 1;
    if (stat_port > 0 && dsstat_start(stat_port, proc_snapshot, NULL) != 0)
        return 1;
    printf("READY %d\n", cfg.port);
    fflush(stdout);
    for (;;)
        pause();
    return 0;
}

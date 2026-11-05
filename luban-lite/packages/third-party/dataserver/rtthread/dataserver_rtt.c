/*
 * dataServer RT-Thread integration: Kconfig settings, autostart, msh command.
 *
 *   dataserver           status
 *   dataserver start     start with the Kconfig settings
 *   netmon               per-interface counters (netmon.c)
 * Also starts the dsstat JSON endpoint on DATASERVER_STAT_PORT.
 */
#ifdef DATASERVER_RTTHREAD

#include <rtthread.h>
#include <string.h>
#include "dataserver.h"
#include "dsstat.h"
#include "netmon.h"

#ifndef DATASERVER_PORT
#define DATASERVER_PORT          9000
#endif
#ifndef DATASERVER_POOL_SIZE
#define DATASERVER_POOL_SIZE     2
#endif
#ifndef DATASERVER_QUEUE_SIZE
#define DATASERVER_QUEUE_SIZE    16
#endif
#ifndef DATASERVER_BLOCK_SIZE
#define DATASERVER_BLOCK_SIZE    4096
#endif
#ifndef DATASERVER_ROOT
#define DATASERVER_ROOT          "/data/adas"
#endif
#ifndef DATASERVER_THREAD_STACK
#define DATASERVER_THREAD_STACK  8192
#endif
#ifndef DATASERVER_STAT_PORT
#define DATASERVER_STAT_PORT     9001
#endif

static int dataserver_start_default(void)
{
    dataserver_config_t cfg;

    dataserver_default_config(&cfg);
    cfg.port = DATASERVER_PORT;
    cfg.thread_pool_size = DATASERVER_POOL_SIZE;
    cfg.queue_size = DATASERVER_QUEUE_SIZE;
    cfg.block_size = DATASERVER_BLOCK_SIZE;
    cfg.root = DATASERVER_ROOT;
    cfg.thread_stack = DATASERVER_THREAD_STACK;
    if (dataserver_start(&cfg) != 0)
        return -1;
#if DATASERVER_STAT_PORT > 0
    netmon_hook_all();
    if (dsstat_start(DATASERVER_STAT_PORT, netmon_snapshot, netmon_ip_forwarded, netmon_lwip) != 0)
        rt_kprintf("dataserver: stats port %d not started\n", DATASERVER_STAT_PORT);
#endif
    return 0;
}

#ifdef DATASERVER_AUTOSTART
INIT_APP_EXPORT(dataserver_start_default);
#endif

static int dataserver(int argc, char **argv)
{
    dataserver_stats_t st;

    if (argc >= 2 && strcmp(argv[1], "start") == 0) {
        if (dataserver_running())
            rt_kprintf("dataserver: already running\n");
        else if (dataserver_start_default() != 0)
            rt_kprintf("dataserver: start failed\n");
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "status") != 0) {
        rt_kprintf("usage: dataserver [status|start]\n");
        return 0;
    }
    dataserver_get_stats(&st);
    rt_kprintf("dataserver: %s, port %d, root %s, %d workers, block %d\n",
               dataserver_running() ? "running" : "stopped", DATASERVER_PORT,
               DATASERVER_ROOT, DATASERVER_POOL_SIZE, DATASERVER_BLOCK_SIZE);
    rt_kprintf("  connections %u, directories sent %u, files sent %u, rejected %u\n",
               (unsigned)st.connections, (unsigned)st.requests_done,
               (unsigned)st.files_sent, (unsigned)st.rejected);
    rt_kprintf("  queued files %u, %u KB sent\n",
               (unsigned)st.queued, (unsigned)(st.bytes_sent / 1024));
    rt_kprintf("  stats endpoint: %s, port %d\n",
               dsstat_running() ? "running" : "off", DATASERVER_STAT_PORT);
    return 0;
}
MSH_CMD_EXPORT(dataserver, dataServer status/start (NikosMav protocol));

#endif /* DATASERVER_RTTHREAD */

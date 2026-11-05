/*
 * dataServer for RT-Thread: C port of NikosMav/System-Programming-3.
 * See dataserver.c for the design and wire protocol.
 */
#ifndef DATASERVER_H
#define DATASERVER_H

#include <stdint.h>
#include <stddef.h>

#define DATASERVER_ROOT_MAX   64
#define DATASERVER_MAX_DEPTH  8     /* directory recursion limit (MCU stack) */

typedef struct {
    int         port;               /* -p */
    int         thread_pool_size;   /* -s */
    int         queue_size;         /* -q */
    int         block_size;         /* -b */
    const char *root;               /* -r: clients only read below this dir */
    size_t      thread_stack;       /* bytes per thread */
} dataserver_config_t;

typedef struct {
    uint32_t connections;           /* accepted connections */
    uint32_t requests_done;         /* directories fully sent */
    uint32_t files_sent;
    uint32_t rejected;              /* bad path / no thread / too many clients */
    uint32_t queued;                /* files waiting in the queue right now */
    uint64_t bytes_sent;            /* file payload bytes */
} dataserver_stats_t;

void dataserver_default_config(dataserver_config_t *cfg);
int  dataserver_start(const dataserver_config_t *cfg);
int  dataserver_running(void);
void dataserver_get_stats(dataserver_stats_t *out);

#endif

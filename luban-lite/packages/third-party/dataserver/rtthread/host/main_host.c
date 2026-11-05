/*
 * Host runner for the C port, same command line as upstream dataServer:
 *   dataserver_c -p <port> -s <thread_pool_size> -q <queue_size> -b <block_size> [-r <root>]
 * Used to test the board code on a PC against the upstream remoteClient.
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "dataserver.h"

int main(int argc, char **argv)
{
    dataserver_config_t cfg;
    int i;

    dataserver_default_config(&cfg);
    cfg.root = ".";
    for (i = 1; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], "-p") == 0)      cfg.port = atoi(argv[i + 1]);
        else if (strcmp(argv[i], "-s") == 0) cfg.thread_pool_size = atoi(argv[i + 1]);
        else if (strcmp(argv[i], "-q") == 0) cfg.queue_size = atoi(argv[i + 1]);
        else if (strcmp(argv[i], "-b") == 0) cfg.block_size = atoi(argv[i + 1]);
        else if (strcmp(argv[i], "-r") == 0) cfg.root = argv[i + 1];
        else {
            fprintf(stderr, "usage: %s -p port -s pool -q queue -b block [-r root]\n", argv[0]);
            return 2;
        }
    }
    signal(SIGPIPE, SIG_IGN);
    if (dataserver_start(&cfg) != 0)
        return 1;
    printf("READY %d\n", cfg.port);
    fflush(stdout);
    for (;;)
        pause();
    return 0;
}

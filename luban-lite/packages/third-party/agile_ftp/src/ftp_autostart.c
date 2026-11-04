/* Luban-Lite glue: start agile_ftp at boot. Serves the whole DFS tree ("/data", "/rodata", ...). */
#include <rtthread.h>
#include "ftp.h"

#ifdef AGILE_FTP_AUTOSTART
static int agile_ftp_autostart(void)
{
    return ftp_init(AGILE_FTP_THREAD_STACK, AGILE_FTP_THREAD_PRIO, 10);
}
INIT_APP_EXPORT(agile_ftp_autostart);
#endif

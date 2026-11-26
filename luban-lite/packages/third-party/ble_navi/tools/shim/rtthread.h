/* host-test shim: maps the rt_* calls used by the SDK's cJSON.c to libc */
#ifndef HOST_SHIM_RTTHREAD_H
#define HOST_SHIM_RTTHREAD_H
#include <stdlib.h>
#include <string.h>
#define rt_malloc  malloc
#define rt_free    free
#define rt_realloc realloc
#define rt_memcpy  memcpy
#define rt_memset  memset
#endif

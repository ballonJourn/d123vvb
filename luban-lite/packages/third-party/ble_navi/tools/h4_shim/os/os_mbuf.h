#pragma once
#include <stdint.h>
struct os_mbuf { uint16_t len, cap; uint8_t *data; };
#define OS_MBUF_PKTLEN(om) ((om)->len)
int os_mbuf_append(struct os_mbuf *om, const void *d, uint16_t n);
int os_mbuf_free_chain(struct os_mbuf *om);

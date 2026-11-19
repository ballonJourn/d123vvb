#pragma once
#include <stdint.h>
struct os_mbuf;
struct os_mbuf *ble_transport_alloc_acl_from_ll(void);
struct os_mbuf *ble_transport_alloc_acl_from_hs(void);
struct os_mbuf *ble_transport_alloc_iso_from_hs(void);
void *ble_transport_alloc_evt(int discardable);
void *ble_transport_alloc_cmd(void);
void ble_transport_free(void *buf);
static inline uint16_t get_le16(const void *p) { const uint8_t *b = p; return b[0] | (b[1] << 8); }

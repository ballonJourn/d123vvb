/*
 * ble_service.h - D133 BLE Navi GATT server, backend neutral API
 *
 * Backends (Kconfig choice):
 *   BLE_NAVI_BACKEND_NIMBLE : RT-Thread NimBLE package (PKG_USING_NIMBLE),
 *                             host only, H4 HCI over RT-Thread UART -> AIC8800 BT
 *   BLE_NAVI_BACKEND_AIC    : vendor BT host shipped with the AIC8800 SDK
 *                             (template: ble_port_aic.c, fill after grep)
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef BLE_SERVICE_H
#define BLE_SERVICE_H

#include <stdint.h>
#include "ble_img_rx.h"
#include "chronos_nav.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ble_navi_link {           /* filled by backend, reported via GET_PARAMS */
    uint16_t conn_handle;        /* 0xFFFF when not connected */
    uint16_t att_mtu;
    uint16_t conn_interval, conn_latency, supervision_to;
    uint8_t  tx_phy, rx_phy;
    uint16_t max_tx_octets, max_rx_octets;
    uint8_t  backend;
};

struct ble_navi {
    struct bir          rx;      /* BNP reassembly             */
    struct chronos_nav  chronos; /* Chronos app compatibility  */
    struct ble_navi_link link;
    uint32_t            notify_fail;
};

extern struct ble_navi g_ble_navi;

/* ---- common glue (ble_service.c) ---- */
int  ble_navi_init(void);                 /* rx engine + chronos + display sink */
void ble_navi_on_connect(uint16_t conn);
void ble_navi_on_disconnect(void);
void ble_navi_on_mtu(uint16_t mtu);
void ble_navi_on_ctrl_write(const uint8_t *b, uint16_t n);
void ble_navi_on_data_write(const uint8_t *b, uint16_t n);
void ble_navi_on_chronos_write(const uint8_t *b, uint16_t n);
void ble_navi_on_chronos_subscribe(void);
void ble_navi_release(uint8_t layer);     /* called by disp_sink after blit */
/* BT is up once the AIC8800 driver has run aicbt_init; calls the vendor hook */
int  ble_navi_bt_power_on(void);
/*
 * Vendor BT bring-up, called after BT_PWR_ON and before the first HCI byte.
 * Weak default does nothing: on Luban-Lite v1.3.x the AIC8800 WiFi driver
 * calls aicbt_init() (fdrv/aicbt_bsp) during SDIO init when
 * CONFIG_AIC8800_BT_SUPPORT=y, which loads the BT patch and selects UART.
 */
int  ble_navi_bt_vendor_init(void);

/* ---- backend contract (ble_port_nimble.c / ble_port_aic.c) ---- */
int  ble_port_start(const char *dev_name);              /* stack up + advertise */
int  ble_port_notify_ctrl(const uint8_t *b, uint16_t n);
int  ble_port_notify_chronos(const uint8_t *b, uint16_t n);
int  ble_port_request_phy(uint8_t phy_mask);            /* 1=1M 2=2M 4=coded */
int  ble_port_request_conn(uint16_t min_ci, uint16_t max_ci, uint16_t lat, uint16_t to);
void ble_port_refresh_link(struct ble_navi_link *l);    /* read current params */

#ifdef __cplusplus
}
#endif
#endif

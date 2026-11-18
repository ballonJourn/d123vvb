/*
 * bnp_service.h - BLE Navi Protocol (BNP) GATT service on esp-nimble-cpp
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef BNP_SERVICE_H
#define BNP_SERVICE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* display finished with the frame of this layer: BNP may accept the next one */
void blenavi_release(uint8_t layer);
#ifdef __cplusplus
}

class NimBLEServer;
/* add the BNP service to an existing server (the one ChronosESP32::begin made) */
void bnp_service_attach(NimBLEServer *server);
void bnp_service_on_connect(NimBLEServer *server);
void bnp_service_on_disconnect();
void bnp_service_dump();
#endif
#endif

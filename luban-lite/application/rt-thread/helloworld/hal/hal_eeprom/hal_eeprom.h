#ifndef __HAL_EEPROM_H__
#define __HAL_EEPROM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>
#include "dev_config.h"

#if MILEAGE_PARAM_ENABLE

#define E2PROM_CAPACITY_BYTE  (1024)  ///< e2prom容量是1024字节
#define E2PROM_PAGE_SIZE      (16)   ///< e2prom页大小为16字节

#define T24C08A_DEV_ADDR    (0xA8)  ///< 设置地址

int e2prom_init(void);
int e2prom_test(uint16_t addr,int size,uint16_t start_data);
int e2prom_byte_read (uint16_t addr, uint8_t *buf, uint8_t length);
int e2prom_byte_write (uint16_t addr,uint8_t *buf, uint8_t length);
int e2prom_write_data(uint16_t addr, uint8_t *buf, int size);
int e2prom_read_data(uint16_t addr, uint8_t *buf, int size);

#endif

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // __HAL_EEPROM_H__
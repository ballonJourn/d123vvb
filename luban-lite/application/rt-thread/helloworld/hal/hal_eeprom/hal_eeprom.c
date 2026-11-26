#include <rthw.h>
#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>
#include <board.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "hal_eeprom.h"
#include "app_log.h"

#if MILEAGE_PARAM_ENABLE

#define E2PROM_DEV_NAME "i2c0"
#define DEBUG_E2PROM_ENABLE

/* I2C 重试: 与 NFC 共用 i2c0, 启动期 NFC NPD 状态稳定窗口可能持续几十 ms.
 * 历史值 (3 次 × 1ms) 在系统启动早期不够, 见 rt_i2c_transfer error 0 报错.
 * 8 × 5ms = 最坏 ~40ms 兜底, 对正常运行期没有副作用 (一次就过). */
#define E2PROM_RETRY_COUNT     8
#define E2PROM_RETRY_DELAY_MS  5

typedef struct rt_i2c_bus_device * e2prom_dev_t;

static e2prom_dev_t e2prom_i2c = NULL;
static rt_mutex_t lock;

#if  0

static uint8_t t24c08a_read_one_byte(uint16_t addr) {
    if (!e2prom_i2c) {
        rt_log_error("i2c0 dev not open!\n");
        return 0;
    }

    if (addr >= E2PROM_CAPACITY_BYTE) {
        rt_log_error("t24c08a byte read out of size!\n");    
        return 0;
    }

    struct rt_i2c_msg msgs[2];
    uint8_t data;
    uint8_t reg = (uint8_t)(addr & 0xff);

    msgs[0].addr  = T24C08A_DEV_ADDR | ((addr >> 8) & 0x03);
    msgs[0].flags = RT_I2C_WR;
    msgs[0].buf   = &reg;
    msgs[0].len   = 1;

    msgs[1].addr  = T24C08A_DEV_ADDR | ((addr >> 8) & 0x03);
    msgs[1].flags = RT_I2C_RD;
    msgs[1].buf   = &data;
    msgs[1].len   = 1;

    if (rt_i2c_transfer(e2prom_i2c, msgs, 2) != 2) {
        rt_log_error("read one byte failed!\n");
        return 0;
    }

    return data;
}

static int t24c08a_write_one_byte(uint16_t addr, uint8_t data) {
    if (addr >= E2PROM_CAPACITY_BYTE) {
        rt_log_error("t24c08a byte write out of size!\n");    
        return -1;
    }

    struct rt_i2c_msg msgs;
    uint8_t buf[2] = {0};

    buf[0] = (uint8_t)(addr & 0xff);
    buf[1] = data;

    msgs.addr = T24C08A_DEV_ADDR | ((addr >> 8) & 0x03);
    msgs.flags = RT_I2C_WR;
    msgs.buf = buf;
    msgs.len = 2;

    if (rt_i2c_transfer(e2prom_i2c, &msgs, 1) != 1) {
        rt_log_error("t24c08a write one byte failed!\n");    
        return -1;
    }

    return 0;
}

static int t24c08a_read_page(uint16_t addr, uint8_t *buffer, uint16_t num) {
    if (addr >= E2PROM_CAPACITY_BYTE || !buffer || 
        (addr + num) > E2PROM_CAPACITY_BYTE) {
        rt_log_error("t24c08a page read out of size!\n");    
        return -1;
    }

    struct rt_i2c_msg msgs[2];
    uint8_t reg = (uint8_t)(addr & 0xff);

    msgs[0].addr = T24C08A_DEV_ADDR | ((addr >> 8) & 0x03); 
    msgs[0].flags = RT_I2C_WR;
    msgs[0].buf = &reg;
    msgs[0].len = 1;

    msgs[1].addr = T24C08A_DEV_ADDR | ((addr >> 8) & 0x03);
    msgs[1].flags = RT_I2C_RD;
    msgs[1].buf = buffer;
    msgs[1].len = num;

    if (rt_i2c_transfer(e2prom_i2c, msgs, 2) != 2) {
        rt_log_error("t24c08a read page failed!\n");    
        return -1;
    }

    return 0;
}

static int t24c08a_write_page(uint16_t addr, uint8_t *buffer, uint16_t num) {
    if (!buffer || addr >= E2PROM_CAPACITY_BYTE ||
        (addr + num) > E2PROM_CAPACITY_BYTE || num > 16) {
        rt_log_error("t24c08a page write out of size!\n");    
        return -1;
    }

    struct rt_i2c_msg msgs;
    uint8_t write_buff[18] = {0};
    write_buff[0] = (uint8_t)(addr & 0xff);

    memcpy(&write_buff[1], buffer, num);

    msgs.addr = T24C08A_DEV_ADDR | ((addr >> 8) & 0x03);
    msgs.flags = RT_I2C_WR;
    msgs.buf = write_buff;
    msgs.len = num + 1;

    if (rt_i2c_transfer(e2prom_i2c, &msgs, 1) != 1) {
        rt_log_error("t24c08a write page failed!\n");    
        return -1;
    }

    return 0;
}

int t24c08a_check(void) {
    uint8_t temp = 0;

    temp = t24c08a_read_one_byte(E2PROM_CAPACITY_BYTE - 1);
    if (temp == 0x66) {
        rt_log_info("t24c08a read one byte success!\n");  
        return 0;  
    } else {
        t24c08a_write_one_byte(E2PROM_CAPACITY_BYTE - 1, 0x66);
        rt_thread_mdelay(5);                 // wait 5ms befor next operation
        temp = t24c08a_read_one_byte(E2PROM_CAPACITY_BYTE - 1);
        if (temp == 0x66) {
            rt_log_info("t24c08a read one byte success:0x%x!\n", temp);
            return 0;  
        }

        return -1;
    }
}

int t24c08a_read(uint16_t addr, uint8_t *buffer, uint16_t num) {
    if (!buffer || addr >= E2PROM_CAPACITY_BYTE ||
        (addr + num) > E2PROM_CAPACITY_BYTE) {
        rt_log_error("t24c08 read out of size!\n");    
        return -1;
    }

    int result = 0;
    result = rt_mutex_take(lock, RT_WAITING_FOREVER);
    if (result == 0) {
        while (num) {
            *buffer++ = t24c08a_read_one_byte(addr++);
            num--;
        }
    }  else {
        rt_log_error("t24c08a read timeout, try again!\n");
    }

    rt_mutex_release(lock);

    return 0;
}

int t24c08a_page_read(uint16_t addr, uint8_t *buffer, uint16_t num) {
    if (!buffer || addr >= E2PROM_CAPACITY_BYTE ||
        (addr + num) > E2PROM_CAPACITY_BYTE) {
        rt_log_error("t24c08 page read out of size!\n");    
        return -1;
    }

    uint16_t page_read_size = E2PROM_PAGE_SIZE - addr % E2PROM_PAGE_SIZE;
    int result = 0;
    result = rt_mutex_take(lock, RT_WAITING_FOREVER);
    if (result == 0) {
        while (num) {
            if (num > page_read_size) {
                if (t24c08a_read_page(addr, buffer, page_read_size) != 0) {
                    result = -1;
                }
                addr += page_read_size;
                buffer += page_read_size;
                num -= page_read_size;
                page_read_size = E2PROM_PAGE_SIZE;
            } else {
                if (t24c08a_read_page(addr, buffer, num) != 0) {
                    result = -1;
                }
                num = 0;
            }
        }
    } else {
        rt_log_error("t24c08a page read timeout, try again!\n");
    }

    rt_mutex_release(lock);

    return result;
}

int t24c08a_write(uint16_t addr, uint8_t *buffer, uint16_t num) {
    if (!buffer || addr >= E2PROM_CAPACITY_BYTE ||
        (addr + num) > E2PROM_CAPACITY_BYTE) {
        rt_log_error("t24c08 write out of size!\n");    
        return -1;
    }

    int result = 0;
    uint16_t i = 0;

    result = rt_mutex_take(lock, RT_WAITING_FOREVER);
    if (result == 0) {
        while (1) {
            if (t24c08a_write_one_byte(addr, buffer[i]) == 0) {
                rt_thread_mdelay(2);
                addr++;
            }

            if (++i == num) {
                break;
            }
            rt_thread_mdelay(5);
        }
    } else {
         rt_log_error("t24c08a write timeout, try again!\n");
    }

    rt_mutex_release(lock); 
    
    return 0;
}

int t24c08a_page_write(uint16_t addr, uint8_t *buffer, uint16_t num) {
    if (!buffer || addr >= E2PROM_CAPACITY_BYTE ||
        (addr + num) > E2PROM_CAPACITY_BYTE) {
        rt_log_error("t24c08 page write out of size!\n");    
        return -1;
    }

    uint16_t page_w_size = E2PROM_PAGE_SIZE - addr % E2PROM_PAGE_SIZE;
    int result = 0;
    result = rt_mutex_take(lock, RT_WAITING_FOREVER);
    if (result == 0) {
        while (num) {
            if (num > page_w_size) {
                if (t24c08a_write_page(addr, buffer, page_w_size) != 0) {
                    result = -1;
                }
                rt_thread_mdelay(5);    ///< wait 5ms befor next operation
                addr += page_w_size;
                buffer += page_w_size;
                num -= page_w_size;
                page_w_size = E2PROM_PAGE_SIZE;
            } else {
                if (t24c08a_write_page(addr, buffer, num) != 0) {
                    result = -1;
                }
                rt_thread_mdelay(5);   ///< wait 5ms befor next operation
                num = 0;
            }
        }
    } else {
        rt_log_error("t24c08a page write timeout, try again!\n");
    }

    rt_mutex_release(lock);

    return result;
}

#else 

int e2prom_byte_write (uint16_t addr,uint8_t *buf, uint8_t length) {
    uint8_t retires = 0;

    if ((length < 1) || (addr >= E2PROM_CAPACITY_BYTE) 
        || ((addr + length) > E2PROM_CAPACITY_BYTE)) {
        rt_log_error("e2prom byte write out of size!\n");    
        return -1;
    }

    struct rt_i2c_msg msg;
    uint8_t write_buff[2] = {0};

    int ret = 0;
    for (int i = 0; i < length; i++) {
        memset(write_buff, 0, 2);
        write_buff[0] = (uint8_t)((addr + i) & 0xff);
        memcpy(&write_buff[1], &buf[i], 1);

        msg.addr = T24C08A_DEV_ADDR | (((addr + i) >> 8) & 0x03);
        msg.flags = RT_I2C_WR; 
        msg.buf = write_buff;
        msg.len = 2;
        retires = 0;
        while (retires < 3) {
            ret = rt_i2c_transfer(e2prom_i2c, &msg, 1);
            if (ret == 1) {
               ret = 0;
               break;
            } else {
                retires++;
                rt_log_error("rt_i2c_transfer error %d at address 0x%04X.\n", ret, addr + i);
                ret = -1;
            }
        }

	    rt_thread_mdelay(5); ///< 延迟5毫秒，等待EEPROM写入完成
    }
 
    return ret;
}

int e2prom_byte_read (uint16_t addr, uint8_t *buf, uint8_t length) {
    uint8_t retires;
    if ((length < 1) || (addr >= E2PROM_CAPACITY_BYTE) 
        || ((addr + length) > E2PROM_CAPACITY_BYTE)) {
        rt_log_error("e2prom byte read out of size!\n");    
        return -1;
    }

    struct rt_i2c_msg msg[2];
    uint8_t reg;

    int ret = 0;
    for (int i = 0; i < length; i++) {
        reg = (uint8_t)((addr + i) & 0xff);

        msg[0].addr = T24C08A_DEV_ADDR | (((addr + i) >> 8) & 0x03);
        msg[0].flags = RT_I2C_WR; 
        msg[0].buf = &reg;
        msg[0].len = 1;

        msg[1].addr = T24C08A_DEV_ADDR | (((addr + i) >> 8) & 0x03);
        msg[1].flags = RT_I2C_RD; 
        msg[1].buf = &buf[i];
        msg[1].len = 1;
        retires = 0;
        while (retires < 3) {
            ret = rt_i2c_transfer(e2prom_i2c, msg, 2);
            if (ret == 2) {
                ret = 0;
                break;
            } else {
                rt_log_error("rt_i2c_transfer error %d at address 0x%04X.\n", ret, addr + i);
                retires++;
                ret = -1;
            }
        }
    }

    return ret;
}

int e2prom_write_data(uint16_t addr, uint8_t *buf, int size) {
    if ((e2prom_i2c == NULL) || (size < 1) 
        || (addr >= E2PROM_CAPACITY_BYTE) 
        || ((addr + size) > E2PROM_CAPACITY_BYTE)) {
        rt_log_error("e2prom byte write out of size!\n");    
        return -1;
    }

    uint16_t write_data = 0;
    uint8_t retries;
    uint16_t page_start;
    uint16_t page_size;
    uint16_t remain_data;
    uint16_t write_len;

    while (write_data < size) {
        ///< 计算当前页起始地址（页大小 16 字节）
        page_start = (addr + write_data) & ~(E2PROM_PAGE_SIZE - 1);
        page_size = E2PROM_PAGE_SIZE - ((addr + write_data) - page_start);
        remain_data = size - write_data;
        write_len = (remain_data < page_size) ? remain_data : page_size;

        struct rt_i2c_msg msgs[2];
        uint8_t reg = (uint8_t)((addr + write_data) & 0xff);   

        msgs[0].addr  = T24C08A_DEV_ADDR | (((addr + write_data) >> 8) & 0x03);
        msgs[0].flags = RT_I2C_WR;
        msgs[0].buf   = &reg;
        msgs[0].len   = 1;

        msgs[1].addr  = T24C08A_DEV_ADDR | (((addr + write_data) >> 8) & 0x03);
        msgs[1].flags = RT_I2C_WR;
        msgs[1].buf   = (uint8_t*)(buf + write_data);
        msgs[1].len   = write_len;

        retries = 0;
        int ret = 0;
        while (retries < E2PROM_RETRY_COUNT) {
            ret = rt_i2c_transfer(e2prom_i2c, msgs, 2);
            if (ret == 2) {
                break;
            } else {
                retries++;
                rt_log_error("Error: I2C transfer error %d, retry %d\n", ret, retries);
                rt_thread_mdelay(E2PROM_RETRY_DELAY_MS);
            }
        }
        if (retries >= E2PROM_RETRY_COUNT) {
            rt_log_error("Error: failed to write at addr 0x%04X\n", addr + write_data);
            return -1;
        }

        rt_thread_mdelay(5);

        write_data += write_len;
    }

    return size;
}

int e2prom_read_data(uint16_t addr, uint8_t *buf, int size) {
    if((size < 1) || addr >= E2PROM_CAPACITY_BYTE 
        || (addr + size) > E2PROM_CAPACITY_BYTE) {
        rt_log_error("e2prom read data out of size!\n");    
        return -1;
    }

    struct rt_i2c_msg msgs[2];
    uint8_t retries = 0;
    int ret = 0;
    uint8_t reg = (uint8_t)(addr & 0xff);

    msgs[0].addr = T24C08A_DEV_ADDR | ((addr >> 8) & 0x03); 
    msgs[0].flags = RT_I2C_WR;
    msgs[0].buf = &reg;
    msgs[0].len = 1;

    msgs[1].addr = T24C08A_DEV_ADDR | ((addr >> 8) & 0x03);
    msgs[1].flags = RT_I2C_RD;
    msgs[1].buf = buf;
    msgs[1].len = size;

    while (retries < E2PROM_RETRY_COUNT) {
        ret = rt_i2c_transfer(e2prom_i2c, msgs, 2);
        if (ret == 2) {
            break;
        } else {
            retries++;
            rt_log_error("rt_i2c_transfer error %d.\n", ret);
            rt_thread_mdelay(E2PROM_RETRY_DELAY_MS);
        }
    }

    if (retries < E2PROM_RETRY_COUNT) {
        return size;
    }

    return -1;
}

static int __attribute__((unused)) e2prom_test(uint16_t addr, int size,uint8_t start_data) {
    uint8_t* w_buf = (uint8_t*)rt_malloc(size);
    uint8_t* r_buf = (uint8_t*)rt_malloc(size);

    int rtn = 0;

    rt_log_info("%s(0x%08X, %d)\r\n",__FUNCTION__, addr, size);
    if (w_buf == NULL || r_buf == NULL) {
        rt_log_error("%s pvPortMalloc fail!\r\n",__FUNCTION__);
        rtn = -1;
        goto _error1;
    }

    memset(w_buf, 0, size);
    memset(r_buf, 0, size);

    for (uint16_t i = 0; i < size; i++) {
        w_buf[i] = start_data + i;
    }

    rtn = e2prom_write_data(addr, w_buf, size);
    if (rtn <0) {
        rt_log_error("err: eeprom_write return %d\r\n",rtn);
        rtn = -2;
        goto _error1;
    } else {
        rt_log_info("eeprom_write %d bytes\r\n",rtn);
    }

    rtn = e2prom_read_data(addr, r_buf, size);
    if (rtn < 0) {
        rt_log_error("err: eeprom_read return %d\r\n",rtn);
        rtn = -3;
        goto _error1;
    } else {
        rt_log_info("eeprom_read %d bytes\r\n",rtn);
    }

#ifdef DEBUG_E2PROM_ENABLE
        rt_log_info("w_buf:");
        for (uint16_t i = 0; i < size; i++) {
            if (i % 16 == 0) {
                rt_log_info("\r\n0x%04X: ",i);
            }
            rt_log_info("%02X ",w_buf[i]);
        }
        rt_log_info("\r\n");

        rt_log_info("r_buf:");
        for (uint16_t i = 0; i < size; i++) {
            if (i % 16 == 0) {
                rt_log_info("\r\n0x%04X: ",i);
            }
            rt_log_info("%02X ",r_buf[i]);
        }

        rt_log_info("\r\n");
#endif

    if (memcmp(w_buf, r_buf, size) == 0) {
        rt_log_info("w_buf == r_buf, eeprom test ok!\r\n");
    } else {
        rt_log_info("w_buf != r_buf, eeprom test fail!\r\n");
    }

    return 0;
_error1:
    if (w_buf)
        rt_free(w_buf);

    if (r_buf)
        rt_free(r_buf);
    
    return rtn;
}

#endif

void clear_e2mprom(void) {
    uint8_t writre_buff[E2PROM_CAPACITY_BYTE] = {0};
    int write_num = 0;

    memset(writre_buff, 0xff, E2PROM_CAPACITY_BYTE);
    write_num = e2prom_write_data(0x00, writre_buff, E2PROM_CAPACITY_BYTE);
    if (write_num != E2PROM_CAPACITY_BYTE) {
        rt_log_info("clear e2prom failed..\n");
        return;
    }

    rt_log_info("clear e2prom success, os will reboot...\n");
    rt_thread_mdelay(1000); 
    system("reboot");
}

int e2prom_init(void) {
    if (e2prom_i2c) { 
        rt_log_info("e2prom_i2c dev already open!\n");
        return 0;
    }

    e2prom_i2c = rt_i2c_bus_device_find(E2PROM_DEV_NAME);
    if (e2prom_i2c == NULL) {
        rt_log_error("Can't not find device name:%s\n", E2PROM_DEV_NAME);
        return -1;
    }

    lock = rt_mutex_create("mutex_e2prom", RT_IPC_FLAG_FIFO);
    if (lock == NULL) {
        rt_log_error("Can't create mutex for e2prom on:%s\n", E2PROM_DEV_NAME);
        return -1;
    }

    return 0;
}

#endif
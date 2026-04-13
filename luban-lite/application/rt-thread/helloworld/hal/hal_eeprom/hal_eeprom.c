#include <rthw.h>
#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>
#include <board.h>
#include "hal_eeprom.h"
#include "app_log.h"

#if MILEAGE_PARAM_ENABLE

#define E2PROM_DEV_NAME "i2c0"
#define DEBUG_E2PROM_ENABLE

typedef struct rt_i2c_bus_device * e2prom_dev_t;

static e2prom_dev_t e2prom_i2c = NULL;
static rt_mutex_t lock;

#if  0
static int read_regs(e2prom_dev_t dev, uint8_t len, uint8_t *buf) {
    struct rt_i2c_msg  msgs;

    msgs.addr = T24C08A_DEV_ADDR;
    msgs.flags = RT_I2C_RD;
    msgs.buf = buf;
    msgs.len = len;

    if (rt_i2c_transfer(dev, &msgs, 1) == 1) {
        return 0;
    }

    return -1;
}

uint8_t t24c08a_read_one_byte(uint16_t addr) {
    if (!e2prom_i2c) {
        rt_log_error("i2c0 dev not open!\n");
        return 0;
    }

    uint8_t buf[2];
    uint8_t temp = 0;

    if (addr >= E2PROM_CAPACITY_BYTE) {
        rt_log_error("t24c08a byte read out of size!\n");    
        return 0;
    }

    buf[0] = (uint8_t)(addr >> 8);
    buf[1] = (uint8_t)(addr & 0xff);

    if (rt_i2c_master_send(e2prom_i2c, T24C08A_DEV_ADDR, 0, buf, 2) == 0) {
        return 0;
    }

    if (read_regs(e2prom_i2c, 1, &temp) != 0) {
        rt_log_error("t24c08a read regs failed!\n");    
        return 0;
    }

    return temp;
}

int t24c08a_write_one_byte(uint16_t addr, uint8_t data) {
    if (addr >= E2PROM_CAPACITY_BYTE) {
        rt_log_error("t24c08a byte write out of size!\n");    
        return -1;
    }

    uint8_t buf[3] = {0};
    buf[0] = (uint8_t)(addr >> 8);
    buf[1] = (uint8_t)(addr & 0xff);
    buf[2] = data;
    
    if (rt_i2c_master_send(e2prom_i2c, T24C08A_DEV_ADDR, 0, buf, 3) == 3) {
        return 0;
    }

    return -1;
}

int t24c08a_read_page(uint16_t addr, uint8_t *buffer, uint16_t num) {
    if (addr >= E2PROM_CAPACITY_BYTE || !buffer || 
        (addr + num) > E2PROM_CAPACITY_BYTE) {
        rt_log_error("t24c08a page read out of size!\n");    
        return -1;
    }

    struct rt_i2c_msg msgs[2];
    uint8_t addr_buf[2];

    msgs[0].addr = T24C08A_DEV_ADDR;
    msgs[0].flags = RT_I2C_WR;
    addr_buf[0] = (uint8_t)(addr >> 8);
    addr_buf[1] = (uint8_t)(addr & 0xff);
    msgs[0].buf = addr_buf;
    msgs[0].len = 2;

    msgs[1].addr = T24C08A_DEV_ADDR;
    msgs[1].flags = RT_I2C_RD;
    msgs[1].buf = buffer;
    msgs[1].len = num;

    if (rt_i2c_transfer(e2prom_i2c, msgs, 2) == 2) {
        return 0;
    }

    return -1;
}

int t24c08a_write_page(uint16_t addr, uint8_t *buffer, uint16_t num) {
    if (!buffer || addr >= E2PROM_CAPACITY_BYTE ||
        (addr + num) > E2PROM_CAPACITY_BYTE) {
        rt_log_error("t24c08a page write out of size!\n");    
        return -1;
    }

    struct rt_i2c_msg msgs;
    uint8_t write_buff[E2PROM_CAPACITY_BYTE + 2] = {0};

    write_buff[0] = (uint8_t)(addr >> 8);
    write_buff[1] = (uint8_t)(addr & 0xff);

    memcpy(&write_buf[2], buffer, num);

    msgs.addr = T24C08A_DEV_ADDR;
    msgs.flags = RT_I2C_WR;
    msgs.buf = buffer;
    msgs.len = num + 2;

    if (rt_i2c_transfer(e2prom_i2c, &msgs, 1) == 1) {
        return 0;
    }

    return -1;
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
            *buffer++ = t24c08a_write_one_byte(addr++);
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
    uint8_t write_buff[3] = {0};

    int ret = 0;
    for (int i = 0; i < length; i++) {
        memset(write_buff, 0, 3);
        write_buff[0] = (uint8_t)((addr + i) >> 8);
        write_buff[1] = (uint8_t)((addr + i) & 0xFF);
        memcpy(&write_buff[2], &buf[i], 1);

        msg.addr = T24C08A_DEV_ADDR;
        msg.flags = RT_I2C_WR; ///< 写操作
        msg.buf = write_buff;
        msg.len = 3;
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
    uint8_t read_buff[2] = {0};

    int ret = 0;
    for (int i = 0; i < length; i++) {
        read_buff[0] = (uint8_t)((addr + i) >> 8);
        read_buff[1] = (uint8_t)((addr + i) & 0xFF);

        msg[0].addr = T24C08A_DEV_ADDR;
        msg[0].flags = RT_I2C_WR; ///< 写操作
        msg[0].buf = read_buff;
        msg[0].len = 2;

        msg[1].addr = T24C08A_DEV_ADDR;
        msg[1].flags = RT_I2C_RD; ///< 读操作
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
    struct rt_i2c_msg msg;
    uint16_t page_cnt = 0;
    uint16_t msg_buff_size = 0;
    uint8_t page_first_size = 0;
    uint8_t page_last_size = 0;
    uint8_t retries = 0;
    uint8_t msg_addr = 0;

    int ret = 0;

    if ((size < 1) || (addr >= E2PROM_CAPACITY_BYTE) 
        || ((addr + size) > E2PROM_CAPACITY_BYTE)) {
        rt_log_error("e2prom write data out of size!\n");    
        return -1;
    }

    ///< 地址是按照页划分，16个字节为一页
    if (addr % E2PROM_PAGE_SIZE) {
        rt_log_error("e2prom write data add error!\n");    
        return -2;
    } else {
        page_cnt = (size - 1) / E2PROM_PAGE_SIZE + 1;
        if (size < E2PROM_PAGE_SIZE) {
            page_first_size = size;
        } else {
            page_first_size = E2PROM_PAGE_SIZE;
        }

        page_last_size = size & 0xf;
        if (page_last_size == 0) {
            page_last_size = 16;
        }
    }

    uint8_t *write_buff = (uint8_t *)rt_malloc(18);
    if (!write_buff) {
        rt_log_error("e2prom write data rt_malloc fail!\n");    
        return -3;
    }

    msg_addr = addr;
    uint8_t *src = buf;

    for (uint16_t i = 0; i < page_cnt; i++) {
        memset(write_buff, 0, 18);
        write_buff[0] = (uint8_t)(msg_addr >> 8);
        write_buff[1] = (uint8_t)(msg_addr & 0xff);
        if (i == 0) {
            msg_buff_size = page_first_size;
        } else if (i == page_cnt - 1) {
            msg_buff_size = page_last_size;
        } else {
            msg_buff_size = E2PROM_PAGE_SIZE;
        }

        memcpy(write_buff + 2, src, msg_buff_size);
        msg.addr = T24C08A_DEV_ADDR;
        msg.flags = RT_I2C_WR;
        msg.buf = write_buff;
        msg.len = msg_buff_size + 2;

        retries = 0;
        while (retries < 3) {
            ret = rt_i2c_transfer(e2prom_i2c, &msg, 1);
            if (ret == 1) {
                break;
            } else {
                retries++;
                rt_log_error("rt_i2c_transfer error:%d\n", ret);
            }
        }

        if (retries == 0) {
            msg_addr += msg_buff_size;
            src += msg_buff_size;
            rt_thread_mdelay(5);
        } else {
            break;
        }
    }

    rt_free(write_buff);

    return (msg_addr - addr);
}

int e2prom_read_data(uint16_t addr, uint8_t *buf, int size) {
    struct rt_i2c_msg msg[2];
    int ret;
    uint8_t retries = 0;
    uint16_t msg_cnt = 0;
    uint16_t msg_buf_size;
    uint16_t msg_addr;
    uint8_t page_off;
    uint8_t page_last_size;
    uint8_t page_first_size;

    if((size < 1) || addr >= E2PROM_CAPACITY_BYTE 
        || (addr + size) > E2PROM_CAPACITY_BYTE) {
        rt_log_error("e2prom read data out of size!\n");    
        return -1;
    }

    page_off = addr % 16;
    if(page_off) {
        rt_log_error("e2prom read data add error!\n");    
        return -2;
    } else {
        msg_cnt = (size - 1) / 16 + 1;
        if (size < 16) {
            page_first_size = size;
        } else {
            page_first_size = 16;
        }

        page_last_size = size & 0xf;
        if(page_last_size == 0) {
            page_last_size = 16;
        }
    }

    msg_addr = addr;
    uint8_t *dst = buf;
    uint8_t read_buff[2] = {0};

    for (uint16_t i = 0; i < msg_cnt; i++) {
        if (i == 0) {
            msg_buf_size = page_first_size;
        } else if(i == msg_cnt-1) {
            msg_buf_size = page_last_size;
        } else {
            msg_buf_size = 16;
        }

        read_buff[0] = (uint8_t)(msg_addr >> 8);
        read_buff[1] = (uint8_t)(msg_addr & 0xff);

        msg[0].addr = T24C08A_DEV_ADDR;
        msg[0].flags = RT_I2C_WR;
        msg[0].buf = read_buff;
        msg[0].len = 2;

        msg[1].addr = T24C08A_DEV_ADDR;
        msg[1].flags = RT_I2C_RD;
        msg[1].buf = dst;
        msg[1].len = msg_buf_size;

        retries = 0;
        while (retries < 3) {
            ret = rt_i2c_transfer(e2prom_i2c, msg, 2);
            if (ret == 2) {
                break;
            } else {
                retries++;
                rt_log_error("rt_i2c_transfer error %d.\n", ret);
            }
        }

        if (retries == 0) {
            msg_addr += msg_buf_size;
            dst += msg_buf_size;
        } else {
            break;
        }
    }

    return (msg_addr - addr);
}

int e2prom_test(uint16_t addr, int size, uint16_t start_data) {
    uint8_t* w_buf = (uint8_t*)rt_malloc(size);
    uint8_t* r_buf = (uint8_t*)rt_malloc(size);

    int rtn = 0;
    uint16_t *p = (uint16_t *)w_buf;

    rt_log_info("%s(0x%08X, %d)\r\n",__FUNCTION__, addr, size);
    if (w_buf == NULL || r_buf==NULL) {
        rt_log_error("%s pvPortMalloc fail!\r\n",__FUNCTION__);
        rtn = -1;
        goto _error1;
    }

    memset(w_buf, 0, size);
    memset(r_buf, 0, size);

    for (uint16_t i = 0; i < size / 2; i++) {
        p[i] = start_data + i;
    }

    rtn = e2prom_write_data(addr, w_buf, size);
    if (rtn <0) {
        rt_log_error("err: eeprom_write return %d\r\n",rtn);
        rtn = -2;
        goto _error1;
    } else {
        rt_log_error("eeprom_write %d bytes\r\n",rtn);
    }

    rtn = e2prom_read_data(addr, r_buf, size);
    if (rtn < 0) {
        rt_log_error("err: eeprom_read return %d\r\n",rtn);
        rtn = -3;
        goto _error1;
    } else {
        rt_log_error("eeprom_read %d bytes\r\n",rtn);
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

int e2prom_init(void) {
    if (e2prom_i2c) { 
        return 0;
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
#endif
#include <rtthread.h>
#include "rtdevice.h"
#include "mw_can.h"
#include "usr_utils.h"
#include "mw_can_tx.h"
#include "rtconfig.h"
#include "vehicle_param.h"
#include "dev_config.h"
#include "app_log.h"
#include "dev_state.h"

#if MW_CAN_ENABLE

#define GET_DATA_BITS(data,offset,mask) ((data >> offset) & mask)  
#define CAN_RX_THREAD_PRIO  (14)
#define MW_CAN_DEVICE_NAME    "can0"

static can_rx_timeout rx_timeout = {0};

static rt_device_t can_dev;
static struct rt_semaphore can_rx_sem;

static rt_err_t can_rx_callback(rt_device_t dev, rt_size_t size) {
    rt_sem_release(&can_rx_sem);

    return RT_EOK;
}

static void timeout_clear_can_data(void) {
    uint32_t current_tick = rt_tick_get();

    if ((current_tick - rx_timeout.ecu_110_rx_timeout) > rt_tick_from_millisecond(1000)) {
        if (vehicle_get_data(VEH_SPEED_ENGINE) > 0) {
            vehicle_set_data(VEH_SPEED_ENGINE, 0);
        }
    }

    if ((current_tick - rx_timeout.ecu_111_rx_timeout) > rt_tick_from_millisecond(1000)) {
        if (vehicle_get_data(VEH_DRIVE_MODE) > 0) {
            vehicle_set_data(VEH_DRIVE_MODE, 0);
        }

        if (vehicle_get_data(VEH_LIGHT_ENGINE_FAULT) > 0) {
            vehicle_set_data(VEH_LIGHT_ENGINE_FAULT, 0);
        }
    }

    if ((current_tick - rx_timeout.ecu_112_rx_timeout) > rt_tick_from_millisecond(1000)) {
        if (vehicle_get_data(VEH_LIGHT_BRAKE) > 0) {
            vehicle_set_data(VEH_LIGHT_BRAKE, 0);
        }

        if (vehicle_get_data(VEH_LIGHT_READY) > 0) {
            vehicle_set_data(VEH_LIGHT_READY, 0);
        }

        if (vehicle_get_data(VEH_LIGHT_GPS) > 0) {
            vehicle_set_data(VEH_LIGHT_GPS, 0);
        }

        if (vehicle_get_data(VEH_TRAM_POWR) > 0) {
            vehicle_set_data(VEH_TRAM_POWR, 0);
        }

        if (vehicle_get_data(VEH_TRAM_REMAIN_BATTARY) > 0) {
            vehicle_set_data(VEH_TRAM_REMAIN_BATTARY, 0);
        }

        if (vehicle_get_data(VEH_GEAR_POSITION) > 0) {
            vehicle_set_data(VEH_GEAR_POSITION, 0);
        }
    }

    if ((current_tick - rx_timeout.ecu_120_rx_timeout) > rt_tick_from_millisecond(1000)) {
        if (vehicle_get_data(VEH_TCS_WARNING) > 0) {
            vehicle_set_data(VEH_TCS_WARNING, 0);
        }
    }

    if ((current_tick - rx_timeout.ecu_122_rx_timeout) > rt_tick_from_millisecond(1000)) {
       if (vehicle_get_data(VEH_SPEED_CURRENT) > 0) {
            vehicle_set_data(VEH_SPEED_CURRENT, 0);
        }
    }

    if ((current_tick - rx_timeout.ecu_12b_rx_timeout) > rt_tick_from_millisecond(1000)) {
        if (vehicle_get_data(VEH_LIGHT_ABS) == 0) {
            vehicle_set_data(VEH_LIGHT_ABS, 1);
        }
    }
}

#define CRC8_MAGIC_NUM (0x1D)

static uint8_t can_j1850_crc8(const uint8_t *data, uint8_t length) {
    uint8_t crc = 0xFF; 
    for (uint8_t i = 0; i < length; i++) {
        crc ^= data[i]; 
        for (uint8_t j = 0; j < 8; j++) 
	    {
	        if (crc & 0x80) { 
	            crc = (crc << 1) ^ CRC8_MAGIC_NUM; 
	        } else {
	            crc <<= 1; 
	        }
	    }
        crc &= 0xFF; 
    }
	crc ^= 0xFF;
    return crc; 
}

static int parse_can_msg_110_msg(uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_START) {
        return 0;
    }

    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_110 dlc[%d] or msg buff err!\r\n",size);
        return -1;
    }

    rx_timeout.ecu_110_rx_timeout = rt_tick_get();;

    int data = ((buf[2] << 8) + (buf[3]));
    if (data == 0xffff) {
        data = 0;
    }

    float engine_speed_tmp = data * 0.25;
    data = (int)engine_speed_tmp;
    if (data > 0 && data <= 1700) {
        data = 1700;
    }

    vehicle_set_data(VEH_SPEED_ENGINE, data);

    return 0;
}

static int parse_can_msg_111_msg(uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_START) {
        return 0;
    }

    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_111 dlc[%d] or msg buff err!\r\n",size);
        return -1;
    }

    rx_timeout.ecu_111_rx_timeout = rt_tick_get();

    int data = GET_DATA_BITS(buf[0], 1, 0x01);
    vehicle_set_data(VEH_LIGHT_ENGINE_FAULT, data);

    data = GET_DATA_BITS(buf[0], 2, 0x07);
    vehicle_set_data(VEH_DRIVE_MODE, data);

    return 0;
}

static int parse_can_msg_112_msg(uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_START) {
        return 0;
    }

    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_112 dlc[%d] or msg buff err!\r\n",size);
        return -1;
    }

    rx_timeout.ecu_112_rx_timeout = rt_tick_get();

    int data = GET_DATA_BITS(buf[0], 0, 0x01);
    vehicle_set_data(VEH_LIGHT_BRAKE, data);

    data = GET_DATA_BITS(buf[0], 1, 0x1);
    vehicle_set_data(VEH_LIGHT_READY, data);

    data = GET_DATA_BITS(buf[0], 2, 0x1);
    vehicle_set_data(VEH_LIGHT_GPS, data);

    data = GET_DATA_BITS(buf[2], 0, 0x07);
    vehicle_set_data(VEH_GEAR_POSITION, data);

    float power = 0;
    data = ((buf[5] << 8) + buf[6]);
    if (data == 0xFFFF) {
        data = 0;
    }

    power = data * 0.01;
    data = (int)power;
    if (data > 100) {
        data = 100;
    }
    vehicle_set_data(VEH_TRAM_POWR, data);

    data = buf[7];
    if (data == 0xFF) {
        data = 0;
    }
    vehicle_set_data(VEH_TRAM_REMAIN_BATTARY, data);

    return 0;
}

static int parse_can_msg_120_msg(uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_START) {
        return 0;
    }

    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_120 dlc[%d] or msg buff err!\r\n",size);
        return -1;
    }

    rx_timeout.ecu_120_rx_timeout = rt_tick_get();

    int data = GET_DATA_BITS(buf[3], 2, 0x01);
    vehicle_set_data(VEH_TCS_WARNING, data);

    return 0;
}

static int parse_can_msg_122_msg(uint8_t *buf, uint8_t size) {
 if (get_check_self_state() < CHECK_SELF_STATE_START) {
        return 0;
    }

    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_122 dlc[%d] or msg buff err!\r\n",size);
        return -1;
    }

    rx_timeout.ecu_122_rx_timeout = rt_tick_get();
    int data = 0;
    float speed_tmp = 0;

    data = ((buf[2] << 8) + buf[3]);
    if (data == 0xFFFF) {
        data = 0;
    }

    speed_tmp = data * 0.01;
    data = (int)speed_tmp;
    if (data > 199) {
        data = 199;
    }

    vehicle_set_data(VEH_SPEED_CURRENT, data);

    return 0;
}

static int parse_can_msg_12b_msg(uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_START) {
        return 0;
    }
    
    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_12b dlc[%d] or msg buff err!\r\n",size);
        return -1;
    }

    rx_timeout.ecu_12b_rx_timeout = rt_tick_get();

    int data = GET_DATA_BITS(buf[5], 0, 0x01);
    vehicle_set_data(VEH_LIGHT_ABS, data);

    return 0;
}

static int parse_can_msg_776_msg(uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_SUCCESS) {
        return 0;
    }
    
    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_776 dlc[%d] or msg buff err!\r\n",size);
        return -1;
    }

    uint8_t crc_chceck;
    static uint8_t rx_cnt = 0;
	static uint8_t rx_buf[8] = {0};
#if 0
     hex_config_data_print("can", "recv(0x):", buf, 8);   
#endif

    crc_chceck = can_j1850_crc8(buf, 7);
    if (crc_chceck != buf[7]) {
        rt_log_error("can_ecu_776 Error checksum = 0x%x, buf[7] = 0x%x !\r\n", \
            crc_chceck, buf[7]);
        return -1;
    }

    if (rx_cnt == 0) {
		memcpy(rx_buf, buf, 8);
		rx_cnt++;
	} else if (rx_cnt == 1) {
		if (memcmp(rx_buf, buf, 8) == 0) {
			rx_cnt++;
		} else{
			memcpy(rx_buf, buf, 8);
			rx_cnt = 1;
		}
	} else if(rx_cnt == 2) {
		if (memcmp(rx_buf, buf, 8) == 0) {
			rx_cnt++;
		} else {
			memcpy(rx_buf, buf, 8);
			rx_cnt = 1;
		}
	}

	if (rx_cnt == 3) {
		rx_cnt = 0;
		uint32_t trip_a;
        uint32_t tmp = 0;
#if 0
        if (!get_hcn_mile_param(HCN_MILE_PARAM_TRIP_A, &tmp)) {
            hcn_log_error("Get HCN_MILE_PARAM_TRIP_A error!\r\n");
            return -1;
        }
#endif
        trip_a = (((uint32_t) buf[0]) << 24 ) |  (((uint32_t) buf[1]) << 16 ) | \
                (((uint32_t) buf[2]) << 8 ) |(((uint32_t) buf[3]) << 0);
		if (trip_a != tmp) {
            //set_hcn_mile_param(HCN_MILE_PARAM_TRIP_A, &trip_a);
            vehicle_set_data(VEH_MILEAGE_CHANGE_MSG, 1);
		}
	}

	return 0;
}

static int parse_can_msg_777_msg(uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_SUCCESS) {
        return 0;
    }
    
    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_777 dlc[%d] or msg buff err!\r\n",size);
        return -1;
    }

    uint8_t crc_chceck;
    static uint8_t rx_cnt = 0;
	static uint8_t rx_buf[8] = {0};
#if 0
     hex_config_data_print("can", "recv(0x):", buf, 8);   
#endif

    crc_chceck = can_j1850_crc8(buf, 7);
    if (crc_chceck != buf[7]) {
        rt_log_error("can_ecu_777 Error checksum = 0x%x, buf[7] = 0x%x !\r\n", \
                    crc_chceck, buf[7]);
        return -1;
    }

    if (rx_cnt == 0) {
		memcpy(rx_buf, buf, 8);
		rx_cnt++;
	} else if (rx_cnt == 1) {
		if (memcmp(rx_buf, buf, 8) == 0) {
			rx_cnt++;
		} else{
			memcpy(rx_buf, buf, 8);
			rx_cnt = 1;
		}
	} else if(rx_cnt == 2) {
		if (memcmp(rx_buf, buf, 8) == 0) {
			rx_cnt++;
		} else {
			memcpy(rx_buf, buf, 8);
			rx_cnt = 1;
		}
	}

	if (rx_cnt == 3) {
		rx_cnt = 0;
		uint32_t trip_b;
        uint32_t tmp = 0;
#if 0
        if (!get_hcn_mile_param(HCN_MILE_PARAM_TRIP_B, &tmp)) {
            hcn_log_error("Get HCN_MILE_PARAM_TRIP_B error!\r\n");
            return -1;
        }
#endif
        trip_b = (((uint32_t) buf[0]) << 24 ) |  (((uint32_t) buf[1]) << 16 ) | \
                (((uint32_t) buf[2]) << 8 ) |(((uint32_t) buf[3]) << 0);
		if (trip_b != tmp) {
            //set_hcn_mile_param(HCN_MILE_PARAM_TRIP_B, &trip_b);
            vehicle_set_data(VEH_MILEAGE_CHANGE_MSG, 2);
		}
	}

	return 0;
}

static int parse_can_msg_778_msg(uint8_t *buf, uint8_t size) {
   if (get_check_self_state() < CHECK_SELF_STATE_SUCCESS) {
        return 0;
    }
    
    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_778 dlc[%d] or msg buff err!\r\n",size);
        return -1;
    }

    uint8_t crc_chceck;
    static uint8_t rx_cnt = 0;
	static uint8_t rx_buf[8] = {0};
#if 0
     hex_config_data_print("can", "recv(0x):", buf, 8);   
#endif

    crc_chceck = can_j1850_crc8(buf, 7);
    if (crc_chceck != buf[7]) {
        rt_log_error("can_ecu_778 Error checksum = 0x%x, buf[7] = 0x%x !\r\n", \
                    crc_chceck, buf[7]);
        return -1;
    }

    if (rx_cnt == 0) {
		memcpy(rx_buf, buf, 8);
		rx_cnt++;
	} else if (rx_cnt == 1) {
		if (memcmp(rx_buf, buf, 8) == 0) {
			rx_cnt++;
		} else{
			memcpy(rx_buf, buf, 8);
			rx_cnt = 1;
		}
	} else if(rx_cnt == 2) {
		if (memcmp(rx_buf, buf, 8) == 0) {
			rx_cnt++;
		} else {
			memcpy(rx_buf, buf, 8);
			rx_cnt = 1;
		}
	}

	if (rx_cnt == 3) {
		rx_cnt = 0;
		uint32_t odo;
        uint32_t tmp = 0;
#if 0
        if (!get_hcn_mile_param(HCN_MILE_PARAM_ODO, &tmp)) {
            rt_log_error("Get HCN_MILE_PARAM_ODO error!\r\n");
            return -1;
        }
#endif
        odo = (((uint32_t) buf[0]) << 24 ) |  (((uint32_t) buf[1]) << 16 ) | \
                (((uint32_t) buf[2]) << 8 ) |(((uint32_t) buf[3]) << 0);
		if (odo != tmp) {
            //set_hcn_mile_param(HCN_MILE_PARAM_ODO, &odo);
            vehicle_set_data(VEH_MILEAGE_CHANGE_MSG, 3);
		}
	}

	return 0;
}

static int parse_can_msg_779_msg(uint8_t *buf, uint8_t size) {
    if (get_check_self_state() < CHECK_SELF_STATE_SUCCESS) {
        return 0;
    }
    
    if ((buf == NULL) || (size < 8)) {
        rt_log_error("can_ecu_779 dlc[%d] or msg buff err!\r\n",size);
        return -1;
    }

    uint8_t crc_chceck;
    static uint8_t rx_cnt = 0;
	static uint8_t rx_buf[8] = {0};
#if 0
     hex_config_data_print("can", "recv(0x):", buf, 8);   
#endif

    crc_chceck = can_j1850_crc8(buf, 7);
    if (crc_chceck != buf[7]) {
        rt_log_error("can_ecu_779 Error checksum = 0x%x, buf[7] = 0x%x !\r\n", \
                    crc_chceck, buf[7]);
        return -1;
    }

    if (rx_cnt == 0) {
		memcpy(rx_buf, buf, 8);
		rx_cnt++;
	} else if (rx_cnt == 1) {
		if (memcmp(rx_buf, buf, 8) == 0) {
			rx_cnt++;
		} else{
			memcpy(rx_buf, buf, 8);
			rx_cnt = 1;
		}
	} else if(rx_cnt == 2) {
		if (memcmp(rx_buf, buf, 8) == 0) {
			rx_cnt++;
		} else {
			memcpy(rx_buf, buf, 8);
			rx_cnt = 1;
		}
	}

	if (rx_cnt == 3) {
		if ((buf[0] == 0xa1) && (buf[1] == 0xb1) 
            && (buf[2] == 0xc1) && (buf[3] == 0xd1) 
            && (buf[4] == 0xe1) && (buf[5] == 0xf1) 
            && (buf[6] == 0xee)) {
            extern void clean_eeprom_operate(void);
            clean_eeprom_operate();
            rt_thread_mdelay(1000);
            //send_mcu_clear_eeprom();
            rt_log_info("clear eerpom, os will reboot...\r\n"); 
        }
	}

	return 0;
}

static void can_msg_process(struct rt_can_msg * msg) {
    if (msg == NULL) {
        return;
    }

#ifdef CAN_RX_DEBUG
    hex_config_data_print(__FUNCTION__, ":recv(0x)", pMsg->Data, pMsg->DLC);
#endif

    switch (msg->id) {
        case CAN_ENGINGE_RPM_ID:
            parse_can_msg_110_msg(msg->data, msg->len);
            break;

        case CAN_DRIVE_MODE_ID:
            parse_can_msg_111_msg(msg->data, msg->len);
            break;

        case CAN_LIGHT_STATE_ID:
            parse_can_msg_112_msg(msg->data, msg->len);
            break;

        case CAN_TCS_WARNING_ID:
            parse_can_msg_120_msg(msg->data, msg->len);
            break;

        case CAN_SPEED_ID:
            parse_can_msg_122_msg(msg->data, msg->len);

        case CAN_ABS_WARNING_ID:
            parse_can_msg_12b_msg(msg->data, msg->len);
            break;

        case CAN_SET_TRIP_A:
            parse_can_msg_776_msg(msg->data, msg->len);
            break;

        case CAN_SET_TRIP_B:
            parse_can_msg_777_msg(msg->data, msg->len);
            break;

        case CAN_SET_ODO:
            parse_can_msg_778_msg(msg->data, msg->len);
            break;

        case CAN_CLEAN_EEPROM:
            parse_can_msg_779_msg(msg->data, msg->len);
            break;

        default:
            break;
    }
}

static void can_rx_thread(void *param) {
    rt_size_t size;
    struct rt_can_msg rxmsg = {0};

    rt_kprintf("Can rx thread success!.\n");

    while (1) {
        rt_sem_take(&can_rx_sem, RT_WAITING_FOREVER);
        rxmsg.hdr = -1;
        size = rt_device_read(can_dev, 0, &rxmsg, sizeof(rxmsg));
        if (!size) {
            rt_kprintf("CAN read error\n");
            continue;
        }
        can_msg_process(&rxmsg);
        rt_thread_mdelay(1);
    }

    rt_device_close(can_dev);
}

static void can_timeout_thread(void *param) {
    while (1) {
        timeout_clear_can_data();
        rt_thread_mdelay(100);
    }
}

static void can_timeout_enable(void) {
    rt_thread_t thread;

    thread = rt_thread_create("can_timeout", can_timeout_thread, RT_NULL,
                                  1024, CAN_RX_THREAD_PRIO + 1, 10);
    if (thread != RT_NULL) {
        rt_thread_startup(thread);
    } else {
        rt_kprintf("create can_timeout thread failed!\n");
        return;
    }
}

static void can_rx_enable(void) {
    rt_thread_t thread;

    /* enable Can Rx interrupt */
    rt_device_control(can_dev, RT_DEVICE_CTRL_SET_INT, NULL);
    rt_device_set_rx_indicate(can_dev, can_rx_callback);
    rt_sem_init(&can_rx_sem, "usr_rx_sem", 0, RT_IPC_FLAG_PRIO);

    thread = rt_thread_create("can_rx", can_rx_thread, RT_NULL,
                                  2048, CAN_RX_THREAD_PRIO, 10);
    if (thread != RT_NULL) {
        rt_thread_startup(thread);
    } else {
        rt_kprintf("create can_rx thread failed!\n");
        return;
    }
}

int mw_can_init(void) {
    rt_err_t ret = 0;

    can_dev = rt_device_find(MW_CAN_DEVICE_NAME);
    if (!can_dev) {
        rt_kprintf("Can not find %s dev\n", MW_CAN_DEVICE_NAME);
        return -1;
    }

    ret = rt_device_open(can_dev, 
                        RT_DEVICE_FLAG_INT_TX | RT_DEVICE_FLAG_INT_RX);
    if (ret) {
        rt_kprintf("%s open failed!\n", MW_CAN_DEVICE_NAME);
        return -1;
    }

    ///< 设置为500kbps波特率
    ret = rt_device_control(can_dev, RT_CAN_CMD_SET_BAUD, (void *)CAN500kBaud);
    if (ret) {
        rt_kprintf("set %s baud failed\n", MW_CAN_DEVICE_NAME);
        ret = -1;
        rt_device_close(can_dev);
        goto __exit;
    }

    can_rx_enable();
    can_timeout_enable();
    can_tx_init(can_dev);

    rt_kprintf("can init success!\n");

__exit:
    return ret;
}

#endif
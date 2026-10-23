#include <string.h>
#include <rtthread.h>
#include "rtdevice.h"
#include "app_log.h"
#include "aic_crc32.h"
#include "mile_param.h"
#include "hal_eeprom.h"

#if MILEAGE_PARAM_ENABLE

#define DEBUG_MILE_PARAM_ENABLE

#define ZONE_NUM                    (3)
#define SLOT_PER_ZONE               (7)
#define ZONE_SIZE                   (sizeof(mile_param_t) * SLOT_PER_ZONE)
#define MILEAGE_PARAM_START_ADDR    (0x00)
#define MILEAGE_PARAM_TOTAL_SIZE    (ZONE_SIZE * ZONE_NUM)
#define MAX_WRITE_RETRIES           (3)

#define MILE_TASK_PRIORITY      10         
#define SAVE_QUEUE_SIZE         5

#define INVALID_DATA            (0xFFFFFFFF)
#define INVALID_DATA16          (0xFFFF)
#define ODO_MAX_DATA            (999999999)
#define TRIP_MAX_DATA           (999999)
#define MILE_DATA_MARGIN_RANGE  (200)   ///< 里程数据误差范围

static rt_mutex_t g_param_mutex;
static rt_mq_t g_save_queue;

static mile_param_t mile_param; 
static mile_param_t mile_param_pre = { 
    .erase_counter = 0xffffffff,
    .checksum = 0xffffffff,
};

static bool is_recovery_mile_param = false;
static int g_latest_valid_slot[ZONE_NUM];

/**
 * @brief  获取分区对应槽位的地址
 * @param  zone 分区编号 0-2
 * @param  slot 槽位 0-6
 * @return 对应槽位地址
 */
static uint16_t get_slot_addr(uint8_t zone, uint8_t slot) {
    if (zone >= ZONE_NUM || slot >= SLOT_PER_ZONE) {
        rt_log_error("Invalid zone(%d) or slot(%d)\n", zone, slot);
        return INVALID_DATA16;
    }
    return MILEAGE_PARAM_START_ADDR + (zone * ZONE_SIZE) + \
            (slot * sizeof(mile_param_t));
}

/**
 * @brief  读取对应槽位的mile_param数据
 * @param  zone 分区编号 0-2
 * @param  slot 槽位 0-6
 * @param  param 里程参数数据指针
 * @return 0:成功 -1:失败
 */
static int read_slot_from_eeprom(uint8_t zone, uint8_t slot, 
                                mile_param_t *param) {
    if (!param) return -1;
    uint16_t addr = get_slot_addr(zone, slot);
    if (addr == INVALID_DATA16) return -1;
    int ret = e2prom_read_data(addr, (uint8_t*)param, sizeof(mile_param_t));
    return (ret == sizeof(mile_param_t)) ? 0 : -1;
}

/**
 * @brief  写对应对应槽位的mile_param数据
 * @param  zone 分区编号 0-2
 * @param  slot 槽位 0-6
 * @param  param 里程参数数据指针
 * @return 0:成功 -1:失败
 */
static int write_slot_to_eeprom(uint8_t zone, uint8_t slot, 
                        const mile_param_t *param) {
    if (!param) return -1;
    uint16_t addr = get_slot_addr(zone, slot);
    if (addr == INVALID_DATA16) return -1;
    int ret = e2prom_write_data(addr, (uint8_t*)param, sizeof(mile_param_t));
    return (ret == sizeof(mile_param_t)) ? 0 : -1;
}

/**
 * @brief  判断槽位数据是否有效
 * @param  param 里程参数数据指针
 * @return true:有效 false:无效
 */
static bool is_valid_param(const mile_param_t *param) {
    if (!param) {
        return false;
    }
    uint32_t calculated_checksum = crc32(0, (uint8_t*)param, \
                                    sizeof(mile_param_t) - 4);
    return (calculated_checksum == param->checksum);
}

static bool params_are_identical(const mile_param_t *a, const mile_param_t *b) {
    return (memcmp(a, b, sizeof(mile_param_t) - sizeof(uint32_t)) == 0);
}

static bool params_are_similar(const mile_param_t *a, const mile_param_t *b) {
    uint32_t odo_diff = (a->odo > b->odo) ? (a->odo - b->odo) : \
                        (b->odo - a->odo);
    uint32_t trip_diff = (a->trip > b->trip) ? (a->trip - b->trip) : \
                        (b->trip - a->trip);

    if (a->odo == INVALID_DATA || b->odo == INVALID_DATA ||
        a->trip == INVALID_DATA || b->trip == INVALID_DATA) {
        return false;
    }

    bool other_fields_match =
        (a->ride_time == b->ride_time) &&
        (a->range == b->range) &&
        (a->avg_elec_cons == b->avg_elec_cons);

    return (odo_diff <= MILE_DATA_MARGIN_RANGE 
            && trip_diff <= MILE_DATA_MARGIN_RANGE 
            && other_fields_match);
}

static bool is_all_ff_param(const mile_param_t *param) {
    if (!param) return false;
    uint8_t *ptr = (uint8_t*)param;
    for (size_t i = 0; i < sizeof(mile_param_t); i++) {
        if (ptr[i] != 0xFF) {
            return false;
        }
    }
    return true;
}

/**
 * @brief  找出当前区域最新的槽位
 * @param  zone 分区范围 0-1
 * @return 返回最新的槽位区域
 */
static int find_latest_valid_slot_in_zone(uint8_t zone) {
    mile_param_t temp_param;
    int latest_valid_slot = -1;
    uint32_t highest_erase_counter = 0;

    for (int slot = 0; slot < SLOT_PER_ZONE; slot++) {
        if (read_slot_from_eeprom(zone, slot, &temp_param) == 0 
            && is_valid_param(&temp_param)) {
            if (latest_valid_slot == -1 || temp_param.erase_counter > \
                highest_erase_counter) {
                latest_valid_slot = slot;
                highest_erase_counter = temp_param.erase_counter;
            }
        }
    }
    return latest_valid_slot;
}

/**
 * @brief  读取仲裁逻辑
 * @param 无
 * @param  1.至少两个分区的参数相同 
 * @param  2.两个分区参数不同，odo,trip误差在范围内，其余参数相同
 * @param  3.以上两者都不相同，使用默认值
 * @return 0：成功读取  -1:失败
 */
static int perform_read_arbitration(void) {
    mile_param_t zone_data[ZONE_NUM];
    bool data_is_valid[ZONE_NUM] = {false};
    bool data_is_all_ff[ZONE_NUM] = {false}; 

    for (int z = 0; z < ZONE_NUM; z++) {
        if (g_latest_valid_slot[z] != -1) {
            if (read_slot_from_eeprom(z, g_latest_valid_slot[z], \
                    &zone_data[z]) == 0) {
                data_is_valid[z] = true;
                if (is_all_ff_param(&zone_data[z])) {
                    ///< 读取出的全是0xFF
                    data_is_valid[z] = true;
                    data_is_all_ff[z] = true;
                } else if (is_valid_param(&zone_data[z])) {
                    data_is_valid[z] = true;
                }
            } else {
                rt_log_warn("zone=%d, slot=%d invalid, rescanning...\n", 
                            g_latest_valid_slot[z], z);
                g_latest_valid_slot[z] = find_latest_valid_slot_in_zone(z);
                if (g_latest_valid_slot[z] != -1) {
                    if (read_slot_from_eeprom(z, g_latest_valid_slot[z], \
                        &zone_data[z]) == 0) {
                        if (is_all_ff_param(&zone_data[z])) {
                            ///< 读取出的全是0xFF
                            data_is_valid[z] = true;
                            data_is_all_ff[z] = true;
                        } else if (is_valid_param(&zone_data[z])) {
                            data_is_valid[z] = true;
                        }
                    }
                }
            }
        }
    }

    ///< 优先处理0xFF空片
    uint8_t invalid_zone = 0;
    for (int z = 0; z < ZONE_NUM; z++) {
        if (data_is_all_ff[z]) {
            invalid_zone++;
        }
    }
    if (invalid_zone == ZONE_NUM) {
        rt_log_info("The eerpom device is empty!\n");
        return -1; 
    }

    for (int i = 0; i < ZONE_NUM; i++) {
        if (!data_is_valid[i] || data_is_all_ff[i]) continue;
        for (int j = i + 1; j < ZONE_NUM; j++) {
            if (!data_is_valid[j] || data_is_all_ff[j]) continue;
            if (params_are_identical(&zone_data[i], &zone_data[j])) {
                memcpy(&mile_param, &zone_data[i], sizeof(mile_param_t));
                rt_log_info("Two mile param zone is same, zone %d and %d.\n", i, j);
                return 0;
            }
        }
    }

    for (int i = 0; i < ZONE_NUM; i++) {
        if (!data_is_valid[i] || data_is_all_ff[i]) continue;
        for (int j = i + 1; j < ZONE_NUM; j++) {
            if (!data_is_valid[j] || data_is_all_ff[j]) continue;
            if (params_are_similar(&zone_data[i], &zone_data[j])) {
                memcpy(&mile_param, &zone_data[i], sizeof(mile_param_t));
                rt_log_info("Found mile param similar data in zones %d and %d.\n", i, j);
                return 0;
            }
        }
    }

    rt_log_error("Not find same mile param zone!\n");

    return -1;
}

/**
 * @brief  写入仲裁逻辑
 * @param  new_param 里程参数指针
 * @param  1.三个分区都写入成功，本次参数才认为写入成功
 * @param  2.任意分区参数写入失败后，查找一下槽位，尝试写入三次，如果还不成功，
 * @param  3.该次写入失败
 * @return 0：写入成功  -1:失败
 */
static int perform_write_arbitration(const mile_param_t *new_param) {
    int next_write_slots[ZONE_NUM];
    bool slot_initialized[ZONE_NUM] = {false};

    for (int z = 0; z < ZONE_NUM; z++) {
        if (g_latest_valid_slot[z] == -1) {
            next_write_slots[z] = 0;
        } else {
            next_write_slots[z] = (g_latest_valid_slot[z] + 1) % SLOT_PER_ZONE;
        }
        slot_initialized[z] = true;
    }

    for (int attempt = 0; attempt < MAX_WRITE_RETRIES; attempt++) {
        bool all_success = true;

        for (int z = 0; z < ZONE_NUM; z++) {
            if (!slot_initialized[z]) {
                continue;
            }
            int ret = write_slot_to_eeprom(z, next_write_slots[z], new_param);
            if (ret == 0) {
                slot_initialized[z] = false;
            } else {
                all_success = false;
                rt_log_error("Write param failed times:%d zone:%d, slot:%d.\n", \
                    attempt + 1, z, next_write_slots[z]);
            }
        }

        if (all_success) {
            for (int z = 0; z < ZONE_NUM; z++) {
                g_latest_valid_slot[z] = next_write_slots[z];
            }
            return 0;
        }

        if (attempt < MAX_WRITE_RETRIES - 1) {
            rt_log_info("Write param times:%d, Retrying...\n", attempt + 1);
            for (int z = 0; z < ZONE_NUM; z++) {
                if (slot_initialized[z]) {
                    next_write_slots[z] = (next_write_slots[z] + 1) % SLOT_PER_ZONE;
                }
            }
        }
    }

    rt_log_error("Try max write param times:%d, write failed!\n", MAX_WRITE_RETRIES);
    return -1;
}

static int save_mile_param(const mile_param_t *param) {
    if (!is_recovery_mile_param || !param) { 
        rt_log_error("param error or recovery param is not ok!\n");
        return -1;
    }

    if (memcmp(&mile_param_pre, param, sizeof(mile_param_t) - 4) == 0) {
        rt_log_debug("Mile param data is the same, don't save.\n");
        return -1;
    }
    
    memcpy(&mile_param, param, sizeof(mile_param_t));
    mile_param.erase_counter++;
    mile_param.checksum = crc32(0, (uint8_t*)&mile_param, \
            sizeof(mile_param_t) - 4);
    int ret = perform_write_arbitration(&mile_param);
    if (ret != 0) {
        rt_log_error("write mile param failed!\n");
        return -1;  
    }
    memcpy(&mile_param_pre, &mile_param, sizeof(mile_param_t));
    return ret;
}

bool get_recovery_mile_param(void) {
    if (!g_param_mutex) return false;
    int err = rt_mutex_take(g_param_mutex, RT_WAITING_FOREVER);
    if (err != 0) {
        rt_log_error("Failed to take mutex in get_recovery_mile_param: %d\n", err);
        return false;
    }
    bool result = is_recovery_mile_param;
    rt_mutex_release(g_param_mutex);
    return result;
}

bool set_mile_param(mile_param_handle_e id, void *param) {
    if (!g_param_mutex || !g_save_queue || !param) return false;

    if (!is_recovery_mile_param) {
        rt_log_error("Mile param not initialized.\n");
        return false;
    }

    int err = rt_mutex_take(g_param_mutex, RT_WAITING_FOREVER);
    if (err != 0) {
        rt_log_error("Failed to take mutex in set_hcn_mile_param: %d\n", err);
        return false;
    }

    mile_param_t temp_param;
    memcpy(&temp_param, &mile_param, sizeof(mile_param_t));

   bool valid_id = true;
    switch (id) {
        case MILE_PARAM_ODO:
            temp_param.odo = *((uint32_t *)param);
            break;
        case MILE_PARAM_TRIP:
            temp_param.trip = *((uint32_t *)param);
            break;
        case MILE_PARAM_RIDE_TIME:
            temp_param.ride_time = *((uint32_t *)param);
            break;
        case MILE_PARAM_AVG_ELEC_CONS:
            temp_param.avg_elec_cons = *((uint16_t *)param);
            break;
        case MILE_PARAM_RANGE:
            temp_param.range = *((uint16_t *)param);
            break;
        case MILE_PARAM_NFC_UID1:
            temp_param.nfc_uid1 = *((uint32_t *)param);
            break;
        case MILE_PARAM_NFC_UID2:
            temp_param.nfc_uid2 = *((uint32_t *)param);
            break;
        case MILE_PARAM_WIRELESS_KEY1:
            temp_param.wireless_key1 = *((uint32_t *)param);
            break;
        case MILE_PARAM_WIRELESS_KEY2:
            temp_param.wireless_key2 = *((uint32_t *)param);
            break;
        default:
            valid_id = false;
            break;
    }

    if (!valid_id) {
        rt_mutex_release(g_param_mutex);
        rt_log_error("Invalid param handle ID: %d\n", id);
        return false;
    }

    bool should_enqueue = (memcmp(&mile_param, &temp_param, \
                        sizeof(mile_param_t) - 4) != 0);
    if (!should_enqueue) {
        rt_mutex_release(g_param_mutex);
        return true;
    }

    rt_mutex_release(g_param_mutex);

    err = rt_mq_send(g_save_queue, &temp_param, sizeof(mile_param_t));
    if (err != 0) {
        rt_log_error("Failed to send mile param queue, id = %d\n", id);
        return false;
    }
   
    return true;
}

bool get_mile_param(mile_param_handle_e id, void *param) {
    if (!g_param_mutex || !param) return false;

    int err = rt_mutex_take(g_param_mutex, RT_WAITING_FOREVER);
    if (err != 0) {
        rt_log_error("Failed to take mutex in get_hcn_mile_param: %d\n", err);
        return false;
    }

    if (!is_recovery_mile_param) {
        rt_mutex_release(g_param_mutex);
        rt_log_error("Mile param not initialized\n");
        return false;
    }

    bool status = true;
    switch (id) {
        case MILE_PARAM_ODO:
            *((uint32_t *)param) = mile_param.odo;
            break;
        case MILE_PARAM_TRIP:
            *((uint32_t *)param) = mile_param.trip;
            break;
        case MILE_PARAM_RIDE_TIME:
            *((uint32_t *)param) = mile_param.ride_time;
            break;
        case MILE_PARAM_AVG_ELEC_CONS:
            *((uint16_t *)param) = mile_param.avg_elec_cons;
            break;
        case MILE_PARAM_RANGE:
            *((uint16_t *)param) = mile_param.range;
            break;
        case MILE_PARAM_NFC_UID1:
            *((uint32_t *)param) = mile_param.nfc_uid1;
            break;
        case MILE_PARAM_NFC_UID2:
            *((uint32_t *)param) = mile_param.nfc_uid2;
            break;
        case MILE_PARAM_WIRELESS_KEY1:
            *((uint32_t *)param) = mile_param.wireless_key1;
            break;
        case MILE_PARAM_WIRELESS_KEY2:
            *((uint32_t *)param) = mile_param.wireless_key2;
            break;
        default:
            status = false;
            break;
    }

    rt_mutex_release(g_param_mutex);
    return status;
}

static void printf_mile_param_info(mile_param_t *param) {
    if (param) {
        rt_log_info("\n--------mile param start--------\n");
        rt_log_info("ODO:%lu\n", param->odo);
        rt_log_info("trip:%lu\n", param->trip);
        rt_log_info("ride_time:%lu\n", param->ride_time);
        rt_log_info("avg_elec_cons:%d\n", param->avg_elec_cons);
        rt_log_info("range:%d\n", param->range);
        rt_log_info("NFC uid1:%08lx\n", (unsigned long)param->nfc_uid1);
        rt_log_info("NFC uid2:%08lx\n", (unsigned long)param->nfc_uid2);
        rt_log_info("wire key1:%08lx\n", (unsigned long)param->wireless_key1);
        rt_log_info("wire key2:%08lx\n", (unsigned long)param->wireless_key2);
        rt_log_info("mile param end--------\n");
    }
}

static void check_mile_param(void) {
    if (mile_param.odo == INVALID_DATA) {
        mile_param.odo = 0;
    }

    if (mile_param.odo > ODO_MAX_DATA && (mile_param.odo != INVALID_DATA)) {
        mile_param.odo = ODO_MAX_DATA;
    }

    if (mile_param.trip == INVALID_DATA) {
        mile_param.trip = 0;
    }

    if (mile_param.trip > TRIP_MAX_DATA && (mile_param.trip != INVALID_DATA)) {
        mile_param.trip = TRIP_MAX_DATA;
    }

    if (mile_param.ride_time == INVALID_DATA) {
        mile_param.ride_time = 0;
    }

    if (mile_param.range == INVALID_DATA16) {
        mile_param.range = 0;
    }

    if (mile_param.avg_elec_cons == INVALID_DATA16) {
        mile_param.avg_elec_cons = 0;
    }

    if (mile_param.all_elec_cons == INVALID_DATA) {
        mile_param.all_elec_cons = 0;
    }

    is_recovery_mile_param = true;
    printf_mile_param_info(&mile_param);
}

static int read_mile_param(void) {
    int ret = -1;

    if (e2prom_init() != 0) {
        rt_log_error("e2prom init failed in task!\n");
        return -1;
    }

    for (int i = 0; i < ZONE_NUM; i++) {
        g_latest_valid_slot[i] = -1;
    }

    for (int z = 0; z < ZONE_NUM; z++) {
        g_latest_valid_slot[z] = find_latest_valid_slot_in_zone(z);
    }

    rt_log_info("Find mile laste slot:[%d %d %d]\n", g_latest_valid_slot[0], \
                    g_latest_valid_slot[1], g_latest_valid_slot[2]);
    if (perform_read_arbitration() == 0) {
        rt_log_info("Mile param read success.\n");
        ret = 0;
    } else {
        rt_log_info("Use default mile param.\n");
        memset(&mile_param, 0, sizeof(mile_param_t));
        memset(&mile_param_pre, 0xFF, sizeof(mile_param_pre)); 
        mile_param_t initial_param = {0};
        rt_err_t err = rt_mq_send(g_save_queue, &initial_param, sizeof(mile_param_t));
        if (err != RT_EOK) {
            rt_log_error("Failed to send initial queue.\n");
            return -1;
        } else {
            rt_log_info("Initial save request queued success.\n");
            ret = 0; 
        }

        ret = 0;
    }

    check_mile_param();

    return ret;
}

static void mile_param_thread(void *parameter) {
    mile_param_t request;    
    int err;

    if (read_mile_param() != 0) {
        rt_log_error("Read mile param failed!\n");
    }

    for (;;) {
        err = rt_mq_recv(g_save_queue, &request, 
                        sizeof(mile_param_t), 
                        RT_WAITING_FOREVER);
        if (err != 0) {
            rt_log_error("Failed to receive from save queue, err=%d\n", err);
            continue;
        }

        rt_mutex_take(g_param_mutex, RT_WAITING_FOREVER);
        save_mile_param(&request);
        rt_mutex_release(g_param_mutex);
    }
}

int mile_param_init(void) {
    rt_thread_t thread;

    g_param_mutex = rt_mutex_create("mile_param_lock", RT_IPC_FLAG_FIFO);
    if (g_param_mutex == RT_NULL) {
        rt_log_error("Failed to create mutex for mile_param.\n");
        return -1;
    }

     g_save_queue = rt_mq_create("save_queue", sizeof(mile_param_t), 
                                    SAVE_QUEUE_SIZE, RT_IPC_FLAG_FIFO);
    if (g_save_queue == RT_NULL) {
        rt_log_error("Failed to create save queue for mile_param.\n");
        rt_mutex_delete(g_param_mutex);
        g_param_mutex = RT_NULL;
        return -1;
    }

    memset(&mile_param, 0, sizeof(mile_param_t));
    memset(&mile_param_pre, 0xFF, sizeof(mile_param_pre));
    for (int i = 0; i < ZONE_NUM; i++) {
        g_latest_valid_slot[i] = -1;
    }

    thread = rt_thread_create("mile_praram_thread",
                                   mile_param_thread,
                                   RT_NULL,
                                   2048,
                                   MILE_TASK_PRIORITY,
                                   20);
    if (thread) {
        rt_thread_startup(thread);
    } else {
        rt_log_info("Create mile param thread faile!\n");
        rt_mq_delete(g_save_queue);
        g_save_queue = RT_NULL;
        rt_mutex_delete(g_param_mutex);
        g_param_mutex = RT_NULL;
        return -1;
    }

    rt_log_info("mile param init success!\n");

    return 0;
}

#endif
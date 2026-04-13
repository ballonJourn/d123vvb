#include "avg_consumption.h"
#include <rtdevice.h>
#include <string.h>
#include <math.h>
#include "app_log.h" 
#include "aic_crc32.h" 

#if AVG_CONSUMPTION_ENABLE

#define SAMPLE_PERIOD_MS     100                 /* 采样周期 (ms) */
#define SAMPLE_PERIOD_SEC    (SAMPLE_PERIOD_MS / 1000.0)
#define SEC_PER_HOUR         3600.0

#define SPEED_THRESHOLD_KMH  0.5        /* 速度阈值*/

#define MAX_DISTANCE_KM      1000000.0  /* 100万公里 */
#define MAX_ENERGY_WH        100000000.0 /* 100MWh */

#define SAVE_DISTANCE_DELTA_KM 1.0  /* 每行驶1km保存一次 */
#define SAVE_TIME_DELTA_MINUTES 60  /* 每小时保存一次 */

typedef struct {
    double total_energy_wh;         /* 累计耗电量 (Wh) */
    double total_regen_energy_wh;   /* 累计回收能量 (Wh) */
    double total_distance_km;       /* 累计里程 (km) */
    double avg_consumption;         /* 平均电耗 (Wh/km) */
    uint32_t last_save_distance;    /* 上次保存时的里程 (dm, decimeter) */
    uint32_t last_save_minutes;     /* 上次保存时的分钟数 */
} AvgConsumptionData;

static AvgConsumptionData sys_data;      /* 系统累计数据（永久） */
static AvgConsumptionData trip_data;     /* 旅程数据（可复位） */

static rt_timer_t sample_timer = RT_NULL; /* 采样定时器 */
static rt_mutex_t data_mutex = RT_NULL;   /* 保护共享数据的互斥锁 */

static void update_avg_value(AvgConsumptionData *data);
static void sample_timeout(void *parameter);
static int load_data_from_storage(void);
static int save_data_to_storage(void);
static void avg_consumption_get_power_speed(double *power_w, double *speed_kmh);

/**
 * @brief 更新单个数据结构的平均电耗
 */
static void update_avg_value(AvgConsumptionData *data) {   
    if (data->total_distance_km > 1e-6) { 
        data->avg_consumption = data->total_energy_wh / data->total_distance_km;
    } else {
        data->avg_consumption = 0.0;
    }
}

void avg_consumption_update(double accumulated_energy_mwh, double distance_m) {
    if (!data_mutex) return;
    rt_mutex_take(data_mutex, RT_WAITING_FOREVER);

    double energy_wh = accumulated_energy_mwh / 1000.0; ///< mWh -> Wh
    double distance_km = distance_m / 1000.0; ///< m -> km

    if (energy_wh > 0) {
        sys_data.total_energy_wh += energy_wh;
        trip_data.total_energy_wh += energy_wh;
    } else {
        sys_data.total_regen_energy_wh -= energy_wh;  ///< 能量回收
        trip_data.total_regen_energy_wh -= energy_wh;
    }
    
    sys_data.total_distance_km += distance_km;
    trip_data.total_distance_km += distance_km;

    if (sys_data.total_distance_km > MAX_DISTANCE_KM ||
        sys_data.total_energy_wh > MAX_ENERGY_WH) {
        rt_log_error("AvgCons: Data overflow detected! Resetting.\n");
        memset(&sys_data, 0, sizeof(sys_data));
    }

    update_avg_value(&sys_data);
    update_avg_value(&trip_data);

#if 0
    // 检查是否需要保存到Flash (此部分逻辑可以按需保留或移到其他地方)
    // uint32_t current_distance_dm = (uint32_t)(sys_data.total_distance_km * 10);
    // uint32_t current_minutes = rt_tick_get() / (RT_TICK_PER_SECOND * 60);
#endif

    rt_mutex_release(data_mutex);
}


/**
 * @brief 采样回调函数，每次定时器触发时调用
 * @param parameter 未使用
 */
static void sample_timeout(void *parameter) {
    double power_w, speed_kmh;
    avg_consumption_get_power_speed(&power_w, &speed_kmh);

    if (isnan(power_w) || isnan(speed_kmh) || \
        isinf(power_w) || isinf(speed_kmh)) {
        return;
    }

    if (speed_kmh < SPEED_THRESHOLD_KMH) {
        return;
    }

    double delta_time_h = SAMPLE_PERIOD_SEC / SEC_PER_HOUR;
    double delta_energy_wh = power_w * delta_time_h;
    double delta_distance_km = speed_kmh * delta_time_h;

    if (delta_energy_wh > 0) {
        sys_data.total_energy_wh += delta_energy_wh;
        trip_data.total_energy_wh += delta_energy_wh;
    } else {
        sys_data.total_regen_energy_wh -= delta_energy_wh; 
        trip_data.total_regen_energy_wh -= delta_energy_wh;
    }
    
    sys_data.total_distance_km += delta_distance_km;
    trip_data.total_distance_km += delta_distance_km;

    if (sys_data.total_distance_km > MAX_DISTANCE_KM ||
        sys_data.total_energy_wh > MAX_ENERGY_WH) {
        rt_log_error("AvgCons: Data overflow detected! Resetting.\n");
        memset(&sys_data, 0, sizeof(sys_data));
    }

    update_avg_value(&sys_data);
    update_avg_value(&trip_data);

    uint32_t current_distance_dm = (uint32_t)(sys_data.total_distance_km * 10); 
    uint32_t current_minutes = rt_tick_get() / (RT_TICK_PER_SECOND * 60);

    if ((current_distance_dm - sys_data.last_save_distance) >= \
                                (SAVE_DISTANCE_DELTA_KM * 10) || \
                        (current_minutes - sys_data.last_save_minutes) >= \
                        SAVE_TIME_DELTA_MINUTES) {
        avg_consumption_save_sys_data(); 
    }
}

/**
 * @brief 从非易失存储加载系统累计数据
 */
static int load_data_from_storage(void) {
#if 0
    rt_memcpy(&storage_data, (void*)STORAGE_ADDR, sizeof(storage_data));

    if (storage_data.magic != STORAGE_MAGIC) {
        rt_log_info("AvgCons: No valid data in Flash (magic mismatch). Using defaults.\n");
        return -1; // 数据无效
    }

    uint32_t calc_checksum = calculate_storage_checksum(&storage_data);
    if (calc_checksum != storage_data.checksum) {
        rt_log_warn("AvgCons: Data checksum failed! Calculated: 0x%08X, Stored: 0x%08X\n",
                     calc_checksum, storage_data.checksum);
        return -1; // 校验失败
    }
#else
    ///< 从eeprom中读取参数
#endif

    rt_log_info("load data from storage success.\n");
    return 0; 
}

/**
 * @brief 保存系统累计数据到非易失存储
 */
static int save_data_to_storage(void) {
#if 1
    ///< 存储到eeprom中
#endif
    sys_data.last_save_distance = (uint32_t)(sys_data.total_distance_km * 10);
    sys_data.last_save_minutes = rt_tick_get() / (RT_TICK_PER_SECOND * 60);

    return 0;
}

double avg_consumption_get_sys_avg(void) {
    if (!data_mutex) return 0.0;
    rt_mutex_take(data_mutex, RT_WAITING_FOREVER);
    double val = sys_data.avg_consumption;
    rt_mutex_release(data_mutex);
    return val;
}

double avg_consumption_get_sys_distance(void) {
    if (!data_mutex) return 0.0;
    rt_mutex_take(data_mutex, RT_WAITING_FOREVER);
    double val = sys_data.total_distance_km;
    rt_mutex_release(data_mutex);
    return val;
}

double avg_consumption_get_sys_energy(void) {
    if (!data_mutex) return 0.0;
    rt_mutex_take(data_mutex, RT_WAITING_FOREVER);
    double val = sys_data.total_energy_wh;
    rt_mutex_release(data_mutex);
    return val;
}

double avg_consumption_get_sys_regen_energy(void) {
    if (!data_mutex) return 0.0;
    rt_mutex_take(data_mutex, RT_WAITING_FOREVER);
    double val = sys_data.total_regen_energy_wh;
    rt_mutex_release(data_mutex);
    return val;
}

double avg_consumption_get_trip_avg(void) {
    if (!data_mutex) return 0.0;
    rt_mutex_take(data_mutex, RT_WAITING_FOREVER);
    double val = trip_data.avg_consumption;
    rt_mutex_release(data_mutex);
    return val;
}

double avg_consumption_get_trip_distance(void) {
    if (!data_mutex) return 0.0;
    rt_mutex_take(data_mutex, RT_WAITING_FOREVER);
    double val = trip_data.total_distance_km;
    rt_mutex_release(data_mutex);
    return val;
}

double avg_consumption_get_trip_energy(void) {
    if (!data_mutex) return 0.0;
    rt_mutex_take(data_mutex, RT_WAITING_FOREVER);
    double val = trip_data.total_energy_wh;
    rt_mutex_release(data_mutex);
    return val;
}

double avg_consumption_get_trip_regen_energy(void) {
    if (!data_mutex) return 0.0;
    rt_mutex_take(data_mutex, RT_WAITING_FOREVER);
    double val = trip_data.total_regen_energy_wh;
    rt_mutex_release(data_mutex);
    return val;
}

void avg_consumption_reset_trip(void) {
    if (!data_mutex) return;
    rt_mutex_take(data_mutex, RT_WAITING_FOREVER);
    rt_memset(&trip_data, 0, sizeof(trip_data));
    rt_log_info("Trip data reset.\n");
    rt_mutex_release(data_mutex);
}

void avg_consumption_save_sys_data(void) {
    if (!data_mutex) return;
    rt_mutex_take(data_mutex, RT_WAITING_FOREVER);
    save_data_to_storage();
    rt_mutex_release(data_mutex);
}

/**
 * @brief 获取当前瞬时功率和速度
 * @note 从can解析中获取功率和车速
 */
static void avg_consumption_get_power_speed(double *power_w, double *speed_kmh) {
    static int tick = 0;
    tick++;

    ///< 模拟车速和功率
    if (tick < 200) {
        *speed_kmh = tick * 0.5;           // 0~100 km/h 加速
        *power_w = 500 + tick * 25;
    } else if (tick < 500) {
        *speed_kmh = 100.0;                // 匀速
        *power_w = 3000;
    } else if (tick < 800) {
        *speed_kmh = 100.0 - (tick - 500) * 0.33;  // 减速
        *power_w = -500;                  // 能量回收
    } else if (tick < 1000) {
        *speed_kmh = 0.0;
        *power_w = 0.0;
    } else {
        tick = 0; // 重置模拟
        *speed_kmh = 0.0;
        *power_w = 0.0;
    }
}

int avg_consumption_init(void) {
    data_mutex = rt_mutex_create("avg_cons_mtx", RT_IPC_FLAG_FIFO);
    if (!data_mutex) {
        rt_log_error("AvgCons: Failed to create mutex.\n");
        return -RT_ERROR;
    }

    rt_memset(&sys_data, 0, sizeof(sys_data));
    rt_memset(&trip_data, 0, sizeof(trip_data));

    if (load_data_from_storage() != 0) {
        rt_log_info("use default values.\n");
    }

    sample_timer = rt_timer_create("avg_samp",
                                   sample_timeout,
                                   RT_NULL,
                                   SAMPLE_PERIOD_MS,
                                   RT_TIMER_FLAG_PERIODIC | RT_TIMER_FLAG_SOFT_TIMER);
    if (sample_timer == RT_NULL) {
        rt_log_error("AvgCons: Create timer failed.\n");
        rt_mutex_delete(data_mutex);
        data_mutex = RT_NULL;
        return -RT_ERROR;
    }
    rt_timer_start(sample_timer);

    rt_log_info("Avg consumption module init OK.\n");
    return RT_EOK;
}

#ifdef RT_USING_FINSH
#include <finsh.h>

static void show_avg_info(void)
{
    rt_kprintf("========== Avg Consumption Info ==========\n");
    rt_kprintf("System:\n");
    rt_kprintf("  Distance: %.3f km\n", avg_consumption_get_sys_distance());
    rt_kprintf("  Energy:   %.3f Wh\n", avg_consumption_get_sys_energy());
    rt_kprintf("  Regen:    %.3f Wh\n", avg_consumption_get_sys_regen_energy());
    rt_kprintf("  Avg:      %.2f Wh/km\n", avg_consumption_get_sys_avg());
    rt_kprintf("Trip:\n");
    rt_kprintf("  Distance: %.3f km\n", avg_consumption_get_trip_distance());
    rt_kprintf("  Energy:   %.3f Wh\n", avg_consumption_get_trip_energy());
    rt_kprintf("  Regen:    %.3f Wh\n", avg_consumption_get_trip_regen_energy());
    rt_kprintf("  Avg:      %.2f Wh/km\n", avg_consumption_get_trip_avg());
    rt_kprintf("==========================================\n");
}
MSH_CMD_EXPORT(show_avg_info, show average consumption info);

static void reset_trip_cmd(void)
{
    avg_consumption_reset_trip();
    rt_kprintf("Trip reset done.\n");
}
MSH_CMD_EXPORT(reset_trip_cmd, reset trip data);

static void save_sys_cmd(void)
{
    avg_consumption_save_sys_data();
    rt_kprintf("System data saved to Flash.\n");
}
MSH_CMD_EXPORT(save_sys_cmd, save system data to flash);

#endif /* RT_USING_FINSH */

#endif
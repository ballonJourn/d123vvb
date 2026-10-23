/* 续航里程计算 (XT1-MAX)
 *
 * 三包并联放电, 各自独立统计:
 *   CountMile += V × T, 每下降 1% SOC 结算一次
 *   RefMile = clamp(CountMile) → 推入长度 10 的滑动窗口 (RefMileArr)
 *   avg_RefMile = mean(RefMileArr)
 *   RemainMile_pack = avg_RefMile × SOC_pack,  总续航 = Σ 三包
 *
 * RefMileArr 下电保存于 usr_param 的 ref_mile_arr (JSON), 默认 1.00km。
 * 显示失效判据只有一条: 三包 SOC 全部 ≤5% (通信超时按 0 计) → "--km"。
 *
 * 开机初值: 上电先用上次落盘的续航 (mile_param.range, 由 mileage_calc 的百米
 * 保存点写入) 顶上, 待收到任一包有效 SOC 或超时 10s 后交回上述计算, 避免上电
 * 头几秒 UI 显示 "--km"、0x453 广播 0。
 */

#include "ref_mileage_calc.h"
#include "vehicle_data.h"      /* vehicle_get_data_speed / vehicle_get_data_bat_soc */
#include "vehicle_mile.h"      /* vehicle_get/set_ref_mile_arr / vehicle_set_mile_endurance */
#include "vehicle_argument.h"  /* vehicle_get_param_recovery */
#include <rtthread.h>

#define REF_MILE_TICK_MS          (100)     ///< 采样周期 T
#define REF_MILE_STOP_MS          (3000)    ///< 连续静止判据: 超过则该次参考里程不列入计算
#define REF_MILE_USE_TIMEOUT_MS   (10000)   ///< 本模块自维护的 SOC 通信超时判据
#define REF_MILE_DISPLAY_MIN      (5)       ///< 三包 SOC 均 ≤5% → 显示 "--km"
#define REF_MILE_MAX_DROP_PCT     (5)       ///< 单次 SOC 掉档上限, 超过视为异常跳变

/* 参考里程钳位区间 (0.01km 编码) */
#define REF_MILE_FIXED_MIN        (60)      ///< 固定包 0.60km
#define REF_MILE_FIXED_MAX        (95)      ///< 固定包 0.95km
#define REF_MILE_SWAP_MIN         (30)      ///< 换电包 0.30km
#define REF_MILE_SWAP_MAX         (55)      ///< 换电包 0.55km

#define REF_MILE_ARR_NUM          VEHICLE_REF_MILE_ARR_LEN
#define REF_MILE_ARR_DEFAULT_CK   (100)     ///< 默认参考里程 1.00km

///< 1 号包为固定包, 钳位区间与换电包不同
#define REF_MILE_FIXED_PACK       (1)

typedef struct {
    uint8_t   arr[REF_MILE_ARR_NUM];  ///< [0]=最旧(RefMile1) … [9]=最新(RefMile10), 0.01km
    uint16_t  sum;                    ///< arr 累加和, 入队后重算, 供求平均
    double    count_mile_km;          ///< 本 SOC 窗口累计里程 (km)
    int32_t   last_soc;               ///< 结算基准 SOC, -1 = 未建立
    uint8_t   last_valid_soc;         ///< 最后一次读到的有效 SOC
    rt_tick_t last_alive_tick;        ///< 最后一次"该包在通信"的时刻
    int32_t   soc_used;               ///< 本 tick 参与计算/显示的 SOC
    bool      had_long_stop;          ///< 本窗口内是否出现过连续静止 >3s
    bool      fixed_pack;             ///< true: 固定包 (钳位区间不同)
} ref_mile_pack_t;

static ref_mile_pack_t s_pack[REF_MILE_PACK_NUM] = {0};
static uint32_t s_stop_ms   = 0;        ///< 连续静止时长 (车速是全车信号, 三包共用)
static uint32_t s_total_x10 = 0;        ///< 三包续航总和, 0.1km
static bool     s_all_low   = true;     ///< 三包 SOC 是否全部 ≤5%
static bool     s_state     = false;    ///< 数据变化 → UI 刷新标志
static bool     s_loaded    = false;    ///< ref_mile_load 是否已完成 (初值/真值已建立)
static bool     s_boot_hold = false;    ///< 开机初值保持中 (见 ref_mile_load)
static rt_tick_t s_boot_hold_tick = 0;  ///< 初值注入时刻, 用于放弃保持的超时判据

static uint8_t ref_mile_clamp(const ref_mile_pack_t *pk, uint16_t centikm)
{
    uint16_t lo = pk->fixed_pack ? REF_MILE_FIXED_MIN : REF_MILE_SWAP_MIN;
    uint16_t hi = pk->fixed_pack ? REF_MILE_FIXED_MAX : REF_MILE_SWAP_MAX;

    if (centikm < lo) {
        return (uint8_t)lo;
    }
    if (centikm > hi) {
        return (uint8_t)hi;
    }

    return (uint8_t)centikm;
}

///< 数组左移丢最旧、尾部追加最新: 下标 0 恒为 RefMile1(最旧), 含义不随时间漂移
static void ref_mile_push(ref_mile_pack_t *pk, uint8_t pack, uint8_t centikm)
{
    uint16_t sum = 0;
    uint8_t i;

    for (i = 1; i < REF_MILE_ARR_NUM; i++) {
        pk->arr[i - 1] = pk->arr[i];
    }
    pk->arr[REF_MILE_ARR_NUM - 1] = centikm;

    for (i = 0; i < REF_MILE_ARR_NUM; i++) {
        sum += pk->arr[i];
    }
    pk->sum = sum;

    /* 数组只落 RAM, 由 IGN OFF 统一写 JSON; 参数未就绪时本次不入库 */
    vehicle_set_ref_mile_arr(pack, pk->arr);

    s_state = true;
}

///< 开机初值是否继续保持: 收到任一包有效 SOC, 或超时均未收到即交回正常计算
static bool ref_mile_hold_boot(void)
{
    uint8_t i;

    if (!s_boot_hold) {
        return false;
    }

    for (i = 0; i < REF_MILE_PACK_NUM; i++) {
        if (s_pack[i].last_valid_soc > 0) {
            s_boot_hold = false;
            return false;
        }
    }

    /* CAN 未给 SOC, 或 SOC 恒为 0(与"超时清零"无法区分): 超时后放弃保持 */
    if ((rt_tick_get() - s_boot_hold_tick)
        > rt_tick_from_millisecond(REF_MILE_USE_TIMEOUT_MS)) {
        s_boot_hold = false;
        return false;
    }

    return true;
}

///< 三包求和 + 失效判定, 每 tick 调用
static void ref_mile_publish(void)
{
    uint32_t total = 0;
    bool all_low = true;
    uint8_t i;

    /* 初值保持期间不重算也不发布, 让 ref_mile_load 注入的续航一直有效;
     * 0x455 的 count_mile 此时只是 0, 跳过不影响 */
    if (ref_mile_hold_boot()) {
        return;
    }

    for (i = 0; i < REF_MILE_PACK_NUM; i++) {
        int32_t soc = s_pack[i].soc_used;

        if (soc > REF_MILE_DISPLAY_MIN) {
            all_low = false;
        }
        /* remain(0.1km) = avg(0.01km/1%) × SOC × 10 / 1000 = sum × SOC / 100 */
        total += (uint32_t)s_pack[i].sum * (uint32_t)(soc > 0 ? soc : 0)
                 / (REF_MILE_ARR_NUM * 10);
    }

    if (total > 0xFFFF) {
        total = 0xFFFF;
    }

    if (total != s_total_x10 || all_low != s_all_low) {
        s_total_x10 = total;
        s_all_low   = all_low;
        /* 整数 km (0x453 原值直取) */
        vehicle_set_mile_endurance((uint16_t)(total / 10));
        s_state = true;
    }

    /* 0x455 调试帧: 1 号包当前 SOC 窗口内累计里程 (0.01km) */
    vehicle_set_ref_mile_count((uint16_t)(s_pack[REF_MILE_FIXED_PACK - 1].count_mile_km
                                          * 100.0 + 0.5));
}

static void ref_mile_tick(void)
{
    rt_tick_t now = rt_tick_get();
    int32_t v = vehicle_get_data_speed();
    double inc_km;
    uint8_t pack;

    if (v < 0) {
        v = 0;
    }

    s_stop_ms = (v == 0) ? (s_stop_ms + REF_MILE_TICK_MS) : 0;
    inc_km = (double)v * (REF_MILE_TICK_MS / 3600000.0);   /* km/h × s → km */

    for (pack = 0; pack < REF_MILE_PACK_NUM; pack++) {
        ref_mile_pack_t *pk = &s_pack[pack];
        int32_t soc_raw = vehicle_get_data_bat_soc(pack + 1);
        bool timeout;

        if (soc_raw > 0) {
            pk->last_alive_tick = now;
            pk->last_valid_soc  = (uint8_t)soc_raw;
        }

        /* mw_can 在 1s 无报文后就把 SOC 清 0, 无法区分"超时清零"与"SOC 真为 0",
         * 故本模块自维护 10s 判据: 1s~10s 之间保持最后一次有效 SOC
         * (单帧丢失不该让续航瞬间跳变), 超 10s 才按 0 处理 */
        timeout = ((now - pk->last_alive_tick)
                   > rt_tick_from_millisecond(REF_MILE_USE_TIMEOUT_MS));
        pk->soc_used = timeout ? 0
                      : (soc_raw > 0 ? soc_raw : (int32_t)pk->last_valid_soc);

        pk->count_mile_km += inc_km;
        if (s_stop_ms > REF_MILE_STOP_MS) {
            pk->had_long_stop = true;
        }

        if (timeout) {
            continue;   ///< 通信失效期间不结算、不改基准, 累加里程保留待恢复
        }

        if (pk->last_soc < 0) {
            pk->last_soc = pk->soc_used;                ///< 首次: 只建立基准
        } else if (pk->soc_used < pk->last_soc) {
            int32_t delta = pk->last_soc - pk->soc_used;

            if (delta <= REF_MILE_MAX_DROP_PCT) {
                /* 单 tick 掉多档 (SOC 上报慢于 tick) 时按速率折算, 只入队 1 个样本;
                 * 超过上限视为异常跳变(换包等), 不记录只重建基准 */
                double per_pct = pk->count_mile_km / (double)delta;

                if (!pk->had_long_stop) {
                    ref_mile_push(pk, pack + 1,
                                  ref_mile_clamp(pk, (uint16_t)(per_pct * 100.0 + 0.5)));
                }
            }

            pk->count_mile_km = 0.0;
            pk->had_long_stop = false;
            pk->last_soc = pk->soc_used;
        } else if (pk->soc_used > pk->last_soc) {
            /* SOC 上升: 充电或换电包热插拔, 不结算, 只重建基准 */
            pk->count_mile_km = 0.0;
            pk->had_long_stop = false;
            pk->last_soc = pk->soc_used;
        }
    }

    ref_mile_publish();
}

static void ref_mile_load(void)
{
    uint8_t arr[REF_MILE_ARR_NUM];
    uint8_t pack, i;
    uint16_t saved_km = 0;

    for (pack = 0; pack < REF_MILE_PACK_NUM; pack++) {
        ref_mile_pack_t *pk = &s_pack[pack];

        pk->fixed_pack      = (pack == REF_MILE_FIXED_PACK - 1);
        pk->last_soc        = -1;
        pk->soc_used        = 0;
        pk->last_valid_soc  = 0;
        pk->count_mile_km   = 0.0;
        pk->had_long_stop   = false;
        pk->last_alive_tick = rt_tick_get();

        if (!vehicle_get_ref_mile_arr(pack + 1, arr)) {
            /* 参数未就绪: 用默认 1.00km 兜底。
             * 注意不按钳位区间压缩, 否则默认值会被压到上限, 须等首个窗口才收敛 */
            for (i = 0; i < REF_MILE_ARR_NUM; i++) {
                arr[i] = REF_MILE_ARR_DEFAULT_CK;
            }
        }

        pk->sum = 0;
        for (i = 0; i < REF_MILE_ARR_NUM; i++) {
            pk->arr[i] = arr[i];
            pk->sum += arr[i];
        }
    }

    /* 开机初值: 先用上次落盘的续航顶上(UI 与 0x453 都取这里), 并广播一次到中间层。
     * 上一熄火周期若续航不可用(存 0)则无初值, 维持原来的 "--km" 行为 */
    if (vehicle_get_mile_range(&saved_km) && saved_km > 0) {
        s_total_x10      = (uint32_t)saved_km * 10;
        s_all_low        = false;
        s_boot_hold      = true;
        s_boot_hold_tick = rt_tick_get();
        s_state          = true;   ///< 触发 UI 立即显示初值
        vehicle_set_mile_endurance(saved_km);
    }

    s_loaded = true;
}

static void ref_mile_task(void *param)
{
    rt_tick_t last_wake = rt_tick_get();
    int wait_cnt;

    (void)param;

    /* 等参考里程数组从 JSON 恢复完成, 避免以默认值起算并覆写已学习的历史数组。
     * 最多等 5s, 超时降级用默认值(JSON 故障时不阻塞续航线程) */
    for (wait_cnt = 0; !vehicle_get_param_recovery() && wait_cnt < 50; wait_cnt++) {
        rt_thread_mdelay(100);
    }

    /* 开机初值取自 mile_param, 同样等里程参数恢复完成; 未恢复则无初值,
     * 不阻塞续航计算 (同样最多 5s) */
    for (wait_cnt = 0; !vehicle_get_mile_recovery() && wait_cnt < 50; wait_cnt++) {
        rt_thread_mdelay(100);
    }

    ref_mile_load();

    while (1) {
        /* delay_until 按整点基准推进 last_wake, 循环内不要再赋值,
         * 否则被抢占晚醒的延迟会累积, 使实际周期 >100ms 而高估里程 */
        rt_thread_delay_until(&last_wake, rt_tick_from_millisecond(REF_MILE_TICK_MS));
        ref_mile_tick();
    }
}

void ref_mileage_calc_init(void)
{
    rt_thread_t tid = rt_thread_create("ref_mile", ref_mile_task, RT_NULL,
                                       1024, 19, 10);

    if (tid != RT_NULL) {
        rt_thread_startup(tid);
    } else {
        rt_kprintf("Failed to create ref_mile thread!\n");
    }

    return;
}

bool get_ref_mileage_state(void)
{
    return s_state;
}

bool ref_mileage_ready(void)
{
    return s_loaded;
}

void set_ref_mileage_state(bool state)
{
    s_state = state;
}

uint32_t ref_mileage_total_x10(void)
{
    return s_total_x10;
}

bool ref_mileage_display_valid(void)
{
    return !s_all_low;
}
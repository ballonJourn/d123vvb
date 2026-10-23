#include <rtthread.h>
#include <stdbool.h>
#include "can_key.h"
#include "key_common.h"
#include "vehicle_param.h"
#include "dev_config.h"
#include "app_log.h"

#if MW_CAN_IO_KEY_ENABLE

///< 扫描周期, 单位 ms。0x501 发送周期为 200ms, 取半个周期即可覆盖每一帧
#define CAN_KEY_SCAN_PERIOD_MS (50)

///< 长按判定时长, 单位 ms (rocker_left 专用)。受扫描周期限制, 实际在 1000~1100ms 之间触发
#define CAN_KEY_LONG_PRESS_MS  (1000)

///< 0x501 Roller_Dire@byte7 bit0-1 取值, 见 mw_can.c::parse_can_msg_501_msg
#define CAN_ROLLER_DOWN (1)           // 下滑
#define CAN_ROLLER_UP   (2)           // 上滑

///< 滚轮保持转动多久后转为"周期重复"(即连续滚动), 单位 ms
#define CAN_ROLLER_REPEAT_DELAY_MS  (400)

///< 连续滚动时相邻两次事件的间隔, 单位 ms。
///< 受 CAN_KEY_SCAN_PERIOD_MS 量化, 实际约 200ms 一次
#define CAN_ROLLER_REPEAT_PERIOD_MS (150)

/**
 * @brief 按键种类枚举
 *
 * 取值与 gpio_key.h / adc_key.h 各自的 key_press_e 一致, 但此处自包含一份:
 * 那两个头文件的枚举量同名, 且 CAN 按键与面板 IO 按键允许同时使能,
 * 若放进本头文件, mw_init.c 会同时看到两套同名枚举量而重定义。
 * 本枚举仅为"CAN 信号 -> key_common 事件"的中间量, 外部无使用者。
 */
typedef enum {
    NO_KEY_PRESS = 0,
    UP_KEY_PRESS = 1,
    DOWN_KEY_PRESS = 2,
    ENTER_KEY_PRESS = 3,
    BACK_KEY_PRESS = 4,
    HOME_KEY_PRESS = 5,     // 滚轮按键 VEH_BTN_WHEEL
} can_key_press_e;

///< 按压时长档位, 与 gpio_key.h 的 key_press_mode_e 同义, 同样收在本文件内
typedef enum {
    SHORT_PRESS = 0,
    LONG_PRESS  = 1,
} can_key_press_mode_e;

///< 一个 1bit 触发位的扫描状态
typedef struct {
    uint8_t   value;       ///< 上一拍的值
    rt_tick_t press_tick;  ///< 按下沿时刻, 用于长按计时
    bool      long_sent;   ///< 长按事件是否已发出
} can_key_state_t;

static can_key_state_t rocker_left_state  = {0, 0, false};
static can_key_state_t rocker_right_state = {0, 0, false};
static can_key_state_t wheel_btn_state    = {0, 0, false};

///< 滚轮转动状态的扫描状态。
///< 方向是"转向量": 持续转动期间保持 1/2 不变, 手停后才回 0, 故需自行计时产生重复事件
typedef struct {
    uint8_t   value;       ///< 上一拍的方向值 0/1/2
    rt_tick_t first_tick;  ///< 进入当前方向的时刻, 用于起始延迟计时
    rt_tick_t last_tick;   ///< 上一次发出事件的时刻, 用于重复周期计时
} can_roller_state_t;

static can_roller_state_t roller_state = {0, 0, 0};

///< 键码 -> 按键事件, 映射遵循 gpio_key.c::check_key_status / adc_key.c::adc_send_event
static void can_key_send(can_key_press_e key_code, can_key_press_mode_e mode) {
    const char *name;
    uint8_t key_event;

    switch (key_code) {
        case UP_KEY_PRESS:
            key_event = (mode == LONG_PRESS) ? UP_KEY_LONG_PR : UP_KEY_SHORT_PR;
            name = "up";
            break;

        case DOWN_KEY_PRESS:
            key_event = (mode == LONG_PRESS) ? MODE_KEY_LONG_PR : MODE_KEY_SHORT_PR;
            name = "down";
            break;

        case ENTER_KEY_PRESS:
            key_event = (mode == LONG_PRESS) ? SET_KEY_LONG_PR : SET_KEY_SHORT_PR;
            name = "enter";
            break;

        case BACK_KEY_PRESS:
            key_event = (mode == LONG_PRESS) ? BACK_KEY_LONG_PR : BACK_KEY_SHORT_PR;
            name = "back";
            break;

        case HOME_KEY_PRESS:
            key_event = HOME_KEY_SHORT_PR;   ///< 无对应的长按事件, 固定短按
            name = "home";
            break;

        default:
            return;
    }

    // rt_kprintf("can key: %s key %s press\n", name,
    //            (mode == LONG_PRESS) ? "long" : "short");
    send_key_event(key_event);
}

/**
 * @brief 处理一个 1bit 触发位
 *
 * 短按在"松开时"判定(照 gpio_key.c::scan_gpio_key 的约定):
 *   按下沿只记时刻不定档, 松开时若未触发长按才补发短按, 使一次长按只产生一个事件。
 *
 * @param st         该键的扫描状态
 * @param value      本拍的值
 * @param key_code   键码
 * @param allow_long 是否支持长按(当前仅 rocker_left)
 */
static void scan_trigger_key(can_key_state_t *st, uint8_t value,
                             can_key_press_e key_code, bool allow_long) {
    if (value && !st->value) {
        ///< 按下沿: 只记时刻, 短按留到松开时判定
        st->press_tick = rt_tick_get();
        st->long_sent  = false;
    } else if (value && st->value) {
        ///< 持续按下: 到时发一次长按
        if (allow_long && !st->long_sent &&
            ((rt_tick_get() - st->press_tick) >=
             rt_tick_from_millisecond(CAN_KEY_LONG_PRESS_MS))) {
            st->long_sent = true;
            can_key_send(key_code, LONG_PRESS);
        }
    } else if (!value && st->value) {
        ///< 松开: 未触发长按才补发短按
        if (!st->long_sent) {
            can_key_send(key_code, SHORT_PRESS);
        }
    }

    st->value = value;
}

/**
 * @brief 处理滚轮转动, 支持连续滚动
 *
 * Roller_Dire 是 2bit 值, 属"转向量"而非按下位: 持续转动期间保持 1/2 不变, 手停后才回 0。
 * 若只做边沿检测, 一直转也只出一次事件, 故此处:
 *   方向变化(0->1/2、1<->2 反转)时立即出一次事件, 并重新开始计时;
 *   方向保持超过 CAN_ROLLER_REPEAT_DELAY_MS 后, 每 CAN_ROLLER_REPEAT_PERIOD_MS 再出一次,
 *   形成连续滚动。
 *
 * @param roller_dire 本拍的方向值 0:空闲 1:下滑 2:上滑 3:未定义
 */
static void scan_can_roller(uint8_t roller_dire) {
    can_key_press_e key_code;

    if (roller_dire > CAN_ROLLER_UP) {
        ///< 3: 未定义, 忽略且不改变状态, 避免总线抖动被计成一次转动
        return;
    }

    if (roller_dire == 0) {
        ///< 空闲: 复位, 使下一次转动重新走"立即触发"
        roller_state.value = 0;
        return;
    }

    key_code = (roller_dire == CAN_ROLLER_DOWN) ? DOWN_KEY_PRESS : UP_KEY_PRESS;

    if (roller_dire != roller_state.value) {
        ///< 方向边沿: 立即出一次, 并重新开始计时
        roller_state.first_tick = rt_tick_get();
        roller_state.last_tick  = roller_state.first_tick;
        can_key_send(key_code, SHORT_PRESS);
    } else if (((rt_tick_get() - roller_state.first_tick) >=
                rt_tick_from_millisecond(CAN_ROLLER_REPEAT_DELAY_MS)) &&
               ((rt_tick_get() - roller_state.last_tick) >=
                rt_tick_from_millisecond(CAN_ROLLER_REPEAT_PERIOD_MS))) {
        ///< 方向保持: 到时周期补发, 形成连续滚动
        roller_state.last_tick = rt_tick_get();
        can_key_send(key_code, SHORT_PRESS);
    }

    roller_state.value = roller_dire;
}

static void scan_can_key(void) {
    uint8_t rocker_left  = (uint8_t)vehicle_get_data(VEH_ROCKER_LEFT);
    uint8_t rocker_right = (uint8_t)vehicle_get_data(VEH_ROCKER_RIGHT);
    uint8_t wheel_btn    = (uint8_t)vehicle_get_data(VEH_BTN_WHEEL);
    uint8_t roller_dire  = (uint8_t)vehicle_get_data(VEH_ROLLER_DIRE);

    // scan_trigger_key(&rocker_left_state,  rocker_left,  BACK_KEY_PRESS,  true);
    // scan_trigger_key(&rocker_right_state, rocker_right, ENTER_KEY_PRESS, false);
    // scan_trigger_key(&wheel_btn_state,    wheel_btn,    HOME_KEY_PRESS,  false);

    if(rocker_left)
    {
        can_key_send(BACK_KEY_PRESS, SHORT_PRESS);
        vehicle_set_data(VEH_ROCKER_LEFT, 0);
    }
    if(rocker_right)
    {
        can_key_send(ENTER_KEY_PRESS, SHORT_PRESS);
        vehicle_set_data(VEH_ROCKER_RIGHT, 0);
    }
    if(wheel_btn)
    {
        can_key_send(HOME_KEY_PRESS, SHORT_PRESS);
        vehicle_set_data(VEH_BTN_WHEEL, 0);
    }

    ///< 滚轮转动(含连续滚动), 见 scan_can_roller
    scan_can_roller(roller_dire);
}

static void can_key_thread(void *param) {
    for (;;) {
        scan_can_key();
        rt_thread_mdelay(CAN_KEY_SCAN_PERIOD_MS);
    }
}

int can_key_init(void) {
    rt_thread_t tid;

    tid = rt_thread_create("can_key",
                           can_key_thread,
                           RT_NULL,
                           2048,          // 堆栈大小，可根据编译情况调整
                           20,            // 优先级，数值越小优先级越高
                           10);           // 时间片
    if (tid != RT_NULL) {
        rt_thread_startup(tid);
    } else {
        rt_log_error("Create can_key_thread failed.\n");
        return -1;
    }

    rt_log_info("can key init ok!\n");

    return 0;
}

#endif
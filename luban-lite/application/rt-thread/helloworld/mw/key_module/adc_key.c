#include <rtthread.h>
#include <string.h>
#include "rtdevice.h"
#include "app_log.h"
#include "adc_key.h"
#include "dev_state.h"
#include "key_common.h"
#include "aic_core.h"
#include "hal_rtp.h"
#include <drivers/touch.h>

#if MW_ADC_KEY_ENABLE

#define UP_KEY_AD       (3340)
#define DOWN_KEY_AD     (2240)
#define ENTER_KEY_AD    (1351)
#define BACK_KEY_AD     (497)

#define KEYAD_NUM	           (4)
#define LONG_PRESS_MS          (2500)  ///< 长按时间，单位ms
#define SUPER_LONG_PRESS_MS    (8000)  ///< 超长按时间，单位ms（仅上键）
#define ADC_SAMPLE_PERIOD_MS   (10)
#define ADC_SAMPLE_COUNT       (5)     ///< 采集次数，总消抖时间约50ms
#define ADC_RELEASE_THRESHOLD  (4000)  ///< 大于此值认为释放

#define KEYAD_RANGE_VALUE      (120)

static uint16_t keyadc_value[KEYAD_NUM] = {
    UP_KEY_AD,
    DOWN_KEY_AD,
    ENTER_KEY_AD,
    BACK_KEY_AD
};

static uint8_t key_map[KEYAD_NUM] = {
    UP_KEY_PRESS,
    DOWN_KEY_PRESS,
    ENTER_KEY_PRESS,
    BACK_KEY_PRESS,
};

static rt_device_t g_adc_dev = RT_NULL;
static rt_thread_t g_adc_key_tid = RT_NULL;

static key_state_t s_key_state = KEY_STATE_IDLE;
static int8_t s_pressed_key_idx = -1;       /* 当前按下的按键索引 */
static rt_tick_t s_press_start_tick = 0;    /* 按键按下开始时刻 */
static bool s_long_sent = false;    /* 长按事件是否已发送 */
static bool s_super_long_sent = false;  /* 超长按事件是否已发送 */

static uint16_t s_adc_sample_buf[ADC_SAMPLE_COUNT] = {0};
static uint8_t s_sample_cnt = 0;

static uint16_t adc_read_raw(void) {
    if (!g_adc_dev) {
        return 0;
    }

    struct aic_rtp_adc_info adc_info = {0};
    adc_info.ch = MW_ADC_KEY_CHANNEL;
#ifdef AIC_RTP_DRV
    rt_device_control(g_adc_dev, RT_TOUCH_CTRL_GET_ADC, (void *)&adc_info);
#endif
    return adc_info.data;
}

static void push_key_to_avg_pool(uint16_t ad_value) {
	if (s_sample_cnt < ADC_SAMPLE_COUNT) {
		s_adc_sample_buf[s_sample_cnt] = ad_value;
		s_sample_cnt++;
	}
}

static bool avg_pool_is_full(void){
	return s_sample_cnt == ADC_SAMPLE_COUNT;
}

static void reset_key_avg_pool(void) {
	s_sample_cnt = 0;
}

static uint16_t get_key_avg_value(void){
	uint16_t buf[ADC_SAMPLE_COUNT];
    for (int i = 0; i < s_sample_cnt; i++) {
        buf[i] = s_adc_sample_buf[i];
    }

    ///< 冒泡排序（升序）
    for (int i = 0; i < s_sample_cnt - 1; i++) {
        for (int j = i + 1; j < s_sample_cnt; j++) {
            if (buf[i] > buf[j]) {
                uint16_t tmp = buf[i];
                buf[i] = buf[j];
                buf[j] = tmp;
            }
        }
    }

    ///< 取中位数（偶数个时取中间两个的平均值）
    if (s_sample_cnt % 2 == 1) {
        return buf[s_sample_cnt / 2];
    } else {
        return (buf[s_sample_cnt / 2 - 1] + buf[s_sample_cnt / 2]) / 2;
    }
}

static int8_t check_key(uint16_t sample_value) {
    if (sample_value > ADC_RELEASE_THRESHOLD) {
        return -1;
    }

    int8_t i = -1;

	for (i = 0; i < (sizeof(keyadc_value) / sizeof(keyadc_value[0])); i++ ) {
		if((sample_value >= keyadc_value[i] - KEYAD_RANGE_VALUE)
        && (sample_value <= keyadc_value[i] + KEYAD_RANGE_VALUE)) {
			return i;
		}
	}

	return -1;
}

/**
 * @brief 发送按键事件（短按或长按）
 */
static void adc_send_event(int8_t idx, bool is_long) {
    if (idx < 0 || idx >= KEYAD_NUM) return;
    uint8_t key_code = key_map[idx];
    uint8_t key_event = 0;

    switch (key_code) {
        case UP_KEY_PRESS:
            key_event = is_long ? UP_KEY_LONG_PR : UP_KEY_SHORT_PR;
            rt_kprintf("%s\n", is_long ? "up key long press" : "up key short press");
            send_key_event(key_event);
            break;

        case DOWN_KEY_PRESS:
            key_event = is_long ? MODE_KEY_LONG_PR : MODE_KEY_SHORT_PR;
            rt_kprintf("%s\n", is_long ? "down key long press" : "down key short press");
            send_key_event(key_event);
            break;

        case ENTER_KEY_PRESS:
            key_event = is_long ? SET_KEY_LONG_PR : SET_KEY_SHORT_PR;
            rt_kprintf("%s\n", is_long ? "enter key long press" : "enter key short press");
            send_key_event(key_event);
            break;

        case BACK_KEY_PRESS:
            key_event = is_long ? BACK_KEY_LONG_PR : BACK_KEY_SHORT_PR;
            rt_kprintf("%s\n", is_long ? "back key long press" : "back key short press");
            send_key_event(key_event);
            break;

        default:
            break;

    }
}

/**
 * @brief 处理一次平均后的按键判决结果
 * @param cur_key 当前判决的按键索引（-1表示无按键）
 */
static void process_key_decision(int8_t cur_key) {
    rt_tick_t now_tick = rt_tick_get();

    switch (s_key_state) {
    case KEY_STATE_IDLE:
        if (cur_key != -1) {
            ///< 检测到有效按键，进入按下状态
            s_key_state = KEY_STATE_PRESSED;
            s_pressed_key_idx = cur_key;
            s_press_start_tick = now_tick;
            s_long_sent = false;
            s_super_long_sent = false;
        }
        break;

    case KEY_STATE_PRESSED:
        if (cur_key != s_pressed_key_idx) {
            ///< 按键已释放或变为其他按键：发送短按（如果未长按）
            if (!s_long_sent) {
                adc_send_event(s_pressed_key_idx, false);
            }
            s_key_state = KEY_STATE_IDLE;
            s_pressed_key_idx = -1;
            s_long_sent = false;
            s_super_long_sent = false;
        } else {
            ///< 按键仍按下，检查长按
            if (!s_long_sent) {
                uint32_t duration_ms = (now_tick - s_press_start_tick) * 1000 / RT_TICK_PER_SECOND;
                if (duration_ms >= LONG_PRESS_MS) {
                    adc_send_event(s_pressed_key_idx, true);
                    s_long_sent = true;
                    s_key_state = KEY_STATE_LONG_PRESSED;
                }
            }
        }
        break;

    case KEY_STATE_LONG_PRESSED:
        if (cur_key != s_pressed_key_idx) {
            s_key_state = KEY_STATE_IDLE;
            s_pressed_key_idx = -1;
            s_long_sent = false;
            s_super_long_sent = false;
        } else {
            ///< 上键继续按住，检查超长按
            if (key_map[s_pressed_key_idx] == UP_KEY_PRESS && !s_super_long_sent) {
                uint32_t duration_ms = (now_tick - s_press_start_tick) * 1000 / RT_TICK_PER_SECOND;
                if (duration_ms >= SUPER_LONG_PRESS_MS) {
                    key_event_e evt = UP_KEY_SUPER_LONG_PR;
                    rt_kprintf("up key super long press\n");
                    send_key_event(evt);
                    s_super_long_sent = true;
                    s_key_state = KEY_STATE_SUPER_LONG_PRESSED;
                }
            }
        }
        break;

    case KEY_STATE_SUPER_LONG_PRESSED:
        if (cur_key != s_pressed_key_idx) {
            s_key_state = KEY_STATE_IDLE;
            s_pressed_key_idx = -1;
            s_long_sent = false;
            s_super_long_sent = false;
        }
        break;

    default:
        break;
    }
}

static void adc_key_scan(void) {
    if (get_check_self_state() < CHECK_SELF_STATE_SUCCESS) {
        return;
    }

    uint16_t raw = adc_read_raw();
    push_key_to_avg_pool(raw);
    if (avg_pool_is_full()) {
        uint16_t avg_ad_value = get_key_avg_value();
        int8_t key_code = check_key(avg_ad_value);
        process_key_decision(key_code);
        reset_key_avg_pool();
    }
}

static void adc_key_thread(void * parameter) {
    g_adc_dev = rt_device_find(AIC_RTP_NAME);
    if (!g_adc_dev) {
        rt_log_error("Failed to find %s device\n", AIC_RTP_NAME);
        return;
    }

    rt_device_control(g_adc_dev, RT_TOUCH_CTRL_ENABLE_INT, RT_NULL);
    for (;;) {
        rt_thread_mdelay(ADC_SAMPLE_PERIOD_MS);
        adc_key_scan();
    }
}

int adc_key_init(void) {
    g_adc_key_tid = rt_thread_create("adc_key_thread", adc_key_thread,
                            RT_NULL, 2048, 14, 10);
    if (g_adc_key_tid != RT_NULL) {
        rt_thread_startup(g_adc_key_tid);
    } else {
        rt_kprintf("create adc key thread failed!\n");
        return -1;
    }

    rt_log_info("adc key init success!\n");

    return 0;
}

/* 休眠前清理:
 *   1. 删除采集线程 (循环里只有 rt_thread_mdelay, 无持锁, delete 安全)
 *   2. 关 ADC 中断, 释放 g_adc_dev 引用
 * 唤醒走 rt_hw_cpu_reset, 起来后 adc_key_init 会再开. */
void adc_key_sleep(void) {
    if (g_adc_key_tid != RT_NULL) {
        rt_thread_delete(g_adc_key_tid);
        g_adc_key_tid = RT_NULL;
    }

    if (g_adc_dev) {
#ifdef AIC_RTP_DRV
        rt_device_control(g_adc_dev, RT_TOUCH_CTRL_DISABLE_INT, RT_NULL);
#endif
        g_adc_dev = RT_NULL;
    }

    /* 复位状态机, 防止下次 init 残留 */
    s_key_state = KEY_STATE_IDLE;
    s_pressed_key_idx = -1;
    s_long_sent = false;
    s_super_long_sent = false;
    s_sample_cnt = 0;

    rt_log_info("adc key sleep: thread deleted, ADC int disabled\n");
}

#endif
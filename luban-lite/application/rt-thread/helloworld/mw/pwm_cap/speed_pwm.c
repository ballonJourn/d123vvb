#include <rtthread.h>
#include <rtdevice.h>
#include "aic_core.h"
#include "aic_hal_gpio.h"
#include "aic_time.h"
#include "app_log.h"
#include "app_log.h"
#include "dev_config.h"
#include "speed_pwm.h"

#if SPEED_PWM_ENABLE

#define SPEED_PWM_PIN "PC.6"

///< 全局变量（volatile 保证中断中可见）
static volatile uint32_t last_time_us = 0;   ///< 上一次边沿时刻（微秒）
static volatile uint32_t period_us = 0;      ///< 测量周期（微秒）
static volatile uint32_t freq_hz = 0;        ///< 计算频率（Hz）
static volatile rt_bool_t valid_flag = RT_FALSE;

static uint32_t speed_io_pin = 0;
static rt_sem_t sem_pwm_ready = RT_NULL;

static uint32_t period_sum = 0;
static uint8_t period_cnt = 0;
#define AVG_SAMPLES 8

static void pwm_irq_callback(void *args) {
    uint32_t now_us = aic_get_time_us();
    uint32_t diff_us = now_us - last_time_us;

    if (diff_us > 100 && diff_us < 5000) {
        period_sum += diff_us;
        period_cnt++;
        if (period_cnt >= AVG_SAMPLES) {
            uint32_t avg_period = period_sum / AVG_SAMPLES;
            freq_hz = 1000000 / avg_period;
            period_sum = 0;
            period_cnt = 0;
            rt_sem_release(sem_pwm_ready);
        }
    }
    last_time_us = now_us;
}

static void pwm_worker_thread(void *param) {
    while (1) {
        rt_sem_take(sem_pwm_ready, RT_WAITING_FOREVER);
        rt_base_t level;
        uint32_t freq;
        level = rt_hw_interrupt_disable();
        freq = freq_hz;
        rt_hw_interrupt_enable(level);
        rt_kprintf("PWM frequency: %d Hz\n", freq);
    }
}

int speed_pwm_init(void) {
    sem_pwm_ready = rt_sem_create("pwm_sem", 0, RT_IPC_FLAG_FIFO);
    if (sem_pwm_ready == RT_NULL) {
        rt_kprintf("Failed to create semaphore\n");
        return -1;
    }

    speed_io_pin = rt_pin_get(SPEED_PWM_PIN);
    if (speed_io_pin == 0) {
        return -1;
    }

    rt_pin_mode(speed_io_pin, PIN_MODE_INPUT);
    rt_pin_attach_irq(speed_io_pin, PIN_IRQ_MODE_RISING, pwm_irq_callback, RT_NULL);
    rt_pin_irq_enable(speed_io_pin, PIN_IRQ_ENABLE);

    rt_thread_t tid = rt_thread_create("pwm_worker", pwm_worker_thread, RT_NULL,
                                        1024, 25, 10);
    if (tid != RT_NULL)
        rt_thread_startup(tid);

    last_time_us = aic_get_time_us();

    return 0;
}

#endif
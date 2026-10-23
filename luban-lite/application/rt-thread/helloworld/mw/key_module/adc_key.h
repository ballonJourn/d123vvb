#ifndef __ADC_KEY_H__
#define __ADC_KEY_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "dev_config.h"

#if MW_ADC_KEY_ENABLE

typedef enum {
    KEY_STATE_IDLE,             /* 空闲 */
    KEY_STATE_PRESSED,          /* 按键已确认按下（未触发长按） */
    KEY_STATE_LONG_PRESSED,     /* 长按已触发 */
    KEY_STATE_SUPER_LONG_PRESSED, /* 超长按已触发（仅上键） */
} key_state_t;

/**
 * @brief 按键种类枚举
 */
typedef enum {
   NO_KEY_PRESS = 0,
   UP_KEY_PRESS = 1,
   DOWN_KEY_PRESS = 2,
   ENTER_KEY_PRESS = 3,
   BACK_KEY_PRESS = 4,
} key_press_e;

/**
 * @brief  adc按键初始化
 * @param  无
 * @return 0:成功 -1:失败
 */
int adc_key_init(void);

/**
 * @brief  休眠前停止 ADC 按键采集: 关中断 + 删除采集线程
 *         IGN_ON 唤醒走 rt_hw_cpu_reset, 重启后 adc_key_init 会重新拉起
 * @param  无
 * @return 无
 */
void adc_key_sleep(void);
#endif

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // __ADC_KEY_H__
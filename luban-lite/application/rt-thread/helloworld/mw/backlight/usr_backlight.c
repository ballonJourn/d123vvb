#include <stdlib.h>
#include "usr_hal_pwm.h"
#include "app_log.h"
#include "usr_backlight.h"

#if MW_BL_PW_ENABLE

#define LEVEL_1_DUTY_VALUE (1000000)
#define LEVEL_2_DUTY_VALUE (600000)
#define LEVEL_3_DUTY_VALUE (400000)
#define LEVEL_4_DUTY_VALUE (200000)
#define LEVEL_5_DUTY_VALUE (1)
#define BREATH_BACKLIGHT_PERIOD (80)

typedef struct {
    uint32_t pre_led_value;
    uint32_t cur_led_value;
    uint32_t target_led_value;
    uint32_t inc_led_value;
} breath_bl_param_t;

static uint8_t led_level = 0;
static uint8_t auto_led_level = 0;
static breath_bl_param_t auto_backlight;

static uint32_t get_dutu_cycle_value(uint8_t level) {
    uint32_t value = LEVEL_1_DUTY_VALUE;

    switch (level) {
        case 1:
            value = LEVEL_1_DUTY_VALUE;
            break;

        case 2:
        case 3:
            value = LEVEL_3_DUTY_VALUE;
            break;

        case 4:
        case 5:
            value = LEVEL_5_DUTY_VALUE;
            break;

        default:
            break;
    }

    return value;
}

void set_backlight_level(uint8_t level) {
    if (level != led_level) {
        if (level >= BACKLIGHT_LEVEL_1 && level <= BACKLIGHT_LEVEL_5) {
            uint32_t level_value = LEVEL_3_DUTY_VALUE;
            switch (level) {
                case 1:
                    level_value = LEVEL_1_DUTY_VALUE;
                    break;

                case 2:
                    level_value = LEVEL_2_DUTY_VALUE;
                    break;

                case 3:
                    level_value = LEVEL_3_DUTY_VALUE;
                    break;

                case 4:
                    level_value = LEVEL_4_DUTY_VALUE;
                    break;

                case 5:
                    level_value = LEVEL_5_DUTY_VALUE;
                    break;

                default:
                    break;
            }

            hal_pwm_config(LCD_PWM_CH, level_value, PWM_BACKLIGHT_PERION);
            hal_pwm_enable(LCD_PWM_CH);

            auto_backlight.target_led_value = level_value;
            auto_backlight.cur_led_value = level_value;
            rt_log_info("Set pwm value:%lu\n", level_value);
            led_level = level;
        }
    }
}

void init_auto_backlight(uint8_t level) {
    if (auto_led_level != level) {
        uint32_t level_value = get_dutu_cycle_value(level);
        hal_pwm_config(LCD_PWM_CH, level_value, PWM_BACKLIGHT_PERION);
        hal_pwm_enable(LCD_PWM_CH);

        auto_backlight.target_led_value = level_value;
        auto_backlight.cur_led_value = level_value;
        auto_led_level = level;
    }
}

void init_backlight_pwm(void) {
    hal_pwm_config(LCD_PWM_CH, PWM_BACKLIGHT_PERION, PWM_BACKLIGHT_PERION);
    hal_pwm_enable(LCD_PWM_CH);
    rt_log_info("init pwm value:1000000\r\n");
}

#endif

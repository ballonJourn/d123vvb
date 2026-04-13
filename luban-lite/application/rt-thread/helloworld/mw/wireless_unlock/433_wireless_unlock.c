#include <rtthread.h>
#include <rtdevice.h>
#include "aic_core.h"
#include "aic_hal_gpio.h"
#include "aic_time.h"
#include "app_log.h"
#include "433_wireless_unlock.h"

#if WIRELESS_UNLOCK_ENABLE

#define WIRELESS_PIN  "PD.1"

///< ook 1527编码，具体同步帧需要根据实际波形
#define SYNC_HIGH_MIN_US 1000
#define SYNC_HIGH_MAX_US 1400
#define SYNC_LOW_MIN_US  10000
#define SYNC_LOW_MAX_US  14000
#define SYNC_ERROR_MAX_US  33000

///< bit1 高电平时间为900us,低电平时间300us
#define BIT1_HIGH_MIN_US 850
#define BIT1_HIGH_MAX_US 950
#define BIT1_LOW_MIN_US  250
#define BIT1_LOW_MAX_US  350

///< bit0 高电平时间为300us,低电平时间900us
#define BIT0_HIGH_MIN_US 250
#define BIT0_HIGH_MAX_US 350
#define BIT0_LOW_MIN_US  850
#define BIT0_LOW_MAX_US  950

#define  WIRELESS_TYPE_1527   0
#define  WIRELESS_TYPE_TS3090 1

#define GET_DATA_IN_VALUE(pin) rt_pin_read(pin)

#define WIRELESS_TYPE WIRELESS_TYPE_TS3090

#if WIRELESS_TYPE == WIRELESS_TYPE_1527
typedef enum {
    WAIT_SYNC_HIGH,
    WAIT_SYNC_LOW,
    RECEIVING_DATA
} decode_state_t;

static decode_state_t state = WAIT_SYNC_HIGH;
static uint32_t last_tick = 0;          // 上一次边沿时刻（us）
static uint32_t data_buffer = 0;         // 24位数据缓冲区
static uint8_t bit_count = 0;            // 已接收位数
static uint8_t frame_count = 0;          // 连续有效帧计数
static uint32_t last_addr = 0;           // 上一帧地址
static uint8_t last_key = 0;             // 上一帧按键码
static void (*rx_callback)(uint32_t address, uint8_t key) = NULL;

static int decode_data_bit(uint32_t high_us, uint32_t low_us);
static void process_frame(uint32_t data);
static void reset_decoder(void);
static void ts3090_irq_callback(void *args);
#elif WIRELESS_TYPE == WIRELESS_TYPE_TS3090
static wireless_unlock_t unlock_ctx = {
    .recv_state = RECV_STATE_NONE,
    .check_flag = 0,
    .recv_ok = 0,
    .bit_count = 0,    
};

#endif

static uint32_t data_in_pin = 0;

#if WIRELESS_TYPE == WIRELESS_TYPE_1527
static int decode_data_bit(uint32_t high_us, uint32_t low_us) {
    if (high_us >= BIT1_HIGH_MIN_US && high_us <= BIT1_HIGH_MAX_US &&
        low_us  >= BIT1_LOW_MIN_US  && low_us  <= BIT1_LOW_MAX_US) {
        return 1;
    }
    if (high_us >= BIT0_HIGH_MIN_US && high_us <= BIT0_HIGH_MAX_US &&
        low_us  >= BIT0_LOW_MIN_US  && low_us  <= BIT0_LOW_MAX_US) {
        return 0;
    }
    return -1;
}

static void process_frame(uint32_t data) {
    uint32_t addr = (data >> 4) & 0xFFFFF;
    uint8_t  key  = data & 0x0F;

    if (frame_count == 0) {
        last_addr = addr;
        last_key  = key;
        frame_count = 1;
    } else if (frame_count == 1 && addr == last_addr && key == last_key) {
        if (rx_callback) rx_callback(addr, key);
        frame_count = 0;
    } else {
        frame_count = 0;
    }
}

static void reset_decoder(void){
    state = WAIT_SYNC_HIGH;
    bit_count = 0;
    data_buffer = 0;
}

#elif WIRELESS_TYPE == WIRELESS_TYPE_TS3090
static uint8_t ook_rx_1527(void) {
    if (GET_DATA_IN_VALUE(data_in_pin) == PIN_LOW) {
        unlock_ctx.last_tick = aic_get_time_us();
        while (GET_DATA_IN_VALUE(data_in_pin) == PIN_LOW) {
            if (aic_get_time_us() - unlock_ctx.last_tick > SYNC_ERROR_MAX_US) { 
                return 1;
            }
        }

        unlock_ctx.low_duration = aic_get_time_us() - unlock_ctx.last_tick;
        if (unlock_ctx.low_duration >= SYNC_LOW_MIN_US && unlock_ctx.low_duration <= SYNC_LOW_MAX_US) {
            unlock_ctx.level_state = START_LEVEL;
            unlock_ctx.recv_state = RECV_STATE_IDLE;
            unlock_ctx.bit_count = 0;
            unlock_ctx.data_index = 0;
        } else if (unlock_ctx.low_duration >= BIT1_LOW_MIN_US && unlock_ctx.low_duration <= BIT1_LOW_MAX_US) {
            unlock_ctx.level_state = HIGH_LEVEL;
        } else if (unlock_ctx.low_duration >= BIT0_LOW_MIN_US && unlock_ctx.low_duration <= BIT0_LOW_MAX_US) {
            unlock_ctx.level_state = LOW_LEVEL;
        } else {
            unlock_ctx.level_state = MISTAKE_LEVEL;
            unlock_ctx.recv_state = RECV_STATE_IDLE;
            unlock_ctx.bit_count = 0;
            unlock_ctx.data_index = 0;

            return 0;
        }

        switch (unlock_ctx.recv_state) {
            case RECV_STATE_IDLE: {
                    unlock_ctx.bit_count = 0;
                    unlock_ctx.data_value = 0;
                    unlock_ctx.recv_state = RECV_STATE_RECV;
                }
                break;
            case RECV_STATE_RECV: 
                unlock_ctx.data_value <<= 1;
                if (unlock_ctx.level_state != MISTAKE_LEVEL) {
                    if (unlock_ctx.level_state == HIGH_LEVEL) {
                        unlock_ctx.data_value |= 0x01;
                    } else if (unlock_ctx.level_state == LOW_LEVEL) {
                        unlock_ctx.data_value |= 0x00;
                    }
                } 
                unlock_ctx.bit_count++;
                if (unlock_ctx.bit_count == 8) {
                    unlock_ctx.recv_data[unlock_ctx.data_index] = unlock_ctx.data_value;
                    unlock_ctx.data_index++;
                    unlock_ctx.data_value = 0;
                } else if (unlock_ctx.bit_count == 16) {
                    unlock_ctx.recv_data[unlock_ctx.data_index] = unlock_ctx.data_value;
                    unlock_ctx.data_index++;
                    unlock_ctx.data_value = 0;
                } else if (unlock_ctx.bit_count == 24) {
                    unlock_ctx.recv_data[unlock_ctx.data_index] = unlock_ctx.data_value;
                    unlock_ctx.recv_state = RECV_STATE_IDLE;
                    unlock_ctx.data_index = 0;
                    unlock_ctx.bit_count = 0;
                    unlock_ctx.data_value = 0;
                }

                if (!unlock_ctx.recv_ok) {
                    break;
                }
            case RECV_STATE_CHECK:
                unlock_ctx.recv_ok = 0;
                if (unlock_ctx.check_flag != 1) {
                    unlock_ctx.recv_compare[0] = unlock_ctx.recv_data[0];
                    unlock_ctx.recv_compare[1] = unlock_ctx.recv_data[1];
                    unlock_ctx.recv_compare[2] = unlock_ctx.recv_data[2];
                    unlock_ctx.check_flag = 1;
                    break;
                } else {
                    if (unlock_ctx.recv_compare[0] == unlock_ctx.recv_data[0] &&
                        unlock_ctx.recv_compare[1] == unlock_ctx.recv_data[1] &&
                        unlock_ctx.recv_compare[2] == unlock_ctx.recv_data[2]) {
                        unlock_ctx.check_flag = 1;
                        unlock_ctx.recv_data[0] = 0;
                        unlock_ctx.recv_data[1] = 0;
                        unlock_ctx.recv_data[2] = 0;
                    } else {
                        unlock_ctx.recv_compare[0] = unlock_ctx.recv_data[0];
                        unlock_ctx.recv_compare[1] = unlock_ctx.recv_data[1];
                        unlock_ctx.recv_compare[2] = unlock_ctx.recv_data[2];
                        break;
                    }
                }
                break;
            default:
                break;
        }
    }

    return 0;
}

static void wireless_unlock_1527_ref(void) {
    if (ook_rx_1527() == 1) {
        unlock_ctx.recv_data[0] = 0;
        unlock_ctx.recv_data[1] = 0;
        unlock_ctx.recv_data[2] = 0;
        return;
    }

    ///< to do: 根据recv_data内容执行相应操作
}
#endif

static void ts3090_irq_callback(void *args) {
#if WIRELESS_TYPE == WIRELESS_TYPE_1527
    uint32_t now = aic_get_time_us();
    uint8_t level = rt_pin_read(data_in_pin); 
    uint32_t duration = now - last_tick;
    last_tick = now;

    switch (state) {
        case WAIT_SYNC_HIGH:
            if (level == PIN_HIGH && duration >= SYNC_HIGH_MIN_US 
                && duration <= SYNC_HIGH_MAX_US) {
                state = WAIT_SYNC_LOW;
            }
            break;

        case WAIT_SYNC_LOW:
            if (level == PIN_LOW && duration >= SYNC_LOW_MIN_US 
                && duration <= SYNC_LOW_MAX_US) {
                state = RECEIVING_DATA;
                bit_count = 0;
                data_buffer = 0;
            } else {
                state = WAIT_SYNC_HIGH;
            }
            break;

        case RECEIVING_DATA: {
            static uint32_t high_time = 0;
            static uint8_t wait_low = 0;

            if (!wait_low && level == PIN_HIGH) {
                high_time = duration;
                wait_low = 1;
            } else if (wait_low && level == PIN_LOW) {
                wait_low = 0;
                int bit = decode_data_bit(high_time, duration);
                if (bit >= 0) {
                    data_buffer = (data_buffer << 1) | bit;
                    bit_count++;
                    if (bit_count == 24) {
                        process_frame(data_buffer);
                        reset_decoder();
                    }
                } else {
                    reset_decoder();
                }
            }
            break;
        }
    }

#elif WIRELESS_TYPE == WIRELESS_TYPE_TS3090
    wireless_unlock_1527_ref();
#endif
}

int wireless_unlock_init(void) {
    data_in_pin = rt_pin_get(WIRELESS_PIN);
    if (data_in_pin == 0) {
        rt_log_error("get pin failed, pin name: %s\n", WIRELESS_PIN);
        return -1;
    }

    rt_pin_mode(data_in_pin, PIN_MODE_INPUT);
    rt_pin_attach_irq(data_in_pin, PIN_IRQ_MODE_RISING_FALLING, \
                        ts3090_irq_callback, RT_NULL);
    rt_pin_irq_enable(data_in_pin, PIN_IRQ_ENABLE);

    return 0;
}

#endif
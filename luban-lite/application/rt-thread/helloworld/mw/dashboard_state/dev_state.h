
#ifndef __DEV_STATE_H__
#define __DEV_STATE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
    CHECK_SELF_STATE_INIT = 0,
    CHECK_SELF_STATE_START,
    CHECK_SELF_STATE_SUCCESS,
} check_self_state_e;

typedef enum {
    ANIMATION_STATE_IDLE = 0,
    ANIMATION_STATE_RUNNING,
    ANIMATION_STATE_END    ,
} animation_state_e;

/**
 * @brief  设置自检状态
 * @param  state 自检状态
 * @return 无
 */
void set_check_self_state(check_self_state_e state);

/**
 * @brief  获取自检状态
 * @param  none
 * @return check_self_state_e 自检状态
 */
check_self_state_e get_check_self_state(void);

/**
 * @brief  获取开机动画状态
 * @param  none
 * @return animation_state_e 开机动画状态
 */
animation_state_e get_boot_animation_status(void);

/**
 * @brief  设置开机动画状态
 * @param  none
 * @return animation_state_e 开机动画状态
 */
void set_boot_animation_status(animation_state_e state);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // __DEV_STATE_H__
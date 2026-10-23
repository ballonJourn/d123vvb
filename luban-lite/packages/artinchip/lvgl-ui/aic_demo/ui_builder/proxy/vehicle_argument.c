
#include "vehicle_argument.h"
#include "vehicle_data.h"
#include "set_param.h"
#include "usr_backlight.h"
#include "dev_config.h"
#include "vehicle_param.h"   /* 中间层数据：VEH_CUR_DISPALY_MODE（仅 proxy 层可引用） */

///< false:参数未准备好 true:已准备好
bool vehicle_get_param_recovery()
{
#if !ON_PC_CACLE
    return get_recovery_usr_param() ;
#endif

    return true ;
}

///< 语言：存的是 language_option_e，0:English 1:中文 2:韩文 3:Singaporean 4:Thai。
///< 注意与 lang_type_t{AIC_LANG_CH=0,...} 前两项相反，转换须走 language_option_to_lang_type()
uint8_t vehicle_get_param_language()
{
    uint8_t value ;
#if !ON_PC_CACLE
    if (get_usr_param(USR_PARAM_LANGUAGE, &value) )
        return value ;
#endif
    return 0 ;

}

void vehicle_set_param_language(uint8_t value)
{
#if !ON_PC_CACLE
    set_usr_param(USR_PARAM_LANGUAGE, (void*)&value) ;
#endif
    return ;
}

///< 里程格式 0:公制 1：英制
uint8_t vehicle_get_param_unit()
{
    uint8_t value ;
#if !ON_PC_CACLE
    if (get_usr_param(USR_PARAM_MILE_FORMAT, &value) )
        return value ;
#endif
    return 0 ;

}

void vehicle_set_param_unit(uint8_t value)
{
#if !ON_PC_CACLE
    set_usr_param(USR_PARAM_MILE_FORMAT, (void*)&value) ;
#endif
    return ;
}

//亮度等级
uint8_t vehicle_get_param_brightness()
{
    uint8_t value ;
#if !ON_PC_CACLE
    if (get_usr_param(USR_PARAM_BRIGHTNESS_LEVEL, &value) )
        return value ;
#endif
    return 0 ;

}

void vehicle_set_param_brightness(uint8_t value)
{
#if !ON_PC_CACLE
    set_usr_param(USR_PARAM_BRIGHTNESS_LEVEL, (void*)&value) ;
    #if MW_BL_PW_ENABLE
    set_backlight_level(value);
    #endif
#endif

    return ;
}

bool vehicle_get_brightness_is_auto()
{
    uint8_t value ;
#if !ON_PC_CACLE
    if (get_usr_param(USR_PARAM_BRIGHTNESS_LEVEL, &value))
        return (value == VEHICLE_BRIGHTNESS_AUTO) ;
#endif
    return false ;
}

void vehicle_set_brightness_auto()
{
#if !ON_PC_CACLE
    uint8_t value = VEHICLE_BRIGHTNESS_AUTO ;
    /* 只写 param，不动背光：实际亮度由中间层光感在下一周期接管 */
    set_usr_param(USR_PARAM_BRIGHTNESS_LEVEL, (void*)&value) ;
#endif
    return ;
}

//主题
uint8_t vehicle_get_param_display()
{
    uint8_t value ;
#if !ON_PC_CACLE
    if (get_usr_param(USR_PARAM_THEME, &value) )
        return value ;
#endif

    return 0;
}

void vehicle_set_param_display(uint8_t value)
{
#if !ON_PC_CACLE
    set_usr_param(USR_PARAM_THEME, (void*)&value) ;
#endif
    return ;
}

//当前实际生效的显示模式
uint8_t vehicle_get_cur_display_mode()
{
    uint8_t value = VEHICLE_DISPLAY_MODE_DAY ;
#if !ON_PC_CACLE
    if (!get_usr_param(USR_PARAM_THEME, &value))
        return VEHICLE_DISPLAY_MODE_DAY ;

    if (value == VEHICLE_DISPLAY_MODE_AUTO)
        return (uint8_t)vehicle_get_data(VEH_CUR_DISPALY_MODE) ;
#endif
    return value ;
}

//蓝牙开关
uint8_t vehicle_get_param_bluetooth()
{
    uint8_t value ;
#if !ON_PC_CACLE
    if (get_usr_param(USR_PARAM_BT_SWITCH, &value) )
        return value ;
#endif
    return 0 ;

}

void vehicle_set_param_bluetooth(uint8_t value)
{
#if !ON_PC_CACLE
    set_usr_param(USR_PARAM_BT_SWITCH, (void*)&value) ;
#endif
    return ;
}

//小计里程清零方式  取值见 VEHICLE_RESET_BY_BUTTON / VEHICLE_RESET_AFTER_POWER_OFF
///< 取值必须与中间层一致 (不一致时编译报错, 避免两处定义漂移)
typedef char vehicle_reset_type_check[
    (VEHICLE_RESET_BY_BUTTON       == RESET_TYPE_BY_BUTTON &&
     VEHICLE_RESET_AFTER_POWER_OFF == RESET_TYPE_AFTER_POWER_OFF) ? 1 : -1];

uint8_t vehicle_get_param_reset_type()
{
    uint8_t value ;
#if !ON_PC_CACLE
    if (get_usr_param(USR_PARAM_RESET_TYPE, &value) )
        return value ;
#endif
    return VEHICLE_RESET_BY_BUTTON ;

}

void vehicle_set_param_reset_type(uint8_t value)
{
#if !ON_PC_CACLE
    set_usr_param(USR_PARAM_RESET_TYPE, (void*)&value) ;
#endif
    return ;
}

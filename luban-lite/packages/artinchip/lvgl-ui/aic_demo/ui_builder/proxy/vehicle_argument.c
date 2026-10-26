
#include "vehicle_argument.h"
#include "vehicle_data.h"
#include "set_param.h"
#include "usr_backlight.h"
#include "dev_config.h"

///< false:参数未准备好 true:已准备好
bool vehicle_get_param_recovery()
{
#if !ON_PC_CACLE
    return get_recovery_usr_param() ;
#endif

    return true ;
}

///< 语言 0:中文 1:英文,预留位
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

//主题
uint8_t vehicle_get_param_display()
{
    uint8_t value ;
#if !ON_PC_CACLE
    if (get_usr_param(USR_PARAM_THEME, &value) )
        return value ;
#endif
    return 0 ;

}

void vehicle_set_param_display(uint8_t value)
{
#if !ON_PC_CACLE
    set_usr_param(USR_PARAM_THEME, (void*)&value) ;
#endif
    return ;
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

#include "link_logic.h"
#include "link_view.h"
#include "set_param.h"
#include "vehicle_param.h"
#include "speed_view.h"
#include "vehicle_argument.h"
#include "vehicle_data.h"
#include "mileage_calc.h"
#include "dev_config.h"

#define REFRESH_INTERVAL_50_MS   (50)

static lv_timer_t * link_view_timer = NULL ;

static void timer_refresh_50_ms( lv_timer_t *info) ;

static int32_t current_speed         = 0 ;
static gear_e  gear          = GEAR_MAX ;
static int32_t electriacl    = 0 ;
static bool init_qr = false;

void link_init() 
{
    link_view_init() ;

    if (NULL == link_view_timer)
        link_view_timer  = lv_timer_create( timer_refresh_50_ms  , REFRESH_INTERVAL_50_MS ,  NULL) ;
    
    timer_refresh_50_ms(NULL);

    return  ;
}

#if MW_CARLINK_EC_ENABLE
extern const char *get_carlink_url(void);
#endif

static void timer_refresh_50_ms(lv_timer_t *info)
{
    (void)info ;

    int __state = vehicle_get_data(VEH_CARLINK_CONNECTED) ;
    phone_refresh_link_state(!!__state) ;

#if MW_CARLINK_EC_ENABLE
   if (!init_qr)
    {
        if (vehicle_get_data(VEH_CARLINK_URL_STATUS))
        {
            //to do
            lv_log("get_carlink_url = %s\n", get_carlink_url());
            phone_refresh_qr(get_carlink_url());
            init_qr = true ;
        }

    }
#endif

    int32_t _speed = speed_view_current();
    if (current_speed != _speed)
    {
        phone_refresh_speed(_speed) ;
        current_speed = _speed ;
    }

    int32_t _gear = vehicle_get_data_gear();
    if(gear != _gear)
    {
        phone_refresh_gear(_gear);
        gear = _gear ;
    }

    int32_t _electriacl = vehicle_get_data_remain_battary() ;
    if (_electriacl != electriacl)
    {
        phone_refresh_electrical(_electriacl);
        electriacl = _electriacl ;
        // printf("phone_refresh_electrical\n");
    }
    

    if(get_mileage_state())
    {
        phone_refresh_mileage();

        set_mileage_state(false);
    }

    return  ;
}


void on_phone_destory()
{
    current_speed         = 0 ;
    gear          = GEAR_MAX ;
    electriacl    = 0 ;
    init_qr = false;
    if (link_view_timer)
    {
        lv_timer_del(link_view_timer) ;
        link_view_timer = NULL ;
    }
    
}
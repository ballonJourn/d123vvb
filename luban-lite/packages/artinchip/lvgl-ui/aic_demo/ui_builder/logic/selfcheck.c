#include "selfcheck.h"
#include "ui_init.h"
#include "aic_ui.h"
#include "definition.h"
#include "dev_state.h"
#include "vehicle_data.h"
#include "home_view_interface.h"

#define SELF_CHECK_DURATION 2500 
#define SELF_CHECK_INTERVAL 50

typedef struct {
    lv_timer_t* timer_id;
    uint32_t start_tick ;
    uint16_t check_count;
    check_state_e state;

}checkself_manager_t ;

static checkself_manager_t manager = { 0 };

static int max_check_count = 30;
static double half_check_count;
// static double rpm_div;
static double speed_div;
static double elec_div;

static void handle_state_idle() {
  // TODO: Implement idle state logic if needed
}

static void handle_state_waiting() {

    #if !defined(AIC_STARTUP_UI_SHOW)
    if (get_boot_animation_status() != ANIMATION_STATE_END)
    {
        set_check_self_state(CHECK_SELF_STATE_START) ;
    }
    #else
    uint32_t elapsed = time_now_ms() - manager.start_tick;
    if (elapsed > 5000) ;
    #endif
    else
    {   //boot animation播放完毕
        manager.state      = CHECK_STATE_CHECKING;
        manager.start_tick = lv_tick_get();
        animation_init();
        printf("selfcheck start\n");
    }

    return ; 
}

static void handle_state_checking() {
    manager.check_count++;

    if (manager.check_count > max_check_count) {
        manager.state = CHECK_STATE_FINISHED;
        printf("selfcheck end elapsed time = %lu ms\n" , lv_tick_get() - manager.start_tick);
        return ;
    }

    int count = (manager.check_count > half_check_count)
                    ? (max_check_count - manager.check_count)
                    : manager.check_count;

    // int rpm_value   = (int)(count * rpm_div);
    int speed_value = (int)(count * speed_div);
    int elec_value  = (int)(count * elec_div );
    // printf(" handle_state_checking rpm_value = %d speed_value = %d\n" , rpm_value , speed_value);
    // home_refresh_rpm(rpm_value)  ;

    home_refresh_speed(speed_value > SPEED_MAX ? SPEED_MAX : speed_value);
    home_refresh_electrical(elec_value > ELECTRI_MAX ? ELECTRI_MAX : elec_value);

    return ;
}

static void handle_state_finished() {
    if (manager.timer_id) {
        lv_timer_delete(manager.timer_id);
        manager.timer_id = NULL;
    }
    
    home_refresh_signal_visible(false);   

    set_check_self_state(CHECK_SELF_STATE_SUCCESS) ;
    home_refresh_stop_state(true);

    manager.state       = CHECK_STATE_FINISHED ;


    return ;
}

static void on_handle_timer(lv_timer_t *info) {
    (void)info ;
    switch (manager.state) 
    {
        case CHECK_STATE_IDLE:
            handle_state_idle();
            break;
        case CHECK_STATE_WAITING:
            handle_state_waiting();
            break;
        case CHECK_STATE_CHECKING:
            handle_state_checking();
            break;
        case CHECK_STATE_FINISHED:
            handle_state_finished();
            break;
        default:
            LV_LOG_WARN("Unknown state in checkself manager");
            break;
    }

    return  ;
}


void selfcheck_init() {
    manager.state       = CHECK_STATE_WAITING;
    manager.start_tick  = lv_tick_get() ;
    manager.check_count = 0 ;
    max_check_count     = SELF_CHECK_DURATION / SELF_CHECK_INTERVAL ;
    half_check_count    = max_check_count / 2.0 ;

    // rpm_div             = (double)(RPM_MAX )   / half_check_count;
    speed_div           = (double)(SPEED_MAX) / half_check_count;
    elec_div            = (double)(ELECTRI_MAX) / half_check_count;

    home_refresh_signal_visible(true);
    home_refresh_speed(0);

    manager.timer_id    = lv_timer_create(on_handle_timer, SELF_CHECK_INTERVAL , NULL);
    if (manager.timer_id == NULL) {
        LV_LOG_ERROR("Failed to add timer for checkself manager \n");
        return;
    }
}

int checkself_get_state() {
    return manager.state ;
}

void checkself_set_state(check_state_e _state) {
    manager.state = _state ;
}

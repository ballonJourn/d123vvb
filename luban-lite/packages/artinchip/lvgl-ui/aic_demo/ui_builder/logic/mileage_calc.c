#include "mileage_calc.h"
#include "vehicle_mile.h"
#include "vehicle_data.h"
#include "ref_mileage_calc.h"
#include "logic.h"
#include "logic.h"
#include <rtthread.h>

bool refresh_mileage = false ;



#define SPEED_INTERVAL_MS 50
#define CALC_INTERVAL_MS  1050

#define MAX_SPEEDS         ((CALC_INTERVAL_MS / SPEED_INTERVAL_MS) | 1)
#define TRIP_MILEAGE_MAX   1000000.0f
#define ODO_MILEAGE_MAX    (999999009.0)
#define SAVE_ODO_THRESHOLD 100.0f

typedef struct {
  double odo_mileage;
  double trip_mileage;
  double odo_once    ;
  double speed_array[MAX_SPEEDS];
  int speed_count;
  double unsaved_odo_distance;
  bool is_read;

  int lastHundred  ; 
} MileageData;

static MileageData mileage_data = {0};


static double get_speed() {
  double speed = vehicle_get_data_speed();
  return (speed < 0.0) ? 0.0 : speed / 3.6;
}

static double calculate_distance() {
  double h = SPEED_INTERVAL_MS / 1000.0;
  double distance =
      mileage_data.speed_array[0] + mileage_data.speed_array[MAX_SPEEDS - 1];
  for (int i = 1; i < MAX_SPEEDS - 1; ++i) {
    distance += (i % 2 == 0) ? 2 * mileage_data.speed_array[i]
                             : 4 * mileage_data.speed_array[i];
  }
  mileage_data.speed_array[0] = mileage_data.speed_array[MAX_SPEEDS - 1];
  return (h / 3.0) * distance;
}

static void check_and_reset_trip(void) {
  if (mileage_data.trip_mileage > TRIP_MILEAGE_MAX) {
    mileage_data.trip_mileage = 0.0;
  }
  if (mileage_data.odo_mileage > ODO_MILEAGE_MAX) {
    mileage_data.odo_mileage = ODO_MILEAGE_MAX;
  }
}

/* 续航里程与 odo/trip 共用本文件的百米保存点: set_mile_param 每次调用就写一个
 * 完整槽位, 让 ref_mileage_calc 自己在续航变化时落盘会额外消耗 EEPROM 擦写寿命。
 * 显示失效(三包 SOC 全 ≤5%)时存 0, 与 view 层 "--km" 语义一致 */
static void save_range_mileage(void) {
  uint16_t km = 0 ;

  /* 续航尚未就绪(上电早期)时"无效"只是没算出来, 写 0 会覆盖掉上次的续航 */
  if (!ref_mileage_ready())
  {
    return ;
  }

  if (ref_mileage_display_valid())
  {
    km = (uint16_t)(ref_mileage_total_x10() / 10) ;   // 0.1km → km
  }

  vehicle_set_mile_range(km) ;

  return ;
}

static void save_unsaved_mileage(void) {

  int currentHundred = (int)(mileage_data.trip_mileage / 100 ) ;
  if(currentHundred != mileage_data.lastHundred)
  {
    //printf("saved = %.2f\n" ,mileage_data.trip_mileage / 1000);
    set_mileage_state(true) ;
    vehicle_set_mile_odo(mileage_data.odo_mileage);
    vehicle_set_mile_tripA(mileage_data.trip_mileage);
    save_range_mileage();
    mileage_data.lastHundred = currentHundred ;
  }

}

static void process_full_speed_array(void) {
  double distance = calculate_distance();
  mileage_data.odo_mileage += distance;
  mileage_data.trip_mileage += distance;
  mileage_data.speed_count = 1;
  mileage_data.unsaved_odo_distance += distance;
  mileage_data.odo_once     += distance ;
  check_and_reset_trip();

  // printf("trip_mileage = %.2f\n" , (float)(mileage_data.trip_mileage / 1000 ));
  save_unsaved_mileage();  //保存数据

} 

static void calc_task(void *param)
{
    printf("calc_task\n");
    (void)param;
    
    /* RT-Thread 获取当前 tick */
    rt_tick_t xLastWakeTime = rt_tick_get();

    /* 等待里程参数从 EEPROM 恢复完成, 避免基数为 0 时计算并覆写历史里程.
     * 最多等 5s, 超时降级使用当前值(EEPROM 故障时不阻塞里程线程) */
    for (int wait_cnt = 0; !vehicle_get_mile_recovery() && wait_cnt < 10; wait_cnt++) {
        rt_thread_mdelay(100);
    }

    /* 初始化里程数据 */
    mileage_data.odo_mileage  = vehicle_get_mile_odo();
    mileage_data.trip_mileage = vehicle_get_mile_tripA();
    mileage_data.is_read      = true;
    mileage_data.odo_once     = 0;
    mileage_data.lastHundred  = (int)(mileage_data.trip_mileage / 100);
    
    while (1) {
        /* delay_until 内部按整点基准推进 xLastWakeTime, 不要再次赋值, 否则
         * 被抢占晚醒的延迟会累积, 导致采样周期 >50ms 而积分按时长高估里程 */
        rt_thread_delay_until(&xLastWakeTime, rt_tick_from_millisecond(SPEED_INTERVAL_MS));

        /* 里程被外部改写(下电清零 / CAN 改写)时, 同步本线程的累加基数:
         * UI 侧的刷新受"主界面门控", 若此刻不在主界面, 旧 trip 会被当基数继续
         * 累加并在百米保存点落盘, 等于把清零结果冲掉 */
        if (vehicle_get_mile_changed()) {
            mileage_data.odo_mileage  = vehicle_get_mile_odo();
            mileage_data.trip_mileage = vehicle_get_mile_tripA();
            mileage_data.lastHundred  = (int)(mileage_data.trip_mileage / 100);
            set_mileage_state(true);   /* 交 LVGL 线程按新值刷新显示 */
        }

        double current_speed = get_speed();
        mileage_data.speed_array[mileage_data.speed_count++] = current_speed;
        
        if (mileage_data.speed_count >= MAX_SPEEDS) {
            process_full_speed_array();
        }
    }
}

void mileage_calc_init(void)
{
    /* RT-Thread 创建线程 */
    rt_thread_t tid = rt_thread_create(
        "odo_calc",
        calc_task,
        RT_NULL,
        1024,
        19,
        10
    );
    
    if (tid != RT_NULL) {
        rt_thread_startup(tid);
    } else {
        rt_kprintf("Failed to create calc_task thread!\n");
    }
    
    return;
}

void mileage_clear_trip() {
  mileage_data.trip_mileage = 0.0;
  mileage_data.lastHundred = 0 ;
  vehicle_set_mile_tripA(mileage_data.trip_mileage);
  global_refresh_mileage();

  return ;
}

void mileage_clear_odo() {

  mileage_data.odo_mileage  = 0.0;
  vehicle_set_mile_odo(mileage_data.odo_mileage);
  
  mileage_clear_trip();
  return ;
}


bool get_mileage_state()
{
  return refresh_mileage ;
}

void set_mileage_state(bool state)
{
  refresh_mileage = state ;
}

void on_mileage_changed()
{
  mileage_data.odo_mileage  = vehicle_get_mile_odo();
  mileage_data.trip_mileage = vehicle_get_mile_tripA();
  mileage_data.is_read      = true;
  mileage_data.lastHundred  = -1;

  check_and_reset_trip();
  save_unsaved_mileage();

  return ;
}
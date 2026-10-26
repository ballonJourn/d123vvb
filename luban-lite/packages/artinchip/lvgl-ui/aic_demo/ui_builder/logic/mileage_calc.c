#include "mileage_calc.h"
#include "vehicle_mile.h"
#include "vehicle_data.h"
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
  double speed = 100 ;//vehicle_get_data_speed();
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

static void save_unsaved_mileage(void) {

  int currentHundred = (int)(mileage_data.trip_mileage / 100 ) ;
  if(currentHundred != mileage_data.lastHundred)
  {
    //printf("saved = %.2f\n" ,mileage_data.trip_mileage / 1000);
    set_mileage_state(true) ;
    vehicle_save_odo_mileage();
    vehicle_save_mile_tripA(); 
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
  vehicle_set_mile_odo(mileage_data.odo_mileage);
  vehicle_set_mile_tripA(mileage_data.trip_mileage);

  // printf("trip_mileage = %.2f\n" , (float)(mileage_data.trip_mileage / 1000 ));
  save_unsaved_mileage();  //保存数据

} 

static void calc_task(void *param)
{
    printf("calc_task\n");
    (void)param;
    
    /* RT-Thread 获取当前 tick */
    rt_tick_t xLastWakeTime = rt_tick_get();
    
    /* 初始化里程数据 */
    mileage_data.odo_mileage  = vehicle_get_mile_odo();
    mileage_data.trip_mileage = vehicle_get_mile_tripA();
    mileage_data.is_read      = true;
    mileage_data.odo_once     = 0;
    mileage_data.lastHundred  = (int)(mileage_data.trip_mileage / 100);
    
    while (1) {
        rt_thread_delay_until(&xLastWakeTime, rt_tick_from_millisecond(SPEED_INTERVAL_MS));

        xLastWakeTime = rt_tick_get();

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
        RT_THREAD_PRIORITY_MAX - 2,
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
  vehicle_save_mile_tripA(); 

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
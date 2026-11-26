#include "tire_pressure.h"
#include "setting_menu.h"
#include "meter_view.h"
#include "template.h"
#include "aic_lang.h"
#include "vehicle_tpms.h"
#include "pop_tip_manager.h"
#include "mw_can.h"          /* pop_tip_code_e: POP_TIP_TPMS_INIT */

/* 胎压文本上限："9999kPa" + '\0' */
#define TPMS_TEXT_MAX       16
/* 指示层闪烁 / 数据刷新周期 */
#define TPMS_TICK_MS        500
/* 学习结果弹窗（成功/失败）自动收掉的时长：3s（tick 周期 500ms → 6 拍） */
#define TPMS_RESULT_TICKS   10
/* 学习态看门狗：70s（中间层自己的超时是 60s，留 10s 余量；tick 500ms → 140 拍） */
#define TPMS_LEARN_TICKS    140
/* 胎压数值字号（设计稿文本框高 32、字宽 13.4px/字符 → 24 号） */
#define TPMS_VALUE_FONT     24

/* 轮位。前轮走左前槽位、对应 setting_tire_pressure_left.png 那张红环 */
typedef enum {
    TPMS_WHEEL_FRONT = 0,
    TPMS_WHEEL_REAR,
    TPMS_WHEEL_NUM,
} tpms_wheel_e;

typedef enum {
    TPMS_VIEW_BROWSE = 0,   /* 浏览态：未匹配轮常显红环 */
    TPMS_VIEW_SELECT,       /* 选择态：选中轮闪烁，未选中(未匹配)轮常显 */
    TPMS_VIEW_LEARNING,     /* 学习态：pop_tip "胎压匹配中"，轮询结果 */
    TPMS_VIEW_RESULT,       /* 结果态：pop_tip 成功/失败提示，3s 或任意键收掉 */
} tpms_view_state_e;

/* 轮位 -> proxy slot */
static const uint8_t wheel_slot[TPMS_WHEEL_NUM] = {
    VEHICLE_TPMS_LEFT_FRONT,
    VEHICLE_TPMS_LEFT_REAR,
};

static tpms_view_state_e state = TPMS_VIEW_BROWSE;
static tpms_wheel_e focus_wheel = TPMS_WHEEL_FRONT;
static bool blink_on = true;                /* 选择态闪烁相位 */
static int result_ticks = 0;                /* 结果态已显示的拍数 */
static int learn_ticks = 0;                 /* 学习态已等待的拍数（看门狗用） */
static lv_timer_t *tick_timer = NULL;
/* 上次写进 label 的胎压值，值没变就不重写 label（INT32_MIN = 强制重填） */
static int32_t last_kpa[TPMS_WHEEL_NUM] = { INT32_MIN, INT32_MIN };
/* 上次写进 label 的胎温值，同上 */
static int32_t last_temp[TPMS_WHEEL_NUM] = { INT32_MIN, INT32_MIN };

/* 该轮是否已匹配（学到即为已匹配） */
static bool wheel_matched(tpms_wheel_e w)
{
    return vehicle_tpms_pair_get_state(wheel_slot[w]) == VEHICLE_TPMS_PAIR_SUCCESS;
}

/* 从 from 出发按 dir 方向找下一个未匹配的轮，找不到返回 -1。
 * 两轮都已匹配时无可学，选择态进不去。 */
static int next_unmatched_wheel(int from, int dir)
{
    for (int i = 1; i <= TPMS_WHEEL_NUM; i++) {
        int w = ((from + dir * i) % TPMS_WHEEL_NUM + TPMS_WHEEL_NUM) % TPMS_WHEEL_NUM;
        if (!wheel_matched((tpms_wheel_e)w))
            return w;
    }
    return -1;
}

static lv_obj_t *indicator_obj(int w)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    return (w == TPMS_WHEEL_FRONT) ? page->pop_ind_front_l3_tire_pressure
                                   : page->pop_ind_rear_l3_tire_pressure;
}

static lv_obj_t *value_obj(int w)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    return (w == TPMS_WHEEL_FRONT) ? page->pop_label_front_l3_tire_pressure
                                   : page->pop_label_rear_l3_tire_pressure;
}

static lv_obj_t *temp_obj(int w)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    return (w == TPMS_WHEEL_FRONT) ? page->pop_label_front_l3_tire_pressure_temp
                                   : page->pop_label_rear_l3_tire_pressure_temp;
}

/* 指示层显隐：已匹配的一律不显示；选择态下选中的闪烁、未选中的常显 */
static void tire_pressure_show_indicators(void)
{
    for (int w = 0; w < TPMS_WHEEL_NUM; w++) {
        lv_obj_t *ind = indicator_obj(w);
        bool show;

        if (!ind)
            continue;

        if (wheel_matched((tpms_wheel_e)w))
            show = false;
        else if (state == TPMS_VIEW_SELECT)
            show = (w == (int)focus_wheel) ? blink_on : true;
        else
            show = true;    /* 浏览/学习/结果态：未匹配即常显 */

        if (show)
            lv_obj_remove_flag(ind, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(ind, LV_OBJ_FLAG_HIDDEN);
    }
}

/* 胎压文本：有效值 "250kPa"，无有效数据（未学习/传感器丢失/开机30s无数据）显示 "--" */
static void tire_pressure_show_values(void)
{
    for (int w = 0; w < TPMS_WHEEL_NUM; w++) {
        lv_obj_t *lab = value_obj(w);
        char text[TPMS_TEXT_MAX];
        int32_t kpa;

        if (!lab)
            continue;

        kpa = vehicle_tpms_get_pressure(wheel_slot[w]);
        if (kpa == last_kpa[w])
            continue;               /* 值没变，不必重写 label */
        last_kpa[w] = kpa;

        if (kpa >= 0)
            lv_snprintf(text, sizeof(text), "%dkPa", (int)kpa);
        else
            lv_snprintf(text, sizeof(text), "--");
        lv_label_set_text(lab, text);
    }

    /* 胎温文本：压力下方一行，"-5°C"/"25°C"，无有效数据同 "--"
     * （°(U+00B0) 已补进 c_default_16 / c_thai_16，由 DroidSansFallback 段提供字形） */
    for (int w = 0; w < TPMS_WHEEL_NUM; w++) {
        lv_obj_t *lab = temp_obj(w);
        char text[TPMS_TEXT_MAX];
        int32_t temp;

        if (!lab)
            continue;

        temp = vehicle_tpms_get_temp(wheel_slot[w]);
        if (temp == last_temp[w])
            continue;
        last_temp[w] = temp;

        if (temp != -1)
            lv_snprintf(text, sizeof(text), "%d°C", (int)temp);
        else
            lv_snprintf(text, sizeof(text), "--");
        lv_label_set_text(lab, text);
    }
}

static void tire_pressure_refresh(void)
{
    setting_page_t *page = setting_page_get(&ui_manager);

    if (!page->pop_layer_l3_tire_pressure)
        return;

    tire_pressure_show_values();
    tire_pressure_show_indicators();
}

/* 500ms：选择态翻闪烁相位；学习态轮询结果（含看门狗）；结果态倒计时；
 * 每 tick 顺带刷新胎压值与红环 */
static void tire_pressure_tick(lv_timer_t *t)
{
    (void)t;

    if (state == TPMS_VIEW_LEARNING) {
        /* bt_tpms_pair_start() 是同步置位 pairState=1 的，所以只要不再返回
         * LEARNING 就说明已出结果（SUCCESS/TIMEOUT），或被外部停掉回到 IDLE；
         * 只有 SUCCESS 算成功，其余（超时/被取消）一律报失败 */
        uint8_t st = vehicle_tpms_pair_get_state(wheel_slot[focus_wheel]);
        if (st != VEHICLE_TPMS_PAIR_LEARNING) {
            /* show_hold 是直接换掉外部请求码，"匹配中"被结果弹窗替下。
             * 若此刻偏偏有 CAN 弹窗在展，结果会按优先级排队等它收尾 —— 那种情况下
             * 本页的 3s 倒计时照走，可能早于结果上屏（红环状态仍是准的，可据此判断） */
            pop_tip_show_hold(st == VEHICLE_TPMS_PAIR_SUCCESS ? POP_TIP_TPMS_SUCCESS
                                                             : POP_TIP_TPMS_FAILED);
            state = TPMS_VIEW_RESULT;
            result_ticks = 0;
        } else if (++learn_ticks >= TPMS_LEARN_TICKS) {
            /* 看门狗：中间层自己的 60s 超时没生效（PAIR_CMD 入队失败 / 队列未初始化
             * 时 pairState 会一直停在 1），不能把用户永久锁在"匹配中"这一态 ——
             * 全屏遮罩常驻、SET 无动作，唯一出路只剩 BACK。
             * 这里主动停一次学习并直接上"匹配失败"，不依赖中间层状态变化
             * （队列坏掉时 stop 也送不到，只能由 UI 自己收尾） */
            vehicle_tpms_pair_stop(wheel_slot[focus_wheel]);
            pop_tip_show_hold(POP_TIP_TPMS_FAILED);
            state = TPMS_VIEW_RESULT;
            result_ticks = 0;
        }
    } else if (state == TPMS_VIEW_SELECT) {
        blink_on = !blink_on;
    } else if (state == TPMS_VIEW_RESULT) {
        if (++result_ticks >= TPMS_RESULT_TICKS) {
            pop_tip_hide();
            state = TPMS_VIEW_BROWSE;
        }
    }

    tire_pressure_refresh();
}

void tire_pressure_init(void)
{
    setting_page_t *page = setting_page_get(&ui_manager);

    tire_pressure_build();
    if (!page->pop_layer_l3_tire_pressure)
        return;

    set_current_menu_level(MENU_LEVEL_3);
    tpl3_hide();   /* 隐 L2 meter view 模板层 */
    lv_obj_remove_flag(page->pop_layer_l3_tire_pressure, LV_OBJ_FLAG_HIDDEN);

    state = TPMS_VIEW_BROWSE;
    focus_wheel = TPMS_WHEEL_FRONT;
    blink_on = true;
    result_ticks = 0;
    learn_ticks = 0;
    last_kpa[0] = last_kpa[1] = INT32_MIN;
    last_temp[0] = last_temp[1] = INT32_MIN;

    if (!tick_timer)
        tick_timer = lv_timer_create(tire_pressure_tick, TPMS_TICK_MS, NULL);

    tire_pressure_relang();
}

void tire_pressure_relang(void)
{
    setting_page_t *page = setting_page_get(&ui_manager);

    if (page->pop_title_l3_tire_pressure) {
        lv_obj_set_style_text_font(page->pop_title_l3_tire_pressure,
                                   aic_get_multi_lang_font(18), LV_PART_MAIN);
        lv_label_set_text(page->pop_title_l3_tire_pressure, aic_get_translation(tire_pressure));
    }

    /* 胎压 label 是纯数字，各语言字形相同，但字体对象随语言切换（泰文走 c_thai_*），
     * 故按项目约定一并重设，并强制重填文本 */
    for (int w = 0; w < TPMS_WHEEL_NUM; w++) {
        lv_obj_t *lab = value_obj(w);
        if (lab)
            lv_obj_set_style_text_font(lab, aic_get_multi_lang_font(TPMS_VALUE_FONT), LV_PART_MAIN);

        lab = temp_obj(w);
        if (lab)
            lv_obj_set_style_text_font(lab, aic_get_multi_lang_font(16), LV_PART_MAIN);
    }
    last_kpa[0] = last_kpa[1] = INT32_MIN;
    last_temp[0] = last_temp[1] = INT32_MIN;

    tire_pressure_refresh();
}

void tire_pressure_destroy(void)
{
    setting_page_t *page = setting_page_get(&ui_manager);

    /* 学习/结果态都持有 pop_tip 的外部请求，退出时必须撤掉，
     * 否则 pop_tip 的遮罩会留在界面上（学习中还要顺手停掉学习） */
    if (state == TPMS_VIEW_LEARNING)
        vehicle_tpms_pair_stop(wheel_slot[focus_wheel]);
    if (state == TPMS_VIEW_LEARNING || state == TPMS_VIEW_RESULT)
        pop_tip_hide();

    if (tick_timer) {
        lv_timer_delete(tick_timer);
        tick_timer = NULL;
    }
    if (page->pop_layer_l3_tire_pressure)
        lv_obj_add_flag(page->pop_layer_l3_tire_pressure, LV_OBJ_FLAG_HIDDEN);

    state = TPMS_VIEW_BROWSE;
    focus_wheel = TPMS_WHEEL_FRONT;
    blink_on = true;
    result_ticks = 0;
    learn_ticks = 0;
    last_kpa[0] = last_kpa[1] = INT32_MIN;
    last_temp[0] = last_temp[1] = INT32_MIN;
}

/* ===== 按键处理（表驱动） ===== */

/* SET：浏览态进选择态（默认前轮）；选择态开始学习；学习态无动作 */
static void tire_pressure_deal_set(void)
{
    if (state == TPMS_VIEW_BROWSE) {
        /* 默认选中前轮；前轮已匹配则退而选后轮；两轮都已匹配则无可学 */
        int w = wheel_matched(TPMS_WHEEL_FRONT) ? TPMS_WHEEL_REAR : TPMS_WHEEL_FRONT;
        if (wheel_matched((tpms_wheel_e)w))
            return;
        focus_wheel = (tpms_wheel_e)w;
        state = TPMS_VIEW_SELECT;
        blink_on = true;
        tire_pressure_refresh();
    } else if (state == TPMS_VIEW_SELECT) {
        if (vehicle_tpms_pair_start(wheel_slot[focus_wheel]) == 0) {
            state = TPMS_VIEW_LEARNING;
            learn_ticks = 0;
            pop_tip_show_hold(POP_TIP_TPMS_INIT);
        }
    }
}

/* UP/DOWN：选择态在未匹配轮间切换 */
static void tire_pressure_deal_move(int dir)
{
    int w;

    if (state != TPMS_VIEW_SELECT)
        return;

    w = next_unmatched_wheel((int)focus_wheel, dir);
    if (w < 0 || w == (int)focus_wheel)
        return;

    focus_wheel = (tpms_wheel_e)w;
    blink_on = true;
    tire_pressure_refresh();
}

static void tire_pressure_deal_up(void)
{
    tire_pressure_deal_move(-1);
}

static void tire_pressure_deal_down(void)
{
    tire_pressure_deal_move(1);
}

/* BACK：学习态中断学习回浏览态；选择态回浏览态；浏览态退出 L3 回 L2 */
static void tire_pressure_deal_back(void)
{
    if (state == TPMS_VIEW_LEARNING) {
        vehicle_tpms_pair_stop(wheel_slot[focus_wheel]);
        pop_tip_hide();
        state = TPMS_VIEW_BROWSE;
        tire_pressure_refresh();
        return;
    }

    if (state == TPMS_VIEW_SELECT) {
        state = TPMS_VIEW_BROWSE;
        tire_pressure_refresh();
        return;
    }

    tire_pressure_destroy();
    meter_view_init();   /* 回 L2，重配 tpl3 为 meter view */
}

static short_click_deal short_click[] = {
    [CLICK_SHORT_UP]   = tire_pressure_deal_up,
    [CLICK_SHORT_DOWN] = tire_pressure_deal_down,
    [CLICK_SHORT_SET]  = tire_pressure_deal_set,
    [CLICK_SHORT_BACK] = tire_pressure_deal_back,
};

void on_tire_pressure_deal_short_key(click_e key)
{
    if (key >= sizeof(short_click) / sizeof(short_click_deal) || !short_click[key])
        return;

    /* 结果态：任意键都只做"收掉成功/失败提示并回浏览态"，不做其他动作 */
    if (state == TPMS_VIEW_RESULT) {
        pop_tip_hide();
        state = TPMS_VIEW_BROWSE;
        tire_pressure_refresh();
        return;
    }

    short_click[key]();
}
#include "view_faults.h"
#include "setting_menu.h"
#include "template.h"
#include "meter_view.h"

/* 故障信息文本上限："DTC: 0x" + 2 位十六进制 + '\0' */
#define FAULT_TEXT_MAX  16
/* 故障码刷新周期：与 tire_pressure 的 tick 一致 */
#define FAULT_TICK_MS   500

/* ===== 静态状态 ===== */
static lv_obj_t *fault_label[FAULT_ITEM_NUM];   /* 卡片右侧故障码 label，懒创建 */
/* 上次写进 label 的码值，值没变就不重写（INT32_MIN = 强制重填） */
static int32_t last_code[FAULT_ITEM_NUM];
static lv_timer_t *tick_timer = NULL;

/* 项显示名：BMS1/BMS2/BMS3/MCU 为器件缩写，按需求不翻译 */
static const char *item_name[FAULT_ITEM_NUM] = {
    "BMS1",
    "BMS2",
    "BMS3",
    "MCU",
};

/* 在卡片右侧建故障码 label：右对齐、距卡片右边框 30px，只建一次 */
static void fault_label_ensure(int slot)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    lv_obj_t *lab;

    if (slot < 0 || slot >= FAULT_ITEM_NUM || fault_label[slot] || !page->pop_tpl4_card[slot])
        return;

    lab = lv_label_create(page->pop_tpl4_card[slot]);
    lv_obj_align(lab, LV_ALIGN_RIGHT_MID, -30, 0);
    lv_obj_set_style_text_align(lab, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_font(lab, aic_get_multi_lang_font(16), LV_PART_MAIN);
    fault_label[slot] = lab;
}

/* 按当前故障码重填四张卡片：无故障绿字 "No Fault"，有故障红字 "DTC: 0xNN"；
 * 卡片高亮只表示"该卡有故障"（template 只有 n/p 两套图，借用 pressed 态） */
static void view_faults_refresh_codes(void)
{
    for (int s = 0; s < FAULT_ITEM_NUM; s++) {
        lv_obj_t *lab = fault_label[s];
        int32_t code;
        char text[FAULT_TEXT_MAX];

        if (!lab)
            continue;

        code = vehicle_get_fault_code((veh_fault_item_e)s);
        if (code == last_code[s])
            continue;
        last_code[s] = code;

        tpl4_set_card_pressed(s, code > 0);

        if (code > 0) {
            lv_snprintf(text, sizeof(text), "DTC: 0x%02X", (unsigned int)code);
            lv_obj_set_style_text_color(lab, lv_color_hex(0xfc0b0b), LV_PART_MAIN);
        } else {
            lv_snprintf(text, sizeof(text), "No Fault");
            lv_obj_set_style_text_color(lab, lv_color_hex(0x00ff55), LV_PART_MAIN);
        }
        lv_label_set_text(lab, text);
    }
}

static void view_faults_tick(lv_timer_t *t)
{
    (void)t;
    view_faults_refresh_codes();
}

void view_faults_init(void)
{
    set_current_menu_level(MENU_LEVEL_3);

    tpl4_activate(aic_get_translation(view_faults), NULL);   /* 标题居中由模板默认（无图标） */
    tpl4_set_label_mode(false, false);                 /* 左对齐 + 不显示 arrow */
    for (int s = 0; s < FAULT_ITEM_NUM; s++) {
        tpl4_set_slot(s, item_name[s], true);
        fault_label_ensure(s);
        last_code[s] = INT32_MIN;
    }

    view_faults_refresh_codes();

    if (!tick_timer)
        tick_timer = lv_timer_create(view_faults_tick, FAULT_TICK_MS, NULL);
}

/* 语言切换刷新（仅标题随语言变化，列表项为缩写）；
 * 故障码 label 字体对象随语言切换（泰文走 c_thai_*），按项目约定一并重设并强制重填 */
void view_faults_relang(void)
{
    tpl4_activate(aic_get_translation(view_faults), NULL);
    tpl4_set_label_mode(false, false);
    for (int s = 0; s < FAULT_ITEM_NUM; s++) {
        tpl4_set_slot(s, item_name[s], true);
        fault_label_ensure(s);
        if (fault_label[s])
            lv_obj_set_style_text_font(fault_label[s], aic_get_multi_lang_font(16), LV_PART_MAIN);
        last_code[s] = INT32_MIN;
    }
    view_faults_refresh_codes();
}

void view_faults_destroy(void)
{
    if (tick_timer) {
        lv_timer_delete(tick_timer);
        tick_timer = NULL;
    }
    for (int s = 0; s < FAULT_ITEM_NUM; s++) {
        if (fault_label[s]) {
            lv_obj_delete(fault_label[s]);
            fault_label[s] = NULL;
        }
        last_code[s] = INT32_MIN;
    }
}

/* ===== 按键处理：只读页，仅 back 回 L2 ===== */
static void view_faults_deal_back(void)
{
    tpl4_hide();
    view_faults_destroy();
    meter_view_init();   /* 回 L2，重配 tpl3 为 meter view */
}

static short_click_deal short_click[] = {
    [CLICK_SHORT_BACK] = view_faults_deal_back,
};

void on_view_faults_deal_short_key(click_e key)
{
    if (key < sizeof(short_click) / sizeof(short_click_deal) && short_click[key])
        short_click[key]();
}

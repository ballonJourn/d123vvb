#include "view_faults.h"
#include "setting_menu.h"
#include "template.h"
#include "meter_view.h"

/* 故障信息文本上限："DTC: 0x" + 2 位十六进制 + '\0' */
#define FAULT_TEXT_MAX  16

/* ===== 静态状态 ===== */
static fault_item_e focus_slot = FAULT_BMS1;
static bool detail_view = false;       /* true: 详情态（四卡隐、标题=器件名、显故障 label） */
static lv_obj_t *fault_label = NULL;   /* 详情态 label，首次进详情时创建，destroy 时删除 */

/* 项显示名：BMS1/BMS2/BMS3/MCU 为器件缩写，按需求不翻译 */
static const char *item_name[FAULT_ITEM_NUM] = {
    "BMS1",
    "BMS2",
    "BMS3",
    "MCU",
};

/* 刷新：各槽位选中态（模板控件，按 slot 切 n/p + opa） */
static void view_faults_refresh(void)
{
    for (int s = 0; s < FAULT_ITEM_NUM; s++)
        tpl4_set_card_pressed(s, s == (int)focus_slot);
}

/* 详情态 label：经 proxy 取当前项故障码，无故障绿字 "No Fault"，
 * 有故障红字 "DTC: 0xNN"（进详情时取值一次，不随 CAN 实时刷新） */
static void view_faults_show_fault(void)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    int32_t code;
    char text[FAULT_TEXT_MAX];

    if (!page->pop_tpl4)
        return;

    if (!fault_label) {
        fault_label = lv_label_create(page->pop_tpl4);
        lv_obj_align(fault_label, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_font(fault_label, aic_get_multi_lang_font(22), LV_PART_MAIN);
    }

    code = vehicle_get_fault_code((veh_fault_item_e)focus_slot);
    if (code > 0) {
        lv_snprintf(text, sizeof(text), "DTC: 0x%02X", (unsigned int)code);
        lv_obj_set_style_text_color(fault_label, lv_color_hex(0xfc0b0b), LV_PART_MAIN);
    } else {
        lv_snprintf(text, sizeof(text), "No Fault");
        lv_obj_set_style_text_color(fault_label, lv_color_hex(0x00ff55), LV_PART_MAIN);
    }
    lv_label_set_text(fault_label, text);
    lv_obj_remove_flag(fault_label, LV_OBJ_FLAG_HIDDEN);
}

/* 详情态 → 列表态：隐故障 label，恢复四张 item 卡片与页面标题（label 保留复用） */
static void view_faults_leave_detail(void)
{
    if (fault_label)
        lv_obj_add_flag(fault_label, LV_OBJ_FLAG_HIDDEN);
    detail_view = false;

    tpl4_activate(aic_get_translation(view_faults), NULL);
    for (int s = 0; s < FAULT_ITEM_NUM; s++)
        tpl4_set_slot(s, item_name[s], true);
    view_faults_refresh();
}

void view_faults_init(void)
{
    set_current_menu_level(MENU_LEVEL_3);

    tpl4_activate(aic_get_translation(view_faults), NULL);   /* 标题居中由模板默认（无图标） */
    tpl4_set_label_mode(false, false);                 /* 左对齐 + arrow */
    for (int s = 0; s < FAULT_ITEM_NUM; s++)
        tpl4_set_slot(s, item_name[s], true);

    detail_view = false;
    focus_slot = FAULT_BMS1;
    view_faults_refresh();
}

/* 语言切换刷新：保留焦点位置（仅标题随语言变化，列表项为缩写） */
void view_faults_relang(void)
{
    tpl4_activate(aic_get_translation(view_faults), NULL);
    tpl4_set_label_mode(false, false);
    for (int s = 0; s < FAULT_ITEM_NUM; s++)
        tpl4_set_slot(s, item_name[s], true);
    view_faults_refresh();
}

void view_faults_destroy(void)
{
    focus_slot = FAULT_BMS1;
    detail_view = false;
    if (fault_label) {
        lv_obj_delete(fault_label);
        fault_label = NULL;
    }
}

/* ===== 按键处理（环形移动，表驱动） ===== */
static void view_faults_deal_up(void)
{
    if (detail_view)   /* 详情态无焦点移动 */
        return;
    focus_slot = (fault_item_e)((focus_slot - 1 + FAULT_ITEM_NUM) % FAULT_ITEM_NUM);
    view_faults_refresh();
}

static void view_faults_deal_down(void)
{
    if (detail_view)
        return;
    focus_slot = (fault_item_e)((focus_slot + 1) % FAULT_ITEM_NUM);
    view_faults_refresh();
}

/* SET：进详情态 —— 隐四张 item 卡片，标题切为当前项名，显示该项故障信息 */
static void view_faults_deal_set(void)
{
    if (detail_view)
        return;

    detail_view = true;
    for (int s = 0; s < FAULT_ITEM_NUM; s++)
        tpl4_set_slot(s, item_name[s], false);
    tpl4_activate(item_name[focus_slot], NULL);
    view_faults_show_fault();
}

static void view_faults_deal_back(void)
{
    if (detail_view) {
        view_faults_leave_detail();   /* 详情态：回列表，不退出 L3 */
        return;
    }

    tpl4_hide();
    view_faults_destroy();
    meter_view_init();   /* 回 L2，重配 tpl3 为 meter view */
}

static short_click_deal short_click[] = {
    [CLICK_SHORT_UP]   = view_faults_deal_up,
    [CLICK_SHORT_DOWN] = view_faults_deal_down,
    [CLICK_SHORT_SET]  = view_faults_deal_set,
    [CLICK_SHORT_BACK] = view_faults_deal_back,
};

void on_view_faults_deal_short_key(click_e key)
{
    if (key < sizeof(short_click) / sizeof(short_click_deal) && short_click[key])
        short_click[key]();
}

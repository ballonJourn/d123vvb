#include "language.h"
#include "setting_menu.h"
#include "template.h"
#include "vehicle_argument.h"
#include "aic_lang.h"
#include "instrument.h"
#include "logic.h"          /* global_refresh_language */

/* ===== 翻页状态（选项表索引，页/槽位由它换算） ===== */
static int item_idx = 0;

/* ===== 当前对外提供的语言选项（唯一开关）=====
 * 1 = 全部 5 种：英文 / 中文 / 韩文 / 新加坡语 / 泰文
 * 0 = 仅英文 / 中文
 * 关闭时只影响"语言菜单里能选哪些"：translations 的 3~5 列译文、THAI_TTF 独立字体、
 * language_option_to_lang_type() 的 5 个映射分支全部保留，把本宏改回 1 即恢复。 */
#define LANGUAGE_OPTION_ALL   0

/* 选项表：一维，表顺序即翻页顺序；每页固定 LANGUAGE_SLOT_NUM 槽（= tpl3 槽数），
 * 窗口在表上滚动，up/down 到表尾/表头首尾相接 */
#define LANGUAGE_SLOT_NUM   3

#if LANGUAGE_OPTION_ALL
static const language_option_e item_list[] = {
    LANGUAGE_ENGLISH_OPTION, LANGUAGE_CHINESE_OPTION, LANGUAGE_KOREAN_OPTION,
    LANGUAGE_SINGAPOREAN_OPTION, LANGUAGE_THAI_OPTION,
};
#else
static const language_option_e item_list[] = {
    LANGUAGE_ENGLISH_OPTION, LANGUAGE_CHINESE_OPTION,
};
#endif
#define LANGUAGE_ITEM_NUM   ((int)(sizeof(item_list) / sizeof(item_list[0])))

/* 项显示名：语言自名，各语言以自己的文字显示（走词条，5 列同值） */
static const key_id_t item_key[LANGUAGE_OPTION_MAX] = {
    lang_name_english,
    lang_name_chinese,
    lang_name_korean,
    lang_name_singaporean,
    lang_name_thai,
};

/* 语言选项 → 系统语言类型。
 * 注意：language_option_e{EN=0,CH=1,...} 与 lang_type_t{CH=0,EN=1,...} 前两项相反，
 * 必须走本映射，不能强转。 */
lang_type_t language_option_to_lang_type(language_option_e o)
{
    /* 不在 item_list 中的选项一律回落英文 —— 主要是"曾存过韩/新/泰、但当前菜单
     * 不提供"的设备，保证开机恢复的语言与语言页选中态一致 */
    int offered = 0;
    for (int i = 0; i < LANGUAGE_ITEM_NUM && !offered; i++)
        if (item_list[i] == o) { offered = 1; }
    if (!offered)
        return AIC_LANG_EN;

    switch (o) {
    case LANGUAGE_ENGLISH_OPTION:     return AIC_LANG_EN;
    case LANGUAGE_CHINESE_OPTION:     return AIC_LANG_CH;
    /* 以下三种语言的译文与字体均已就绪，仅需把 LANGUAGE_OPTION_ALL 改为 1 即可在菜单中提供 */
    case LANGUAGE_KOREAN_OPTION:      return AIC_LANG_KO;
    case LANGUAGE_SINGAPOREAN_OPTION: return AIC_LANG_SG;
    case LANGUAGE_THAI_OPTION:        return AIC_LANG_TH;
    default:                          return AIC_LANG_EN;
    }
}

static language_option_e current_option(void)
{
    return item_list[item_idx];
}

/* 刷新当前页：窗口内各槽位选中态 + 项文字（模板控件）；表中已无对应项的槽隐藏 */
static void language_refresh_page(void)
{
    int page = item_idx / LANGUAGE_SLOT_NUM;
    for (int s = 0; s < LANGUAGE_SLOT_NUM; s++) {
        int i = page * LANGUAGE_SLOT_NUM + s;
        if (i >= LANGUAGE_ITEM_NUM) {
            tpl3_set_slot(s, NULL, false);
            continue;
        }
        tpl3_set_slot(s, aic_get_translation(item_key[item_list[i]]), true);
        tpl3_set_card_pressed(s, i == item_idx);
    }
}

void language_init(void)
{
    setting_page_t *page = setting_page_get(&ui_manager);
    set_current_menu_level(MENU_LEVEL_3);

    tpl3_activate(aic_get_translation(language_settings), NULL);
    tpl3_set_label_mode(true, false);   /* 居中 + 无 arrow */

    /* 取已存语言，定位到表中对应索引；不在提供范围内则停在默认的英文
     * （与 language_option_to_lang_type() 的回落一致，避免选中态与实际语言不符） */
    item_idx = 0;
    uint8_t value = vehicle_get_param_language();
    for (int i = 0; i < LANGUAGE_ITEM_NUM; i++) {
        if ((language_option_e)value == item_list[i]) {
            item_idx = i;
            break;
        }
    }
    language_refresh_page();
}

/* 语言切换刷新：保留翻页/焦点位置（选项自名与语言无关，仅标题随语言变化） */
void language_relang(void)
{
    tpl3_activate(aic_get_translation(language_settings), NULL);
    tpl3_set_label_mode(true, false);
    language_refresh_page();
}

void language_destroy(void)
{
    item_idx = 0;
}

/* ===== 按键处理（在选项表上循环移动，首尾相接） ===== */
static void language_move(int step)
{
    item_idx = (item_idx + step + LANGUAGE_ITEM_NUM) % LANGUAGE_ITEM_NUM;
    language_refresh_page();
}

static void language_deal_down(void)
{
    language_move(1);
}

static void language_deal_up(void)
{
    language_move(-1);
}

static void language_deal_set(void)
{
    language_option_e o = current_option();
    vehicle_set_param_language((uint8_t)o);

    /* 切语言后立即刷新：主界面（含实时值/单位）→ 设置弹窗 → 投屏页。
     * 弹窗（pop_tip）无需刷新，其每次 show 都重新查表翻译。 */
    global_refresh_language(language_option_to_lang_type(o));
    setting_page_update_current_language();
    phone_page_update_current_language();
}

static void language_deal_back(void)
{
    tpl3_hide();
    language_destroy();
    instrument_init();   /* 回 L2，重配 tpl3 为 instrument */
}

static short_click_deal short_click[] = {
    [CLICK_SHORT_UP]   = language_deal_up,
    [CLICK_SHORT_DOWN] = language_deal_down,
    [CLICK_SHORT_SET]  = language_deal_set,
    [CLICK_SHORT_BACK] = language_deal_back,
};

void on_language_deal_short_key(click_e key)
{
    if (key < sizeof(short_click) / sizeof(short_click_deal) && short_click[key])
        short_click[key]();
}

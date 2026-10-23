#include "set_page_key.h"
#include "button_manager.h"
#include "setting_menu.h"
#include "unlock_seat.h"
#include "power_down.h"
#include "tire_pressure.h"
#include "set_view_interface.h"
#include "window_manager.h"

typedef struct {
    void (*view_init)(void);
    void (*deal_short_key)(click_e);
    void (*lang_refresh)(void);      /* 语言切换时重填本层文案（保留焦点位置） */
} setting_entry_t;

/* L1：单一处理器（6 分类网格导航） */
static setting_entry_t setting_entry_level_1 = {
    setting_l1_view_init, on_setting_l1_deal_short_click, setting_l1_refresh_language,
};

/* L2：按分类索引，仅 UNLOCK / MILEAGE / POWEROFF / INSTRUMENT / METERVIEW 可进；其余项 NULL → SET 无反应 */
static setting_entry_t setting_entry_level_2[SETTING_CATEGORY_NUM] = {
    [CATEGORY_UNLOCK]     = { unlock_seat_init,        on_unlock_seat_deal_short_key,    unlock_seat_relang        },
    [CATEGORY_MILEAGE]    = { reset_trip_init,         on_reset_trip_deal_short_key,     reset_trip_relang         },
    [CATEGORY_POWEROFF]   = { power_down_init,         on_power_down_deal_short_key,     power_down_relang         },
    [CATEGORY_INSTRUMENT] = { instrument_init,         on_instrument_deal_short_key,     instrument_relang         },
    [CATEGORY_METERVIEW]  = { meter_view_init,         on_meter_view_deal_short_key,     meter_view_relang         },
};

/* L3：二维表，第一维为分类、第二维为该分类下 L2 选中项索引。
 * 内层维度取各分类项数的较大值，保证任意分类不越界。 */
#define L3_ITEM_MAX  (INSTRUMENT_ITEM_NUM > METER_ITEM_NUM ? \
                       INSTRUMENT_ITEM_NUM : METER_ITEM_NUM)
static setting_entry_t setting_entry_level_3[SETTING_CATEGORY_NUM][L3_ITEM_MAX] = {
    [CATEGORY_INSTRUMENT][INSTRUMENT_LANGUAGE]        = { language_init,         on_language_deal_short_key,        language_relang         },
    [CATEGORY_INSTRUMENT][INSTRUMENT_BRIGHTNESS]      = { brightness_init,       on_brightness_deal_short_key,      brightness_relang       },
    [CATEGORY_INSTRUMENT][INSTRUMENT_UNIT]            = { unit_init,             on_unit_deal_short_key,            unit_relang             },
    // [CATEGORY_INSTRUMENT][INSTRUMENT_CDC]             = { cdc_settings_init,     on_cdc_settings_deal_short_key    },
    [CATEGORY_INSTRUMENT][INSTRUMENT_TCS]             = { tcs_switch_init,       on_tcs_switch_deal_short_key,      tcs_switch_relang       },
    [CATEGORY_INSTRUMENT][INSTRUMENT_FACTORY_RESET]   = { restore_factory_init,  on_restore_factory_deal_short_key, restore_factory_relang  },
    [CATEGORY_METERVIEW][METER_TIRE]                  = { tire_pressure_init,    on_tire_pressure_deal_short_key,   tire_pressure_relang    },
    [CATEGORY_METERVIEW][METER_FAULTS]                = { view_faults_init,      on_view_faults_deal_short_key,     view_faults_relang      },
    [CATEGORY_METERVIEW][METER_VERSION]               = { sw_version_init,       on_sw_version_deal_short_key,      sw_version_relang       },
};

/* L4：当前活动的 L4 按键处理器（由 L3 进入 L4 时设置，L4 退出时清空）。
 * L4 入口由 L3 内部 focus 动态决定，不适合静态二维表，改用函数指针。 */
static void (*active_l4_handler)(click_e) = NULL;
void set_active_l4_handler(void (*handler)(click_e)) { active_l4_handler = handler; }

/* L4 语言刷新处理器，与 active_l4_handler 成对设置/清空 */
static void (*active_l4_lang_refresh)(void) = NULL;
void set_active_l4_lang_refresh(void (*handler)(void)) { active_l4_lang_refresh = handler; }

/* L3 当前表项：索引算法唯一出处，菜单路由与语言刷新共用，避免两处漂移 */
static setting_entry_t *current_l3_entry(void)
{
    setting_category_e cat = get_l1_index();
    int item = (cat == CATEGORY_INSTRUMENT) ? (int)get_l2_instrument_index()
                                            : (int)get_l2_meter_index();
    return &setting_entry_level_3[cat][item];
}

/* 语言切换时按当前层派发到该层的 lang_refresh。
 * 注意 get_current_menu_level() 不足以判定"哪个模板层在用"（同层可能是 tpl3/tpl4/tpl_yn），
 * 故一律以"谁激活了该层"为准，即本路由表。 */
void menu_manager_lang_refresh(void)
{
    switch (get_current_menu_level()) {
    case MENU_LEVEL_1:
        setting_l1_refresh_language();
        break;
    case MENU_LEVEL_2: {
        setting_category_e cat = get_l1_index();
        if (setting_entry_level_2[cat].lang_refresh)
            setting_entry_level_2[cat].lang_refresh();
        break;
    }
    case MENU_LEVEL_3:
        if (current_l3_entry()->lang_refresh)
            current_l3_entry()->lang_refresh();
        break;
    case MENU_LEVEL_4:
        if (active_l4_lang_refresh)
            active_l4_lang_refresh();
        break;
    default:
        break;
    }
}

bool check_clicked_isVaild(int menu_index)
{
    if (menu_index < 0 || menu_index >= (int)SETTING_CATEGORY_NUM)
        return false;
    return setting_entry_level_2[menu_index].deal_short_key != NULL;
}

void menu_manager_click(const click_e click)
{
    menu_level_e level = get_current_menu_level();
    if (click >= CLICK_END)
        return;

    /* HOME 键: 从设置任意层级一键回主界面 (setting_popup_hide 会复位 L1-L4 状态) */
    if (click == CLICK_SHORT_HOME) {
        window_manager_set_target(WINDOW_MAIN);
        return;
    }

    switch (level) {
    case MENU_LEVEL_1:
        if (setting_entry_level_1.deal_short_key)
            setting_entry_level_1.deal_short_key(click);
        if (click == CLICK_SHORT_SET) {
            setting_category_e cat = get_l1_index();
            if (setting_entry_level_2[cat].view_init)
                setting_entry_level_2[cat].view_init();
        }
        break;

    case MENU_LEVEL_2: {
        setting_category_e cat = get_l1_index();
        if (setting_entry_level_2[cat].deal_short_key)
            setting_entry_level_2[cat].deal_short_key(click);
        /* SET 升级 L3：按分类取对应 L2 选中项，查二维 entry 表 */
        if (click == CLICK_SHORT_SET) {
            if (current_l3_entry()->view_init)
                current_l3_entry()->view_init();
        }
        break;
    }

    case MENU_LEVEL_3: {
        /* L2/L3 不改 l1_index，按分类上下文查二维 entry 表 */
        if (current_l3_entry()->deal_short_key)
            current_l3_entry()->deal_short_key(click);
        break;
    }

    case MENU_LEVEL_4:
        if (active_l4_handler)
            active_l4_handler(click);
        break;

    default:
        break;
    }
}

/*
 * pop_tip_manager.c - CAN 故障弹窗业务逻辑（分辨率无关）
 *
 * 布局代码在 <UI_RES_DIR>/pop_tip_page.c（几何/控件/字体），本文件只负责：
 *   - 读 VEH_CAN_POPUP_MSG 位图, 维护 ack 记忆与 5s 自动消失定时
 *   - 决定弹哪个码, 查表得到背景/图标/词条后交给布局层呈现
 */

#include "pop_tip_manager.h"
#include "pop_tip_page.h"
#include "aic_ui.h"
#include "aic_lang.h"
#include "mw_can.h"          /* pop_tip_code_e */
#include "vehicle_data.h"

#define POP_TIP_AUTO_HIDE_MS  5000

///< "匹配中"点数动画: 每 N 拍换一格(50ms/拍 → 8 拍 = 400ms, 一个循环 1.2s)
#define TPMS_MATCHING_TICKS   8
///< 点数档位数, 与 tpms_matching_dots[] 对齐
#define TPMS_MATCHING_STEPS   3
///< "匹配中"文案缓冲: 最长词条(泰文"กำลังจับคู่ TPMS"约 35B) + "..." + '\0'
#define POP_TIP_TEXT_MAX      64

#define POP_TIP_BG_ERR     LVGL_IMAGE_PATH(setting/pop_tip_err_bg.png)
#define POP_TIP_BG_WARN    LVGL_IMAGE_PATH(setting/pop_tip_warn_bg.png)
#define POP_TIP_BG_SUCCESS LVGL_IMAGE_PATH(setting/pop_tip_succeed_bg.png)

///< 故障位掩码: code 取 pop_tip_code_e (1-based), 对应 VEH_CAN_POPUP_MSG 第 (code-1) 位
#define POP_TIP_BIT(code)   (1u << ((code) - 1))
///< 有效位掩码: 覆盖 pop_tip_code_e 全部码 (bit0 .. bit(POP_TIP_END-2))
#define POP_TIP_VALID_MASK  ((1u << (POP_TIP_END - 1)) - 1)

/* VEH_CAN_POPUP_MSG 存于 int32_t 槽位(见 vehicle_param.c), 位图最多承载 31 个码。
 * 码数超出时此处编译失败(数组长度为负), 避免 1u<<32 静默溢出。
 * 注: 不能用 #if, 枚举常量在预处理阶段是未定义标识符。 */
typedef char pop_tip_bit_width_check[(POP_TIP_END - 1) <= 31 ? 1 : -1];

typedef struct {
    const char *bg;       ///< 背景 src (err/warn/success)
    const char *icon;     ///< 弹出图片 src
    key_id_t text_key;    ///< 多语言词条
} pop_tip_item_t;

///< 映射表:索引即 pop_tip_code_e。bg 的 err/warn 为占位,后续可在此调整
static const pop_tip_item_t pop_tip_table[POP_TIP_END] = {
    [POP_TIP_NONE]            = {NULL, NULL, SAMPLE},
    [POP_TIP_SEAT_UNLOCK]     = {POP_TIP_BG_WARN, LVGL_IMAGE_PATH(setting/popup_seat_unlock.png),       pop_seat_unlock},
    [POP_TIP_BATT_LOW]        = {POP_TIP_BG_WARN,  LVGL_IMAGE_PATH(setting/popup_batt_low.png),          pop_batt_low},
    [POP_TIP_CRUISE_FAULT]    = {POP_TIP_BG_ERR,  LVGL_IMAGE_PATH(setting/popup_cruise_fault.png),      pop_cruise_fault},
    [POP_TIP_NFC_LEARNING]    = {POP_TIP_BG_WARN, LVGL_IMAGE_PATH(setting/popup_nfc.png),               pop_nfc_learning},
    [POP_TIP_NFC_SUCCESS]     = {POP_TIP_BG_SUCCESS, LVGL_IMAGE_PATH(setting/popup_nfc.png),            pop_nfc_success},
    [POP_TIP_NFC_FAILURE]     = {POP_TIP_BG_ERR,  LVGL_IMAGE_PATH(setting/popup_nfc.png),               pop_nfc_failure},
    [POP_TIP_TPMS_INIT]       = {POP_TIP_BG_WARN, LVGL_IMAGE_PATH(setting/popup_tpms_init.png),         pop_tpms_init},
    [POP_TIP_TPMS_SUCCESS]       = {POP_TIP_BG_SUCCESS, LVGL_IMAGE_PATH(setting/popup_upgrade_successfully.png),         pop_tpms_success},
    [POP_TIP_TPMS_FAILED]       = {POP_TIP_BG_ERR, LVGL_IMAGE_PATH(setting/popup_upgrade_failed.png),         pop_tpms_failed},
    [POP_TIP_UPGRADE]         = {POP_TIP_BG_WARN, LVGL_IMAGE_PATH(setting/popup_upgrade.png),          pop_upgrade},
    [POP_TIP_UPGRADE_FAILED]  = {POP_TIP_BG_ERR,  LVGL_IMAGE_PATH(setting/popup_upgrade_failed.png),   pop_upgrade_failed},
    [POP_TIP_UPGRADE_SUCCESS] = {POP_TIP_BG_SUCCESS, LVGL_IMAGE_PATH(setting/popup_upgrade_successfully.png), pop_upgrade_success},
};

///< ack 记忆: 已展示且仍 active 的故障位。某故障清 0 后从 ack 移除, 使其 0->1 可再触发
static uint32_t g_ack_mask = 0;

/* ===== 弹窗来源与优先级 =====
 * CAN 故障优先级最高: 故障到达时抢占上屏, 且外部弹窗不会把它顶掉。
 * 外部(UI 自发, 如胎压页"匹配中")只在没有 CAN 弹窗时上屏, CAN 弹窗收尾后自动恢复。 */
///< 当前正在展示的 CAN 故障码(0=无)
static int g_can_code = 0;
///< 外部(UI)请求展示的码(0=无请求); 生命周期由调用方 show_hold/hide 管
static int g_external_code = 0;
///< 当前实际已上屏的码(0=未上屏)。render 之间做去重, 避免每 50ms tick 重复 show
static int g_shown_code = 0;
///< 5s 自动消失定时(只管 CAN 弹窗; 外部弹窗由调用方收, 其上屏期间定时保持暂停)
static lv_timer_t *g_timer = NULL;

/* ===== "匹配中"(POP_TIP_TPMS_INIT) 点数动画 =====
 * 词条本身不含点, 由本模块在词条后逐帧追加 "."/".."/"...", 让用户看得出还在等。
 * 逐帧改字只走 pop_tip_page_set_text(), 不走 render —— render 只负责"换码时上屏",
 * 保持幂等去重(每 50ms 重复调用是常态)。 */
static const char *const tpms_matching_dots[TPMS_MATCHING_STEPS] = { ".", "..", "..." };
static int matching_count = 0;      ///< 当前点数档位(0→1→2→0)
static int matching_tick = 0;       ///< 距上一格已过的拍数

///< "匹配中"文案 = 词条 + 当前点位; 首帧上屏与逐帧动画共用, 保证第一帧就带点
static void pop_tip_matching_text(char *buf, size_t size)
{
    lv_snprintf(buf, size, "%s%s",
                aic_get_translation(pop_tip_table[POP_TIP_TPMS_INIT].text_key),
                tpms_matching_dots[matching_count]);
}

///< 点数动画: 由 pop_tip_poll() 每个 50ms 拍无条件调用, 只有"匹配中"真的在屏上才推进。
///< 被 CAN 弹窗顶住 / 已收掉时停表, 恢复显示后从当前档位继续、重新计时。
static void pop_tip_matching_dots_tick(void)
{
    if (g_shown_code != POP_TIP_TPMS_INIT) {
        matching_tick = 0;
        return;
    }

    if (++matching_tick < TPMS_MATCHING_TICKS)
        return;

    matching_tick = 0;
    if (++matching_count >= TPMS_MATCHING_STEPS)
        matching_count = 0;

    char text[POP_TIP_TEXT_MAX];
    pop_tip_matching_text(text, sizeof(text));
    pop_tip_page_set_text(text);
}

///< 取掩码中最低位的故障码(1..POP_TIP_END-1), 无则 0
static int pop_tip_lowest_code(uint32_t mask)
{
    for (int code = 1; code < POP_TIP_END; code++) {
        if (mask & POP_TIP_BIT(code))
            return code;
    }
    return 0;
}

///< 退弹窗时释放表内图 src(归还 CMA, 保留 home 屏图)
static void pop_tip_drop_image_cache(void)
{
    for (int code = 1; code < POP_TIP_END; code++) {
        const pop_tip_item_t *item = &pop_tip_table[code];
        if (item->bg)
            lv_image_cache_drop(item->bg);
        if (item->icon)
            lv_image_cache_drop(item->icon);
    }
}

///< 上屏原语(唯一出口): CAN 码优先, 无 CAN 码才落到外部码; 都没请求则隐藏并释放图缓存。
///< 幂等 —— 目标码与当前已上屏的一致就直接返回, 可每 tick 安全调用。
static void pop_tip_render(void)
{
    int code = g_can_code ? g_can_code : g_external_code;

    if (code == g_shown_code)
        return;

    if (code == 0) {
        g_shown_code = 0;
        pop_tip_page_hide();
        if (g_timer)
            lv_timer_pause(g_timer);
        pop_tip_drop_image_cache();
        return;
    }

    /* 外部码不受 5s 自动消失管: 它的生命周期归调用方 */
    if (g_can_code == 0 && g_timer)
        lv_timer_pause(g_timer);

    const pop_tip_item_t *item = &pop_tip_table[code];
    if (code == POP_TIP_TPMS_INIT) {
        /* 带点数上屏(用的是 pop_tip_matching_text 的当前档位)，
         * 之后的逐帧改字由 pop_tip_matching_dots_tick() 接手 */
        char text[POP_TIP_TEXT_MAX];
        pop_tip_matching_text(text, sizeof(text));
        pop_tip_page_show(item->bg, item->icon, text);
    } else {
        pop_tip_page_show(item->bg, item->icon, aic_get_translation(item->text_key));
    }
    g_shown_code = code;
}

static void pop_tip_auto_hide_cb(lv_timer_t *t)
{
    (void)t;
    /* 5s 到: 只收 CAN 弹窗, 不清 ack(故障仍记忆, 不重复弹出直到 0->1 再触发)。
     * 外部码若仍在请求, render 会让它自动回到屏幕上 */
    g_can_code = 0;
    pop_tip_render();
}

///< 重置 5s 自动消失定时
static void pop_tip_reset_timer(void)
{
    if (!g_timer)
        g_timer = lv_timer_create(pop_tip_auto_hide_cb, POP_TIP_AUTO_HIDE_MS, NULL);
    else
        lv_timer_reset(g_timer);
    lv_timer_resume(g_timer);
}

void pop_tip_show(int code)
{
    if (code <= POP_TIP_NONE || code >= POP_TIP_END)
        return;

    /* 按 CAN 优先级直接弹: 抢占外部弹窗并起 5s 自动消失 */
    g_can_code = code;
    pop_tip_render();
    pop_tip_reset_timer();
}

void pop_tip_show_hold(int code)
{
    if (code <= POP_TIP_NONE || code >= POP_TIP_END)
        return;

    /* 外部(UI)请求: 只登记请求码, 是否立即上屏由 pop_tip_render 按优先级决定 ——
     * CAN 弹窗正在展示时会被顶住(不顺延、不打断), 等它收尾再自动冒出来 */
    g_external_code = code;
    matching_count = 0;
    matching_tick = 0;
    pop_tip_render();
}

void pop_tip_hide(void)
{
    /* 撤掉外部请求。CAN 弹窗正在展示时保持不动(CAN 优先级最高), 否则收掉屏幕 */
    g_external_code = 0;
    pop_tip_render();
}

bool pop_tip_is_visible(void)
{
    return pop_tip_page_is_visible();
}

void pop_tip_poll(void)
{
    uint32_t current = (uint32_t)vehicle_get_can_popup_msg() & POP_TIP_VALID_MASK;

    /* "匹配中"点数动画必须在这里无条件推: 下面几个分支只在"有新故障 / CAN 码刚清掉
     * / 位图全 0"时才调 render, 有故障位长期置位(如 SOC<20 的低电量)时一次都不调 */
    pop_tip_matching_dots_tick();

    if (current == 0) {
        /* 全部故障清 0: 收 CAN 弹窗, 清 ack。
         * 外部弹窗若有请求由 render 顶上(注意不能用 pop_tip_hide, 那是"撤销请求") */
        g_ack_mask = 0;
        g_can_code = 0;
        pop_tip_render();
        return;
    }

    /* 故障清 0 后从 ack 移除, 使其 0->1 可再次触发 */
    g_ack_mask &= current;

    uint32_t new_bits = current & ~g_ack_mask;   /* 新出现的故障(0->1) */

    if (new_bits != 0) {
        /* 新故障: 抢占上屏(优先于外部弹窗)并重置 5s */
        int code = pop_tip_lowest_code(new_bits);
        pop_tip_show(code);
        g_ack_mask |= POP_TIP_BIT(code);
        return;
    }

    /* 无新故障: 当前展示的 CAN 故障已清 0 则收尾(收完 render 会落回外部码) */
    if (g_can_code != 0 && !(current & POP_TIP_BIT(g_can_code))) {
        g_can_code = 0;
        pop_tip_render();
    }
    /* 不可见且无新故障: 保持隐藏(已 ack, 等 0->1 再触发或全清) */
}
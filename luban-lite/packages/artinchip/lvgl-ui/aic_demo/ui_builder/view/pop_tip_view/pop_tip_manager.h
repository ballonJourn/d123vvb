#ifndef POP_TIP_MANAGER_H
#define POP_TIP_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/*
 * CAN 故障弹窗业务逻辑（分辨率无关）
 *
 * 数据源: VEH_CAN_POPUP_MSG 位图, 第 (code-1) 位为 1 表示 pop_tip_code_e 中
 *         该故障存在, 全 0 表示无故障。
 * 呈现:   布局层 <UI_RES_DIR>/pop_tip_page.c, 本层只把"背景/图标/文案"交给它。
 *
 * 轮询语义(50ms):
 *   - 新故障位(0->1): 显示该故障并(重)置 5s
 *   - 5s 到: 自动隐藏(故障位仍记忆, 不重复弹出直到 0->1 再触发)
 *   - 位图全 0: 立即隐藏
 *   - 当前显示的故障位清0但仍有其他故障: 收尾, 不重置定时
 *
 * 优先级(弹窗来源两种, CAN 最高):
 *   CAN 故障 > 外部(UI 自发)
 *   - 新 CAN 故障到达时抢占上屏, 正在显示的外部弹窗被顶掉(不顺延、不打断 CAN)
 *   - CAN 弹窗自行收尾(5s 到 / 位清 0)后, 外部弹窗若仍在请求就自动恢复显示
 *   - 外部弹窗期间 5s 自动消隐定时保持暂停(它的生命周期归调用方)
 */
void pop_tip_poll(void);

///< 按 CAN 优先级直接弹某个码: 抢占外部弹窗并(重)置 5s 自动消失。
///< 注意: CAN 弹窗仍受位图管辖 —— 位图里没有该码时下一个 50ms tick 就会收掉它,
///<       要让 UI 自己控制显隐请用 pop_tip_show_hold()
void pop_tip_show(int code);

///< 登记一条外部(非 CAN 位图)弹窗请求, 用于 UI 自己发起的"处理中"提示。
///< CAN 弹窗正在显示时该请求会被顶住(稍后才上屏), 否则立即上屏;
///< 撤销请求用 pop_tip_hide()。同一时间只允许一个外部弹窗, 重复调用即换码。
///< POP_TIP_TPMS_INIT 会额外在词条后自动追加点数动画("."/".."/"..."), 调用方无需处理。
void pop_tip_show_hold(int code);

///< 撤销外部弹窗请求并释放图缓存。CAN 弹窗正在显示时保持不动(优先级最高)。
void pop_tip_hide(void);

///< 弹窗是否正在显示(不区分 CAN / 外部来源)
bool pop_tip_is_visible(void);

#ifdef __cplusplus
}
#endif

#endif /* POP_TIP_MANAGER_H */
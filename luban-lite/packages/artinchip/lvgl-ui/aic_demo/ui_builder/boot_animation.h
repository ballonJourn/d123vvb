/*
 * 开机动画：优先播放，动画结束后再加载主界面，避免主界面图片挤占
 * 图片解码缓存（LV_CACHE_IMG_NUM 上限 / 容量）导致动画掉帧。
 * 仅在未定义 AIC_STARTUP_UI_SHOW 时参与编译（见 boot_animation.c）。
 */
#ifndef _BOOT_ANIMATION_H
#define _BOOT_ANIMATION_H

#ifdef __cplusplus
extern "C" {
#endif

void boot_animation_init(void);
/* 动画播放到约定帧时由 boot_animation.c 调用，提前创建主界面对象并解码
 * 图片（不切屏），用动画剩余帧的播放时间消化主界面加载，避免播完才
 * 加载导致切屏闪烁。 */
void boot_animation_preload_cb(void);
/* 动画播放完毕（或动画资源缺失直接跳过）时由 boot_animation.c 调用，
 * 切到已就绪的主界面并启动业务逻辑（lv_scr_load + custom_init）。 */
void boot_animation_finish_cb(void);

#ifdef __cplusplus
}
#endif

#endif /* _BOOT_ANIMATION_H */
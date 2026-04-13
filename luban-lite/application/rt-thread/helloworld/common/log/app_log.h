#ifndef __APP_LOG_H__
#define __APP_LOG_H__

#ifdef __cplusplus
extern "C" {
#endif
#include <stdio.h>

///< Assert enable 
#define RT_USE_ASSERT 1

///< Global log enable
#define G_LOG_EN 1

///< Log color enable 
#define LOG_COLOR   0

///< Log level
#define U_LOG_LVL_OFF             0
#define U_LOG_LVL_ERROR           1
#define U_LOG_LVL_WARNING         2
#define U_LOG_LVL_INFO            3
#define U_LOG_LVL_DEBUG           4

#ifndef LOG_LVL
#define LOG_LVL U_LOG_LVL_DEBUG
#endif

#if LOG_COLOR
#define LOG_COLOR_HEAD(lvl, color) printf("\033["#color"m["lvl"] ")
#define LOG_COLOR_TAIL printf("\033[0m\n")
#else
#define LOG_COLOR_HEAD(lvl, color) printf("["lvl"] ")
#define LOG_COLOR_TAIL printf("\n")
#endif 

#define LOG_PRINTF(lvl, color, fmt, ...)                                        \
    do                                                                          \
    {                                                                           \
        LOG_COLOR_HEAD(lvl, color);                                             \
        printf("[%s: line:%d] " fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__);    \
        LOG_COLOR_TAIL;                                                         \
    } while (0)


#if (LOG_LVL != U_LOG_LVL_OFF && G_LOG_EN)
#define rt_log_raw(...)      printf(__VA_ARGS__);
#else
#define rt_log_raw(...)
#endif

#if (LOG_LVL >= U_LOG_LVL_ERROR && G_LOG_EN)
#define rt_log_error(fmt, ...)      LOG_PRINTF("ERROR", 31, fmt, ##__VA_ARGS__)
#else
#define rt_log_error(...)
#endif

#if (LOG_LVL >= U_LOG_LVL_WARNING && G_LOG_EN)
#define rt_log_warn(fmt, ...)      LOG_PRINTF("WARN", 33, fmt, ##__VA_ARGS__)
#else
#define rt_log_warn(...)
#endif

#if (LOG_LVL >= U_LOG_LVL_INFO && G_LOG_EN)
#define rt_log_info(fmt, ...)      LOG_PRINTF("INFO", 32, fmt, ##__VA_ARGS__)
#else
#define rt_log_info(...)
#endif

#if (LOG_LVL >= U_LOG_LVL_DEBUG && G_LOG_EN)
#define rt_log_debug(fmt, ...)      LOG_PRINTF("DEBUG", 0, fmt, ##__VA_ARGS__)
#else
#define rt_log_debug(...)
#endif

#if RT_USE_ASSERT
#define RT_ASSERT_PARAM(expr) \
        do {                                                                    \
        if(!(expr)) {                                                           \
            printf("[%s: line:%d] assert failed\n", __FUNCTION__, __LINE__);    \
            while(1);                                                           \
        }                                                                       \
    } while(0)
#else
#define RT_USE_ASSERT(expr)
#endif

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // __APP_LOG_H__
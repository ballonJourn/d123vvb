#ifndef __MW_LOG_H__
#define __MW_LOG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
#include "dev_config.h"

typedef enum  {
	MW_LOGL_ERROR = 0,
	MW_LOGL_WARNING,
	MW_LOGL_INFO,
	MW_LOGL_DEBUG,
	MW_LOGL_VERBOSE,
	MW_LOGL_COUNT,

	MW_LOGL_DEFAULT = 	MW_LOGL_ERROR,
	MW_LOGL_FORCE_DEBUG = 0x10,
} mw_log_level;

#ifdef MW_LOG_DEBUG
#define _MW_LOG_DEBUG	MW_LOGL_FORCE_DEBUG
#else
#define _MW_LOG_DEBUG	0
#endif

#define MW_TAG_ERROR	"Err"
#define MW_TAG_WARNING	"Warn"
#define MW_TAG_INFO	    "Info"
#define MW_TAG_DEBUG	"Debug"
#define MW_TAG_VERBOSE	"Verbose"

#define mw_log(level, tag, fmt, arg...) ({ \
	int _l = level; \
	if (((_MW_LOG_DEBUG != 0) && (_l <= MW_LOGL_DEBUG)) || \
	    (_l <= MW_LOGL_DEFAULT)) \
		printf("%s:[%s:%d]: "fmt"\n", tag, __FUNCTION__, __LINE__, ##arg); \
	})


#define mw_loge(fmt, arg...) mw_log(MW_LOGL_ERROR, MW_TAG_ERROR, "\033[40;31m"fmt"\033[0m", ##arg)
#define mw_logw(fmt, arg...) mw_log(MW_LOGL_WARNING, MW_TAG_WARNING, "\033[40;33m"fmt"\033[0m", ##arg)
#define mw_logi(fmt, arg...) mw_log(MW_LOGL_INFO, MW_TAG_INFO, "\033[40;32m"fmt"\033[0m", ##arg)
#define mw_logd(fmt, arg...) mw_log(MW_LOGL_DEBUG, MW_TAG_DEBUG, fmt, ##arg)
#define mw_logv(fmt, arg...) mw_log(MW_LOGL_VERBOSE, MW_TAG_VERBOSE, fmt, ##arg)

#if 0

#define mw_time_start(tag) unsigned int time_##tag##_start = aic_get_time_us()
#define mw_time_end(tag) unsigned int time_##tag##_end = aic_get_time_us();\
			fprintf(stderr, #tag " time: %u us\n",\
			time_##tag##_end - time_##tag##_start)
#endif

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // __MW_LOG_H__
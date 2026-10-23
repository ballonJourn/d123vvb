#ifndef __ECTINY_API_H__
#define __ECTINY_API_H__

#include "ECTypes.h"

#define ECSDK_VERSION         "1.0.1a"

typedef struct {
    void      (*onECConnectStatus)(ECConnectedStatus status, ECTransportType type);

    void      (*onMirrorStatus)(ECMirrorStatus status);

    /**
    * @brief  互联状态变更通知
    * @param  status  变更的状态
    */
    void      (*onECStatusMessage)(ECStatusMessage status);

    /**
    * @brief  需要拨打或者挂断蓝牙电话时回调
    * @param  type    操作类型
	* @param  name    电话拨打的姓名
    * @param  number  电话号码
    * @note   受限于Android/iOS系统的权限，在车机上点击互联投屏的过来的拨打电话，ECTiny无法完成电话的拨打和接听操作，因此需要依赖于车机系统的蓝牙模块完成。
    *         车机的投屏界面上点击拨打蓝牙电话时，此回调函数会把拨打的用户姓名和电话号码传到车机上，由车机蓝牙模块完成电话的拨打
    *         1.需要车机端作为蓝牙免提设备（Hand-Free-Device）。
    *         2.亿连的蓝牙电话功能，需要在车机和手机的HFP保持连接后，才可以正常启用。
    */
    void        (*onCallAction)(ECCallType type, const char *name, const char *number);

    /**
     * @brief 投屏状态变更
     * @param ECVideoInfo 视屏参数
     * @note 投屏的实际宽高、横竖屏切换时回调此函数
     */
    void        (*onMirrorInfoChanged)(const ECVideoInfo *info);


    void        (*onOTACheckResult)(const ECOTAInfo *info, int count);

    void        (*onOTAData)(unsigned char *data, unsigned int len);

    /**
    * @brief  手机下发HUD信息时回调
    * @param  data  HUD信息
    */
    void        (*onPhoneAppHUD)(const ECNavigationHudInfo *data);

    /**
     * @brief 查询手机时间
     * @param dateTime 返回手机app当前时间字符串
     * @param dateTimeLen 手机app当前时间字符串长度
     * @note 返回的时间格式为：dd.MM.yyyy HH:mm:ss:zzz
     *
     */
    void        (*onQueryTime)(const char* dateTime, unsigned int dateTimeLen);

    /**
     * @brief  回调天气信息
     * @param  data 天气信息字符串
     * @param  length 字符串长度
     * @note   data 是一个固定格式的json字符串
     */
    void        (*onWeatherReceived)(const char *data, int32_t length);
} IECCallBack;

typedef struct
{
    void    (*start)(int width, int height);
    void    (*stop)();
    void    (*play)(const void *data, unsigned int len);
}IECVideoPlayer;

typedef struct
{
    void    (*start)(ECAudioType type, const ECAudioInfo *info);
    void    (*stop)(ECAudioType type);
    void    (*play)(ECAudioType type, const void *data, unsigned int len);
    void    (*setVolume)(ECAudioType type, unsigned int vol);
}IECAudioPlayer;

typedef struct
{
    int (*start)(const ECAudioInfo *info);
    void    (*stop)();
    int (*record)(void *data, unsigned int len);
}IECAudioRecorder;

typedef struct 
{
    unsigned int (*size)(const char *path,long long *mtime);  // get file size
    void *(*op)(const char *path);                      // Open file
    void (*cl)(void *fd);                                 // Close file
    unsigned int (*rd)(void *fd, void *buf, unsigned int len);        // Read file
    unsigned int (*wr)(void *fd, const void *buf, unsigned int len);  // Write file
    unsigned int (*sk)(void *fd, unsigned int offset);                // Set file position
    int (*rm)(const char *path);                         // Delete file
}IECHttpAccessFile;

/**
 * @brief 创建配置句柄
 * @return 配置句柄，用于后续调用 EC_setCommonConfig / EC_setCommonConfig1 填入配置项
 * @note 调用完毕后必须调用 EC_destroyECConfig() 释放，避免内存泄漏。
 *       配置句柄通过 ECConfig::handle 字段传入 EC_initialize()，SDK 初始化时会读取其中的配置。
 * @see EC_destroyECConfig(), EC_setCommonConfig(), EC_setCommonConfig1()
 */
ECConfigHandle  EC_createECConfig();

/**
 * @brief 销毁配置句柄，释放相关内存
 * @param config EC_createECConfig() 返回的配置句柄
 * @note 调用 EC_initialize() 之后即可调用此函数释放，SDK 内部已复制所需配置。
 * @see EC_createECConfig()
 */
void    EC_destroyECConfig(ECConfigHandle config);

/**
 * @brief 设置字符串类型的配置项
 * @param config EC_createECConfig() 返回的配置句柄
 * @param cfgName 配置项名称
 * @param value   配置项字符串值
 * @note 支持的配置项名称参见平台文档。
 * @see EC_createECConfig(), EC_setCommonConfig1()
 */
void    EC_setCommonConfig(ECConfigHandle config, const char *cfgName, const char *value);

/**
 * @brief 设置整数类型的配置项
 * @param config  EC_createECConfig() 返回的配置句柄
 * @param cfgName 配置项名称
 * @param value   配置项整数值
 * @note 支持的配置项名称参见平台文档。
 * @see EC_createECConfig(), EC_setCommonConfig()
 */
void    EC_setCommonConfig1(ECConfigHandle config, const char *cfgName, int32_t value);

/**
 * @brief ECTiny 初始化函数
 * @param config 项目配置
 * @param listener 回调接口
 * @return EC_OK 为成功，其余值为失败。此返回值一般不处理
 * @note 此接口为互联必调用的接口。
 *       1.除了调用 EC_setBaseConfig() 设置基本的运行配置之外，config 一般不需要额外配置。
 *       2.listener 的生命周期大于 ECTiny 生命周期。即 listener 在 EC_initialize() 调用前就需要初始化，EC_release()之后才可以释放。
 */
void EC_initialize(ECConfig *config, ECMirrorConfig *mirrorConfig, IECVideoPlayer* video, IECCallBack *listener);

/**
 * @brief ECTiny 释放函数
 * @return EC_OK 为成功，其余值为失败。此返回值一般不处理
 * @note 此函数调用后，后面声明的函数调用都会失效。
 *       没有互联时，ECTiny线程处于block或者waiting状态，占用系统资源不多，所以ECTiny的一般使用场景不需要调用此函数。
 */
void EC_release();

/**
 * @brief 设置ECTiny日志
 * @param level 日志级别。调试时设置成：EC_LOG_LEVEL_ALL，生产时设置成：EC_LOG_LEVEL_ERROR或者其他高级别
 * @return EC_OK 为成功，其余值为失败。此返回值一般不处理
 * @note 此接口为互联必调用的接口。
 */
void EC_setLogInfo(const ECLogLevel level);


/**
 * @brief 绑定wifi互联ip
 * @param type 互联类型
 * @param ip 对端ip地址
 * @return EC_OK 为成功，其余值为失败。此返回值一般不处理
 * @note 此函数调用之后， ECTiny会直接连接ip，进行互联。
 * @see EC_unbindDevice()
 */
void EC_bindWIFIDevice(ECTransportType type, const char *ip);

/**
 * @brief 释放绑定的设备
 * @param type 互联类型
 * @return EC_OK 为成功，其余值为失败。此返回值一般不处理
 * @note 此函数与 EC_bindUSBDevice()，EC_bindWIFIDevice()，有绑定与解绑定关系。
 */
void EC_unbindDevice();

/**
 * @brief 设置投屏参数
 * @param mirrorCfg 投屏参数
 * @return EC_OK 为成功，其余值为失败。此返回值一般不处理
 * @note 此接口为互联必执行的接口。
 */
void EC_setMirrorConfig(const ECMirrorConfig *mirrorCfg);

/**
 * @brief 仪表wifi状态通知接口
 * @param action 仪表wifi模式：ap/sta。 目前此值不做要求，填写此枚举任意值即可
 * @param netInfo 网络状态。ECNetWorkInfo::state 必须填写，其余字段不需要填。
 * @return EC_OK 为成功，其余值为失败。此返回值一般不处理
 * @note 仪表/车机wifi发生变化时，通过此接口通知给 ECTiny。wifi连接，此接口必调用
 */
void    EC_notifyWifiStateChanged(ECWifiStateAction action, const ECNetWorkInfo* netInfo);

/**
 * @brief 开始投屏
 * @return EC_OK 为成功，其余值为失败。此返回值一般不处理
 * @note 此接口为互联必执行的接口。
 *       此接口调用后，手机app会把 H264 数据发送给 ECTiny。 ECTiny会自动调用 IECVideoPlayer 解码器解码显示。
 *       onECConnectStatus 回调函数，status==EC_CONNECT_STATUS_CONNECT_SUCCEED 时才可以调用此接口，否则无效。
 * @see EC_startMirror()
 */
void    EC_startMirror();

/**
 * @brief 停止投屏
 * @note 此接口为互联必执行的接口。
 * @see EC_stopMirror()
 */
void    EC_stopMirror();

/**
 * @brief 发送按键消息
 * @param btnCode 按键键值，取值于：ECBtnCode
 * @param type 按键类型，取值与：ECBtnEventType
 * @return EC_OK 为成功，其余值为失败。此返回值一般不处理
 * @note 发送按键消息给手机app，让手机作出响应。
 */
void EC_sendBtnEvent(int btnCode, int type);

/**
 * @brief 允许手机下发HUD导航信息
 * @param supportFunction hud导航功能，取值于 ECAPPHUDSupportFunction 类型，可以通过或运算开启多种hud功能
 * @return EC_OK 为成功，其余值为失败。此返回值一般不处理
 * @note 只有在连接层成功建立后，接口调用才能成功。参考 onECConnectStatus()
 *       此接口调用一次即可生效
 */
void EC_enableDownloadPhoneAppHud(unsigned int supportFunction);

/**
 * @brief 停止手机下发HUD导航信息
 */
void EC_disableDownloadPhoneAppHud();

void EC_checkOTA(ECOTAInfo *info, int count);

void EC_startOTA(const char* sid, int vc, int splitsize);

/**
 * @brief 生成二维码url
 * @param info 二维码信息
 * @return EC_OK 为成功，其余值为失败。
 */
const char*  EC_generateQRCodeUrl(ECQRInfo* info);

/**
 * @brief 异步查询手机时间
 * @note 返回的结果参考回调函数：IECCallBack::onQueryTime()
 */
void EC_queryTime();

/**
 * @brief 查询天气
 * @note 查询结果通过 IECCallback::onWeatherReceived 回调函数返回
 */
void EC_queryWeather();


#endif


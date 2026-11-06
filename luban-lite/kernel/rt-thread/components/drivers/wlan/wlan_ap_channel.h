/*
 * Which channels the soft AP may be started on. No dependencies, so a PC test can include it.
 *
 * The AIC8800 driver accepts (bsp/peripheral/wireless/aic8800/fdrv/layer_abstract/inc/wifi_def.h):
 *   2.4 GHz  1 .. 13
 *   5 GHz    36 40 44 48 149 153 157 161 165      (no DFS channels)
 * and picks the band from the channel number (aic8800_softap: channel >= 36 means 5 GHz).
 */
#ifndef __WLAN_AP_CHANNEL_H__
#define __WLAN_AP_CHANNEL_H__

/* The channel `wifi ap SSID PASSWORD` has always used: 2.4 GHz channel 6. */
#ifndef RT_WLAN_AP_DEFAULT_CHANNEL
#define RT_WLAN_AP_DEFAULT_CHANNEL 6
#endif

/* 1 when the driver accepts this channel number for an access point. */
static inline int rt_wlan_ap_channel_is_valid(int channel)
{
    if (channel >= 1 && channel <= 13)
        return 1;
    switch (channel)
    {
    case 36: case 40: case 44: case 48:
    case 149: case 153: case 157: case 161: case 165:
        return 1;
    default:
        return 0;
    }
}

/* 1 when the channel is in the 5 GHz band. */
static inline int rt_wlan_ap_channel_is_5g(int channel)
{
    return channel >= 36;
}

#endif /* __WLAN_AP_CHANNEL_H__ */

/*
 * Prints, for every channel number 0..300 and a few negative ones, what the access point code decides:
 * "<channel> <valid> <5g>". The header under test is the one the board compiles
 * (kernel/rt-thread/components/drivers/wlan/wlan_ap_channel.h), no stubs. Run by tests/test_wlan_channel.py.
 */
#include <stdio.h>
#include "../../../../../kernel/rt-thread/components/drivers/wlan/wlan_ap_channel.h"

int main(void)
{
    int c;

    printf("default %d %d %d\n", RT_WLAN_AP_DEFAULT_CHANNEL,
           rt_wlan_ap_channel_is_valid(RT_WLAN_AP_DEFAULT_CHANNEL), rt_wlan_ap_channel_is_5g(RT_WLAN_AP_DEFAULT_CHANNEL));
    for (c = -3; c <= 300; c++)
        printf("%d %d %d\n", c, rt_wlan_ap_channel_is_valid(c), rt_wlan_ap_channel_is_5g(c));
    return 0;
}

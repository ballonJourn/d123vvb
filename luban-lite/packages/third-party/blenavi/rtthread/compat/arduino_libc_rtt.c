/*
 * arduino_libc_rtt.c - libc pieces ArduinoCore-API expects from a port, and
 * the controller calls esp-nimble-cpp makes on non-ESP targets.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <rtthread.h>

/* newlib has itoa/utoa but not these two (declared in ArduinoCore-API api/itoa.h) */
char *ultoa(unsigned long value, char *str, int radix)
{
    char tmp[33], *p = tmp;
    if (radix < 2 || radix > 36) { *str = 0; return str; }
    do { unsigned d = value % radix; *p++ = d < 10 ? '0' + d : 'a' + d - 10; value /= radix; } while (value);
    char *o = str;
    while (p > tmp) *o++ = *--p;
    *o = 0;
    return str;
}
char *ltoa(long value, char *str, int radix)
{
    if (value < 0 && radix == 10) { str[0] = '-'; ultoa((unsigned long)(-(value + 1)) + 1, str + 1, radix); return str; }
    return ultoa((unsigned long)value, str, radix);
}

/* dtostrf: ArduinoCore-API's own implementation, compiled here as ports do */
#include "api/deprecated-avr-comp/avr/dtostrf.c.impl"

/*
 * NimBLEDevice::setPower()/getPower() call the NimBLE *controller* PHY API on
 * non-ESP targets. On D133 the controller is the AIC8800: TX power is set when
 * the AIC8800 driver loads the BT patch (aicbt_init, txpwr_lvl). Report
 * "not supported" so setPower() returns false instead of pretending.
 */
int ble_phy_tx_power_set(int dbm)
{
    (void)dbm;
    rt_kprintf("[blenavi] TX power is set by the AIC8800 BT patch (aicbt txpwr_lvl)\n");
    return -1;
}
int ble_phy_tx_power_get(void)
{
    return 0; /* unknown from the host side; 0 dBm reported */
}

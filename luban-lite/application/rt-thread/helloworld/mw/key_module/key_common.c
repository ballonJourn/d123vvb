#include <stdio.h>
#include "key_common.h"
#include "dev_config.h"

#if MW_ADC_KEY_ENABLE

static key_event_cb_t key_event_cb = NULL;

int set_key_event_cb(key_event_cb_t event_cb) {
    if (!key_event_cb) {
        if (event_cb) {
            key_event_cb = event_cb;
            return 0;
        }
        return -1;
    } else
        return 1;
}

void send_key_event(uint8_t key_event) {
    if (key_event_cb) {
        key_event_cb(key_event);
    }
}

#endif
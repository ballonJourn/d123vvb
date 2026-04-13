#include "vehicle_param.h"
#include <stddef.h>
#include <stdbool.h>
#include "app_log.h"
#include "vehicle_param.h"

#define VEH_INVALID_DATA INT32_MIN

static int32_t veh_data[VEH_DATA_END] = {0};

static bool is_valid_id(veh_data_e id) {
    return (id < VEH_DATA_END);
}

int32_t vehicle_get_data(veh_data_e id) {
    if (is_valid_id(id)) {
        return veh_data[id];
    } else {
        rt_log_info("Attempt to retrieve data for invalid ID: %u", id);
        return VEH_INVALID_DATA;
    }
}

void vehicle_set_data(veh_data_e id, int32_t value) {
    if (!is_valid_id(id)) {
        rt_log_info("Attempt to set data for invalid ID: %u", id);
        return;
    }

    if (veh_data[id] != value) {
        veh_data[id] = value;
    }
}

#include "dev_state.h"

static check_self_state_e current_state = CHECK_SELF_STATE_INIT;
static animation_state_e animation = ANIMATION_STATE_IDLE;

void set_check_self_state(check_self_state_e state) {
    current_state = state;
}

check_self_state_e get_check_self_state(void) {
    return current_state;
}

animation_state_e get_boot_animation_status(void) {
    return animation;
}

void set_boot_animation_status(animation_state_e state) {
    animation = state;
}

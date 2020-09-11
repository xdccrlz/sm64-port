#include <stdio.h>
#include <ultra64.h>

#include "controller_api.h"

static void controller_ps2_init(void) {

}

static void controller_ps2_read(OSContPad *pad) {

}

struct ControllerAPI controller_ps2 = {
    controller_ps2_init,
    controller_ps2_read
};

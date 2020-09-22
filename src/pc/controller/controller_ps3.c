#ifdef TARGET_PS3

#include <stdbool.h>
#include <ultra64.h>

#include "controller_api.h"

static void controller_ps3_init(void) {

}

static void controller_ps3_read(OSContPad *pad) {

}

struct ControllerAPI controller_ps3 = {
    controller_ps3_init,
    controller_ps3_read
};

#endif // TARGET_PS3

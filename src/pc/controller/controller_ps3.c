#ifdef TARGET_PS3

#include <stdbool.h>
#include <ultra64.h>

#include "controller_api.h"

#include <io/pad.h>

#define DEADZONE (0x20)

static bool init_ok;
static padInfo padinfo;
static padData paddata;

static void controller_ps3_init(void) {
    ioPadInit(7);

    init_ok = true;
}

static void controller_ps3_read(OSContPad *pad) {
    if (!init_ok) {
        return;
    }

    ioPadGetInfo(&padinfo);

    for (int i = 0; i < MAX_PADS; i++) {
        if (padinfo.status[i]) {
            ioPadGetData(i, &paddata);

            if (paddata.BTN_START) pad->button |= START_BUTTON;
            // Uncomment to allow either L2 or R2
            // if (paddata.BTN_L2 || paddata.BTN_R2) pad->button |= Z_TRIG;
            // Comment to remove
            if (paddata.BTN_L2) pad->button |= Z_TRIG;
            if (paddata.BTN_R1) pad->button |= L_TRIG;
            if (paddata.BTN_R1) pad->button |= R_TRIG;
            if (paddata.BTN_CROSS) pad->button |= A_BUTTON;
            if (paddata.BTN_SQUARE) pad->button |= B_BUTTON;

            const uint8_t rstick_h = ((uint8_t)paddata.ANA_R_H);
            const uint8_t rstick_v = ((uint8_t)paddata.ANA_R_V);
            if (rstick_h < 0x40) pad->button |= L_CBUTTONS;
            if (rstick_h > 0xC0) pad->button |= R_CBUTTONS;
            if (rstick_v < 0x40) pad->button |= D_CBUTTONS;
            if (rstick_v > 0xC0) pad->button |= U_CBUTTONS;

            // Uncomment to use D-pads instead
            /*
            if (paddata.BTN_LEFT) pad->button |= L_CBUTTONS;
            if (paddata.BTN_RIGHT) pad->button |= R_CBUTTONS;
            if (paddata.BTN_UP) pad->button |= U_CBUTTONS;
            if (paddata.BTN_DOWN) pad->button |= D_CBUTTONS;
            */

            const int16_t stick_h = ((uint8_t)paddata.ANA_L_H) - 0x80;
            const int16_t stick_v = 0x80 - ((uint8_t)paddata.ANA_L_V);
            const uint32_t magnitude_sq = (uint32_t)(stick_h * stick_h) + (uint32_t)(stick_v * stick_v);

            if (magnitude_sq > (uint32_t)(DEADZONE * DEADZONE)) {
                pad->stick_x = ((float)stick_h / 127.f) * 80.f;
                pad->stick_y = ((float)stick_v / 127.f) * 80.f;  
            }
        }
    }
}

static void controller_ps3_shutdown(void) {
    if (init_ok) {
        ioPadEnd();
        init_ok = false;
    }
}

struct ControllerAPI controller_ps3 = {
    controller_ps3_init,
    controller_ps3_read,
    controller_ps3_shutdown,
};

#endif // TARGET_PS3

#if defined(TARGET_XBOX)

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <hal/input.h>

#include <ultra64.h>

#include "controller_api.h"

#define STICK_DEADZONE 4960
#define BUTTON_DEADZONE 0x20

extern void USBGetEvents(void);

static inline bool abutton_pressed(const XPAD_INPUT *pad, const uint32_t idx) {
    return pad->CurrentButtons.ucAnalogButtons[idx] > BUTTON_DEADZONE;
}

static inline bool dbutton_pressed(const XPAD_INPUT *pad, const uint32_t mask) {
    return (pad->CurrentButtons.usDigitalButtons & mask) != 0;
}

static void controller_xbox_init(void) {
    XInput_Init();
}

static void controller_xbox_read(OSContPad *pad) {
    USBGetEvents();

    XInput_GetEvents();

    XPAD_INPUT *xpad = NULL;
    for (int i = 0; i < XInputGetPadCount(); ++i) {
        if (g_Pads[i].hPresent) {
            xpad = g_Pads + i;
            break;
        }
    }

    if (!xpad) return;

    if (abutton_pressed(xpad, XPAD_A)) pad->button |= A_BUTTON;
    if (abutton_pressed(xpad, XPAD_X)) pad->button |= B_BUTTON;
    if (abutton_pressed(xpad, XPAD_WHITE)) pad->button |= L_TRIG;
    if (abutton_pressed(xpad, XPAD_BLACK)) pad->button |= R_TRIG;
    if (abutton_pressed(xpad, XPAD_LEFT_TRIGGER) || abutton_pressed(xpad, XPAD_RIGHT_TRIGGER))
        pad->button |= Z_TRIG;

    if (dbutton_pressed(xpad, XPAD_START)) pad->button |= START_BUTTON;

    const int16_t lx = xpad->sLThumbX;
    const int16_t ly = xpad->sLThumbY;
    const int16_t rx = xpad->sRThumbX;
    const int16_t ry = xpad->sRThumbY;

    if (rx < -0x4000) pad->button |= L_CBUTTONS;
    if (rx >  0x4000) pad->button |= R_CBUTTONS;
    if (ry < -0x4000) pad->button |= D_CBUTTONS;
    if (ry >  0x4000) pad->button |= U_CBUTTONS;

    const uint32_t magnitude_sq = (uint32_t)(lx * lx) + (uint32_t)(ly * ly);
    if (magnitude_sq > (uint32_t)(STICK_DEADZONE * STICK_DEADZONE)) {
        pad->stick_x = lx / 0x100;
        pad->stick_y = ly / 0x100;
    }
}

struct ControllerAPI controller_xbox = {
    controller_xbox_init,
    controller_xbox_read
};

#endif

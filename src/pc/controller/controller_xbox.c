#if defined(TARGET_XBOX)

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <hal/xbox.h>
#include <hal/input.h>

#include <ultra64.h>

#include "controller_api.h"

#define STICK_DEADZONE 8000
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

    const bool xpad_black = abutton_pressed(xpad, XPAD_BLACK);
    const bool xpad_ltrig = abutton_pressed(xpad, XPAD_LEFT_TRIGGER);
    const bool xpad_rtrig = abutton_pressed(xpad, XPAD_RIGHT_TRIGGER);

    // reboot with the usual "drop out to the dashboard" combination
    if (dbutton_pressed(xpad, XPAD_BACK) && xpad_black && xpad_ltrig && xpad_rtrig)
        XReboot();

    if (abutton_pressed(xpad, XPAD_A)) pad->button |= A_BUTTON;
    if (abutton_pressed(xpad, XPAD_X)) pad->button |= B_BUTTON;
    if (abutton_pressed(xpad, XPAD_WHITE)) pad->button |= L_TRIG;
    if (xpad_black) pad->button |= R_TRIG;
    if (xpad_ltrig || xpad_rtrig) pad->button |= Z_TRIG;

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

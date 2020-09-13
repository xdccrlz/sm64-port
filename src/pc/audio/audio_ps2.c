#ifdef TARGET_PS2

#include <stdio.h>
#include <kernel.h>
#include <sifrpc.h>
#include <iopheap.h>
#include <loadfile.h>

#include "audsrv.h"

#include "macros.h"
#include "audio_api.h"

extern unsigned int size_ps2_freesd_irx;
extern unsigned char ps2_freesd_irx;

extern unsigned int size_ps2_audsrv_irx;
extern unsigned char ps2_audsrv_irx;

static int spu2_init(void) {
    int id, error;

    // load freesd
    id = SifExecModuleBuffer(&ps2_freesd_irx, size_ps2_freesd_irx, 0, NULL, &error);
    if (id < 0 || error < 0) {
        printf("audio_ps2: failed to load freesd: id %d error %d\n", id, error);
        return -1;
    }

    printf("audio_ps2: load freesd id %d\n", id);

    // load audsrv
    id = SifExecModuleBuffer(&ps2_audsrv_irx, size_ps2_audsrv_irx, 0, NULL, &error);
    if (id < 0 || error < 0) {
        printf("audio_ps2: failed to load audsrv: id %d error %d\n", id, error);
        return -1;
    }

    printf("audio_ps2: load audsrv id %d\n", id);

    audsrv_init();

    printf("audio_ps2: init audsrv\n");

    return 0;
}

static bool audio_ps2_init(void) {
    if (spu2_init()) return false;

    audsrv_fmt_t fmt;

    fmt.freq = 32000;
    fmt.bits = 16;
    fmt.channels = 2;

    if (audsrv_set_format(&fmt)) {
        printf("audio_ps2: unsupported sound format\n");
        audsrv_quit();
        return false;
    }

    return true;
}

static int audio_ps2_buffered(void) {
    return audsrv_queued() / 4;
}

static int audio_ps2_get_desired_buffered(void) {
    return 1100;
}

static void audio_ps2_play(const uint8_t *buf, size_t len) {
    if (audio_ps2_buffered() < 6000)
        audsrv_play_audio(buf, len);
}

struct AudioAPI audio_ps2 = {
    audio_ps2_init,
    audio_ps2_buffered,
    audio_ps2_get_desired_buffered,
    audio_ps2_play
};

#endif // TARGET_PS2

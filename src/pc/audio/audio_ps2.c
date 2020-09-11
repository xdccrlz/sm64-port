#ifdef TARGET_PS2

#include "macros.h"
#include "audio_api.h"

static bool audio_ps2_init(void) {
    return true;
}

static int audio_ps2_buffered(void) {
    return 0;
}

static int audio_ps2_get_desired_buffered(void) {
    return 0;
}

static void audio_ps2_play(UNUSED const uint8_t *buf, UNUSED size_t len) {
}

struct AudioAPI audio_ps2 = {
    audio_ps2_init,
    audio_ps2_buffered,
    audio_ps2_get_desired_buffered,
    audio_ps2_play
};

#endif // TARGET_PS2

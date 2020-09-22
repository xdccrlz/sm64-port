#ifdef TARGET_PS3

#include "macros.h"
#include "audio_api.h"

static bool audio_ps3_init(void) {
    return true;
}

static int audio_ps3_buffered(void) {
    return 0;
}

static int audio_ps3_get_desired_buffered(void) {
    return 0;
}

static void audio_ps3_play(UNUSED const uint8_t *buf, UNUSED size_t len) {
}

struct AudioAPI audio_ps3 = {
    audio_ps3_init,
    audio_ps3_buffered,
    audio_ps3_get_desired_buffered,
    audio_ps3_play
};

#endif // TARGET_PS3

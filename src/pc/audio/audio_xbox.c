#include "macros.h"
#include "audio_api.h"

static bool audio_xbox_init(void) {
    return true;
}

static int audio_xbox_buffered(void) {
    return 0;
}

static int audio_xbox_get_desired_buffered(void) {
    return 0;
}

static void audio_xbox_play(UNUSED const uint8_t *buf, UNUSED size_t len) {
}

struct AudioAPI audio_xbox = {
    audio_xbox_init,
    audio_xbox_buffered,
    audio_xbox_get_desired_buffered,
    audio_xbox_play
};

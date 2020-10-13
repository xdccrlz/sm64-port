#ifdef TARGET_PS3

#include <stdlib.h>
#include <string.h>
#include <audio/audio.h>
#include <sys/mutex.h>
#include <sys/thread.h>

#include "macros.h"
#include "audio_api.h"

#define SNDPACKETLEN (8 * 1024)

#define NUM_SAMPLES_32KHZ 171 // (32000 / 48000) * AUDIO_BLOCK_SAMPLES

/** lut for converting 32000 -> 48000 */
static const u16 up_32000_lut[256] = {
    0x0000, 0x0000, 0x0001, 0x0002, 0x0002, 0x0003, 0x0004, 0x0004,
    0x0005, 0x0006, 0x0006, 0x0007, 0x0008, 0x0008, 0x0009, 0x000a,
    0x000a, 0x000b, 0x000c, 0x000c, 0x000d, 0x000e, 0x000e, 0x000f,
    0x0010, 0x0010, 0x0011, 0x0012, 0x0012, 0x0013, 0x0014, 0x0014,
    0x0015, 0x0016, 0x0016, 0x0017, 0x0018, 0x0018, 0x0019, 0x001a,
    0x001a, 0x001b, 0x001c, 0x001c, 0x001d, 0x001e, 0x001e, 0x001f,
    0x0020, 0x0020, 0x0021, 0x0022, 0x0022, 0x0023, 0x0024, 0x0024,
    0x0025, 0x0026, 0x0026, 0x0027, 0x0028, 0x0028, 0x0029, 0x002a,
    0x002a, 0x002b, 0x002c, 0x002c, 0x002d, 0x002e, 0x002e, 0x002f,
    0x0030, 0x0030, 0x0031, 0x0032, 0x0032, 0x0033, 0x0034, 0x0034,
    0x0035, 0x0036, 0x0036, 0x0037, 0x0038, 0x0038, 0x0039, 0x003a,
    0x003a, 0x003b, 0x003c, 0x003c, 0x003d, 0x003e, 0x003e, 0x003f,
    0x0040, 0x0040, 0x0041, 0x0042, 0x0042, 0x0043, 0x0044, 0x0044,
    0x0045, 0x0046, 0x0046, 0x0047, 0x0048, 0x0048, 0x0049, 0x004a,
    0x004a, 0x004b, 0x004c, 0x004c, 0x004d, 0x004e, 0x004e, 0x004f,
    0x0050, 0x0050, 0x0051, 0x0052, 0x0052, 0x0053, 0x0054, 0x0054,
    0x0055, 0x0056, 0x0056, 0x0057, 0x0058, 0x0058, 0x0059, 0x005a,
    0x005a, 0x005b, 0x005c, 0x005c, 0x005d, 0x005e, 0x005e, 0x005f,
    0x0060, 0x0060, 0x0061, 0x0062, 0x0062, 0x0063, 0x0064, 0x0064,
    0x0065, 0x0066, 0x0066, 0x0067, 0x0068, 0x0068, 0x0069, 0x006a,
    0x006a, 0x006b, 0x006c, 0x006c, 0x006d, 0x006e, 0x006e, 0x006f,
    0x0070, 0x0070, 0x0071, 0x0072, 0x0072, 0x0073, 0x0074, 0x0074,
    0x0075, 0x0076, 0x0076, 0x0077, 0x0078, 0x0078, 0x0079, 0x007a,
    0x007a, 0x007b, 0x007c, 0x007c, 0x007d, 0x007e, 0x007e, 0x007f,
    0x0080, 0x0080, 0x0081, 0x0082, 0x0082, 0x0083, 0x0084, 0x0084,
    0x0085, 0x0086, 0x0086, 0x0087, 0x0088, 0x0088, 0x0089, 0x008a,
    0x008a, 0x008b, 0x008c, 0x008c, 0x008d, 0x008e, 0x008e, 0x008f,
    0x0090, 0x0090, 0x0091, 0x0092, 0x0092, 0x0093, 0x0094, 0x0094,
    0x0095, 0x0096, 0x0096, 0x0097, 0x0098, 0x0098, 0x0099, 0x009a,
    0x009a, 0x009b, 0x009c, 0x009c, 0x009d, 0x009e, 0x009e, 0x009f,
    0x00a0, 0x00a0, 0x00a1, 0x00a2, 0x00a2, 0x00a3, 0x00a4, 0x00a4,
    0x00a5, 0x00a6, 0x00a6, 0x00a7, 0x00a8, 0x00a8, 0x00a9, 0x00aa,
};

// this is basically SDL_dataqueue but slightly less generic

typedef struct sndpacket {
    size_t datalen;         /* bytes currently in use in this packet. */
    size_t startpos;        /* bytes currently consumed in this packet. */
    struct sndpacket *next; /* next item in linked list. */
    u8 data[];              /* packet data */
} sndpacket_t;

static sndpacket_t *qhead;
static sndpacket_t *qtail;
static sndpacket_t *qpool;
static size_t queued;

static u32 aport;
static audioPortParam aparams;
static audioPortConfig aconfig;

static u64 akey;
static sys_event_queue_t aqueue;
static sys_ppu_thread_t athread;
static sys_mutex_t amutex;

static volatile bool audio_running = false;

static void sndqueue_init(const size_t bufsize) {
    const size_t wantpackets = (bufsize + (SNDPACKETLEN - 1)) / SNDPACKETLEN;
    for (size_t i = 0; i < wantpackets; ++i) {
        sndpacket_t *packet = malloc(sizeof(sndpacket_t) + SNDPACKETLEN);
        if (packet) {
            packet->datalen = 0;
            packet->startpos = 0;
            packet->next = qpool;
            qpool = packet;
        }
    }
}

static size_t sndqueue_read(void *buf, size_t len) {
    sndpacket_t *packet;
    u8 *ptr = buf;

    while ((len > 0) && ((packet = qhead) != NULL)) {
        const size_t avail = packet->datalen - packet->startpos;
        const size_t tx = (len < avail) ? len : avail;

        memcpy(ptr, packet->data + packet->startpos, tx);
        packet->startpos += tx;
        ptr += tx;
        queued -= tx;
        len -= tx;

        if (packet->startpos == packet->datalen) {
            qhead = packet->next;
            packet->next = qpool;
            qpool = packet;
        }
    }

    if (qhead == NULL)
        qtail = NULL;

    return (size_t)(ptr - (u8*)buf);
}

static inline sndpacket_t *alloc_sndpacket(void) {
    sndpacket_t *packet = qpool;

    if (packet) {
        qpool = packet->next;
    } else {
        packet = malloc(sizeof(sndpacket_t) + SNDPACKETLEN);
        if (!packet) return NULL;
    }

    packet->datalen = 0;
    packet->startpos = 0;
    packet->next = NULL;

    if (qtail == NULL)
        qhead = packet;
    else
        qtail->next = packet;
    qtail = packet;

    return packet;
}

static int sndqueue_push(const void *data, size_t len) {
    sndpacket_t *origtail = qtail;
    const u8 *ptr = data;

    while (len > 0) {
        sndpacket_t *packet = qtail;
        if (!packet || (packet->datalen >= SNDPACKETLEN)) {
            packet = alloc_sndpacket();
            if (!packet) {
                // out of memory, fuck everything
                return -1;
            }
        }

        const size_t room = SNDPACKETLEN - packet->datalen;
        const size_t datalen = (len < room) ? len : room;
        memcpy(packet->data + packet->datalen, ptr, datalen);
        ptr += datalen;
        len -= datalen;
        packet->datalen += datalen;
        queued += datalen;
    }

    return 0;
}

static void convert_block(f32 *out, const s16 *in) {
    for (int p = 0; p < AUDIO_BLOCK_SAMPLES; p++) {
        const int q = up_32000_lut[p] << 1;
        *out++ = in[q + 0] / 32768.f;
        *out++ = in[q + 1] / 32768.f;
    }
}

static void drain_block(f32 *buf) {
    s16 block[NUM_SAMPLES_32KHZ * 2] = { 0 };
    sysMutexLock(amutex, 30000);
    sndqueue_read(block, sizeof(block));
    convert_block(buf, block);
    sysMutexUnlock(amutex);
}

static void audio_thread(UNUSED void *arg) {
    sys_event_t event;

    while (audio_running) {
        sysEventQueueReceive(aqueue, &event, 30000);
        const u64 current_block = *(u64*)((u64)aconfig.readIndex);
        f32 *data_start = (f32*)((u64)aconfig.audioDataStart);
        const u32 audio_block_index = (current_block + 1) % aconfig.numBlocks;
        f32 *buf = data_start + aconfig.channelCount * AUDIO_BLOCK_SAMPLES * audio_block_index;
        drain_block(buf);
    }

    sysThreadExit(0);
}

static bool audio_ps3_init(void) {
    audioInit();

    aparams.numChannels = AUDIO_PORT_2CH;
    aparams.numBlocks = AUDIO_BLOCK_8;
    aparams.attrib = AUDIO_PORT_INITLEVEL;
    aparams.level = 0.75f;

    audioPortOpen(&aparams, &aport);
    audioGetPortConfig(aport, &aconfig);

    audioCreateNotifyEventQueue(&aqueue, &akey);
    audioSetNotifyEventQueue(akey);
    sysEventQueueDrain(aqueue);

    audioPortStart(aport);

    sys_mutex_attr_t mattr;
    sysMutexAttrInitialize(mattr);
    sysMutexCreate(&amutex, &mattr);

    audio_running = true;

    sysThreadCreate(&athread, audio_thread, NULL, 1500, 0x10000, THREAD_JOINABLE, "sm64 audio");

    return true;
}

static int audio_ps3_buffered(void) {
    sysMutexLock(amutex, 30000);
    int ret = queued / 4;
    sysMutexUnlock(amutex);
    return ret;
}

static int audio_ps3_get_desired_buffered(void) {
    return 1100;
}

static void audio_ps3_play(const uint8_t *buf, size_t len) {
    sysMutexLock(amutex, 30000);
    if (queued / 4 < 6000)
        sndqueue_push(buf, len);
    sysMutexUnlock(amutex);
}

static void audio_ps3_shutdown(void) {
    u64 retval;
    audio_running = false;
    sysThreadJoin(athread, &retval);
    sysMutexDestroy(amutex);

    audioPortStop(aport);
    audioRemoveNotifyEventQueue(akey);
    audioPortClose(aport);
    sysEventQueueDestroy(aqueue, 0);
    audioQuit();
}

struct AudioAPI audio_ps3 = {
    audio_ps3_init,
    audio_ps3_buffered,
    audio_ps3_get_desired_buffered,
    audio_ps3_play,
    audio_ps3_shutdown,
};

#endif // TARGET_PS3

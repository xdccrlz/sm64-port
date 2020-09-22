#if defined(TARGET_PS3)

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <malloc.h>
#include <unistd.h>

#include <ppu-types.h>
#include <rsx/rsx.h>
#include <sysutil/video.h>
#include <sys/process.h>

#include "../compat.h"
#include "gfx_window_manager_api.h"
#include "gfx_screen_config.h"

SYS_PROCESS_PARAM(1001, 0x100000);

#define DEFUALT_CB_SIZE     0x80000
#define HOST_STATE_CB_SIZE  0x10000
#define HOST_ADDR_ALIGNMENT (1024*1024)
#define HOSTBUFFER_SIZE     (128*1024*1024)
#define FRAME_BUFFER_COUNT  2

#define GCM_LABEL_INDEX 255

static gcmSurface gcm_surf[FRAME_BUFFER_COUNT];
static u32 *gcm_framebuf[FRAME_BUFFER_COUNT];
static u32 *gcm_zbuf;

gcmContextData *rsx_ctx = NULL;
static void *rsx_buf = NULL;

static u32 write_label = 1;

videoResolution vid_mode;
static int vid_mode_num = -1;

static const u32 vid_mode_ids[] = {
    VIDEO_RESOLUTION_1080,
    VIDEO_RESOLUTION_720,
    VIDEO_RESOLUTION_480,
    VIDEO_RESOLUTION_576
};

static const u32 vid_num_modes = sizeof(vid_mode_ids) / sizeof(vid_mode_ids[0]);

static u32 cur_fb = 0;
static bool first_fb = true;

void rsx_wait_finish(void) {
    rsxSetWriteBackendLabel(rsx_ctx, GCM_LABEL_INDEX, write_label);

    rsxFlushBuffer(rsx_ctx);

    while(*(vu32*)gcmGetLabelAddress(GCM_LABEL_INDEX) != write_label)
        usleep(30);

    ++write_label;
}

static inline void rsx_wait_idle(void) {
    rsxSetWriteBackendLabel(rsx_ctx, GCM_LABEL_INDEX, write_label);
    rsxSetWaitLabel(rsx_ctx, GCM_LABEL_INDEX, write_label);

    ++write_label;

    rsx_wait_finish();
}

static inline void rsx_wait_flip(void) {
    while(gcmGetFlipStatus()) usleep(200);
    gcmResetFlipStatus();
}

static inline void rsx_prepare_rendertarget(gcmSurface *sf, const u32 c_ofs, const u32 c_pitch, const u32 z_ofs, const u32 z_pitch, const u32 w, const u32 h) {
    sf->colorFormat      = GCM_SURFACE_X8R8G8B8;
    sf->colorTarget      = GCM_SURFACE_TARGET_0;
    sf->colorLocation[0] = GCM_LOCATION_RSX;
    sf->colorOffset[0]   = c_ofs;
    sf->colorPitch[0]    = c_pitch;

    sf->colorLocation[1] = GCM_LOCATION_RSX;
    sf->colorLocation[2] = GCM_LOCATION_RSX;
    sf->colorLocation[3] = GCM_LOCATION_RSX;
    sf->colorOffset[1]   = 0;
    sf->colorOffset[2]   = 0;
    sf->colorOffset[3]   = 0;
    sf->colorPitch[1]    = 64;
    sf->colorPitch[2]    = 64;
    sf->colorPitch[3]    = 64;

    sf->depthFormat      = GCM_SURFACE_ZETA_Z24S8;
    sf->depthLocation    = GCM_LOCATION_RSX;
    sf->depthOffset      = z_ofs;
    sf->depthPitch       = z_pitch;

    sf->type             = GCM_SURFACE_TYPE_LINEAR;
    sf->antiAlias        = GCM_SURFACE_CENTER_1;

    sf->width            = w;
    sf->height           = h;
    sf->x                = 0;
    sf->y                = 0;
}

static inline bool vid_set_mode(const u32 mode_id) {
    int ret = videoGetResolutionAvailability(VIDEO_PRIMARY, mode_id, VIDEO_ASPECT_AUTO, 0);
    if (ret != 1) return false;

    ret = videoGetResolution(mode_id, &vid_mode);
    if (ret) {
        printf("vid_set_mode: could not get mode info for mode %u\n", mode_id);
        return false;
    }

    videoConfiguration config = {
        (u8)mode_id,
        VIDEO_BUFFER_FORMAT_XRGB,
        VIDEO_ASPECT_AUTO,
        { 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        (u32)vid_mode.width * 4
    };

    ret = videoConfigure(VIDEO_PRIMARY, &config, NULL, 0);
    if (ret) printf("vid_set_mode: videoConfigure failed on mode %u\n", mode_id);

    return !ret;
}

static void gfx_ps3_init(const char *game_name, bool start_in_fullscreen) {
    rsx_buf = memalign(HOST_ADDR_ALIGNMENT, HOSTBUFFER_SIZE);
    if (!rsx_buf) {
        printf("gfx_ps3_init: alloc failed (%d bytes)\n", HOSTBUFFER_SIZE);
        exit(1);
    }

    rsxInit(&rsx_ctx, DEFUALT_CB_SIZE, HOSTBUFFER_SIZE, rsx_buf);

    // pick first available resolution
    for (u32 i = 0; i < vid_num_modes; ++i) {
        if (vid_set_mode(vid_mode_ids[i])) {
            vid_mode_num = (int)i;
            break;
        }
    }

    if (vid_mode_num < 0) {
        printf("gfx_ps3_init: failed to set any video mode\n");
        exit(1);
    }

    rsx_wait_idle();

    gcmSetFlipMode(GCM_FLIP_VSYNC);

    printf("gfx_ps3_init: set mode %dx%d\n", vid_mode.width, vid_mode.height);

    const u32 z_size = 4;
    const u32 color_size = 4;

    const u32 z_pitch = vid_mode.width * z_size;
    const u32 color_pitch = vid_mode.width * color_size;

    u32 color_ofs[FRAME_BUFFER_COUNT];;
    for (u32 i = 0; i < FRAME_BUFFER_COUNT; ++i) {
        gcm_framebuf[i] = (u32 *)rsxMemalign(64, vid_mode.height * color_pitch);
        rsxAddressToOffset(gcm_framebuf[i], &color_ofs[i]);
        gcmSetDisplayBuffer(i, color_ofs[i], color_pitch, vid_mode.width, vid_mode.height);
    }

    u32 z_ofs;
    gcm_zbuf = (u32 *)rsxMemalign(64, vid_mode.height * z_pitch);
    rsxAddressToOffset(gcm_zbuf, &z_ofs);

    for (u32 i = 0; i < FRAME_BUFFER_COUNT; ++i)
        rsx_prepare_rendertarget(&gcm_surf[i], color_ofs[i], color_pitch, z_ofs, z_pitch, vid_mode.width, vid_mode.height);

    printf("gfx_ps3_init: init complete\n");
}

static void gfx_ps3_set_fullscreen_changed_callback(void (*on_fullscreen_changed)(bool is_now_fullscreen)) {

}

static void gfx_ps3_set_fullscreen(bool enable) {

}

static void gfx_ps3_set_keyboard_callbacks(bool (*on_key_down)(int scancode), bool (*on_key_up)(int scancode), void (*on_all_keys_up)(void)) {

}

static void gfx_ps3_main_loop(void (*run_one_game_iter)(void)) {
    run_one_game_iter();
}

static void gfx_ps3_get_dimensions(uint32_t *width, uint32_t *height) {
    *width = vid_mode.width;
    *height = vid_mode.height;
}

static void gfx_ps3_handle_events(void) {

}

static bool gfx_ps3_start_frame(void) {
    return true;
}

static void gfx_ps3_swap_buffers_begin(void) {
    if (first_fb) gcmResetFlipStatus();
    else rsx_wait_flip();

    gcmSetFlip(rsx_ctx, cur_fb);
    rsxFlushBuffer(rsx_ctx);
    gcmSetWaitFlip(rsx_ctx);

    first_fb = false;
    cur_fb ^= 1;

    rsxSetSurface(rsx_ctx, &gcm_surf[cur_fb]);
}

static void gfx_ps3_swap_buffers_end(void) {

}

static double gfx_ps3_get_time(void) {
    return 0.0;
}

struct GfxWindowManagerAPI gfx_ps3_wapi = {
    gfx_ps3_init,
    gfx_ps3_set_keyboard_callbacks,
    gfx_ps3_set_fullscreen_changed_callback,
    gfx_ps3_set_fullscreen,
    gfx_ps3_main_loop,
    gfx_ps3_get_dimensions,
    gfx_ps3_handle_events,
    gfx_ps3_start_frame,
    gfx_ps3_swap_buffers_begin,
    gfx_ps3_swap_buffers_end,
    gfx_ps3_get_time
};

#endif

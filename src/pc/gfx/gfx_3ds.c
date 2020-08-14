#ifdef TARGET_N3DS

#include "macros.h"

#include "gfx_3ds.h"

C3D_RenderTarget *gTarget, *gTargetRight;
float gSliderLevel;

Gfx3DSMode gGfx3DSMode;
PrintConsole gConsole;

static bool checkN3DS()
{
    bool isNew3DS = false;
    if (R_SUCCEEDED(APT_CheckNew3DS(&isNew3DS)))
        return isNew3DS;

    return false;
}

static void gfx_3ds_init(UNUSED const char *game_name, UNUSED bool start_in_fullscreen)
{
    if (checkN3DS())
        osSetSpeedupEnable(true);

    gfxInitDefault();
    consoleSelect(consoleInit(GFX_BOTTOM, &gConsole));

    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);

    bool useAA = false;
#ifdef N3DS_USE_ANTIALIASING
    useAA = true;
#endif
    bool useWide = false;
#ifdef N3DS_USE_WIDE_800PX
    u8 model;
    CFGU_GetSystemModel(&model);
    useWide = model != 3; //wide is not possible on o2ds
#endif

    u32 transferFlags =
        GX_TRANSFER_FLIP_VERT(0) |
        GX_TRANSFER_OUT_TILED(0) |
        GX_TRANSFER_RAW_COPY(0) |
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
        GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8);

    if (useAA && !useWide)
        transferFlags |= GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_XY);
    else if (useAA && useWide)
        transferFlags |= GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_X);
    else
        transferFlags |= GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);

    int width = useAA || useWide ? 800 : 400;
    int height = useAA ? 480 : 240;

    gTarget  = C3D_RenderTargetCreate(height, width, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    C3D_RenderTargetSetOutput(gTarget,  GFX_TOP, GFX_LEFT,  transferFlags);

    #ifndef N3DS_USE_WIDE_800PX
        gTargetRight = C3D_RenderTargetCreate(height, width, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
        C3D_RenderTargetSetOutput(gTargetRight, GFX_TOP, GFX_RIGHT, transferFlags);
    #endif

    if (!useAA && !useWide)
        gGfx3DSMode = GFX_3DS_MODE_NORMAL;
    else if (useAA && !useWide)
        gGfx3DSMode = GFX_3DS_MODE_AA_22;
    else if (!useAA && useWide)
        gGfx3DSMode = GFX_3DS_MODE_WIDE;
    else
        gGfx3DSMode = GFX_3DS_MODE_WIDE_AA_12;

    if (useWide)
        gfxSetWide(true);
    else
        gfxSet3D(true);
}

static void gfx_set_keyboard_callbacks(UNUSED bool (*on_key_down)(int scancode), UNUSED bool (*on_key_up)(int scancode), UNUSED void (*on_all_keys_up)(void))
{
}

static void gfx_set_fullscreen_changed_callback(UNUSED void (*on_fullscreen_changed)(bool is_now_fullscreen))
{
}

static void gfx_set_fullscreen(UNUSED bool enable)
{
}

static void gfx_3ds_main_loop(void (*run_one_game_iter)(void))
{
    while (aptMainLoop())
        run_one_game_iter();

    ndspExit();
    C3D_Fini();
    gfxExit();
}

static void gfx_3ds_get_dimensions(uint32_t *width, uint32_t *height)
{
    *width = 400;
    *height = 240;
}

static void gfx_3ds_handle_events(void)
{
    // as good a time as any
    gSliderLevel = osGet3DSliderState();
}

static bool gfx_3ds_start_frame(void)
{
    return true;
}

static void gfx_3ds_swap_buffers_begin(void)
{
    // do this in citro.c
}

static void gfx_3ds_swap_buffers_end(void)
{
}

static double gfx_3ds_get_time(void)
{
    return 0.0;
}

struct GfxWindowManagerAPI gfx_3ds =
{
    gfx_3ds_init,
    gfx_set_keyboard_callbacks,
    gfx_set_fullscreen_changed_callback,
    gfx_set_fullscreen,
    gfx_3ds_main_loop,
    gfx_3ds_get_dimensions,
    gfx_3ds_handle_events,
    gfx_3ds_start_frame,
    gfx_3ds_swap_buffers_begin,
    gfx_3ds_swap_buffers_end,
    gfx_3ds_get_time
};

#endif

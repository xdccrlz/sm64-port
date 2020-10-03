#ifdef TARGET_XBOX

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <malloc.h>
#include <math.h>
#include <string.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <pbkit/pbkit.h>
#include <xgu/xgu.h>
#include <xgu/xgux.h>
#include <hal/debug.h>

#ifndef _LANGUAGE_C
# define _LANGUAGE_C
#endif
#include <PR/gbi.h>

#include "gfx_rendering_api.h"

#include "gfx_xbox.h"
#include "gfx_cc.h"
#include "macros.h"

#include "swizzle.h"

#define NV_TEX_FILTER_ANISO  4
#define NV_TEX_FILTER_LINEAR 2
#define NV_TEX_FILTER_NONE   1

#define MAX_SHADERS 64
#define MAX_TEXTURES 512
#define MAX_ATTRIBS 8
#define MAX_VERTS (1024 * 3)

#define TEXCACHE_SIZE (512 * 64 * 32 * 4)
#define VTXBUF_FLOATS (MAX_VERTS * 32)

extern int win_width;
extern int win_height;

typedef uint32_t *(*combiner_fn_t)(uint32_t *);

struct CompiledShader {
    const uint32_t shader_id;
    const XguTransformProgramInstruction *vp_inst;
    const uint32_t *vp_size;
    combiner_fn_t fp_combiner;
};

// this contains `struct CompiledShader shader_objs[]` and `u32 num_shader_objs`
#include "xbox_shader_db.h"

struct VertexAttrib {
    uint32_t ofs;
    uint32_t type;
    uint32_t size;
};

struct ShaderProgram {
    uint32_t shader_id;
    struct CCFeatures cc;

    const struct CompiledShader *prog;

    struct VertexAttrib attr[MAX_ATTRIBS];
    uint32_t num_attrs;
    uint32_t stride;
};

struct Texture {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t wshift;
    uint32_t hshift;
    uint32_t size;
    uint32_t wrap_u;
    uint32_t wrap_v;
    uint32_t filter;
    uint32_t format;
    uint32_t addr;
    uint8_t *data;
};

struct Viewport {
    float cx, cy;
    float hw, hh;
};

static struct ShaderProgram shader_program_pool[MAX_SHADERS];
static uint32_t num_shaders;
static struct ShaderProgram *cur_shader = NULL;

static struct Texture tex_pool[MAX_TEXTURES];
static uint32_t num_textures;
static struct Texture *cur_tex[2] = { NULL, NULL };
static struct Texture *last_tex = NULL;
static uint32_t last_tile = 0;

static uint8_t *tex_cache = NULL;
static uint8_t *tex_cache_ptr = NULL;
static uint8_t *tex_cache_end = NULL;

static XguMatrix4x4 mat_viewport;
static bool do_blend = false;
static bool z_test = true;
static bool z_write = true;
static bool z_decal = false;

static float *vtx_buf;
static float *vtx_buf_ptr;
static float *vtx_buf_end;

static uint32_t *cmd;

/* from stackoverflow.com/a/11398748 */

static int log2_u32(uint32_t value) {
    static const int tab32[32] = {
         0,  9,  1, 10, 13, 21,  2, 29,
        11, 14, 16, 18, 22, 25,  3, 30,
         8, 12, 20, 28, 15, 17, 24,  7,
        19, 27, 23,  6, 26,  5,  4, 31
    };
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    return tab32[(uint32_t)(value*0x07C4ACDD) >> 27];
}

/* from Voxel9/xbox-xgu-examples */

static inline void mtx_viewport(XguMatrix4x4 *mtx_out, float x, float y, float width, float height, float z_min, float z_max) {
    mtx_out->f[0]  = width / 2.0f;
    mtx_out->f[5]  = height / -2.0f;
    mtx_out->f[10] = z_max - z_min;
    mtx_out->f[15] = 1.0f;
    mtx_out->f[12] = x + width / 2.0f;
    mtx_out->f[13] = y + height / 2.0f;
    mtx_out->f[14] = z_min;
}

static inline void draw_set_texture(const uint32_t i) {
    if (cur_tex[i]) {
        cmd = xgu_set_texture_offset(cmd, i, (const void *)cur_tex[i]->addr);
        cmd = xgu_set_texture_format(cmd, i, 2, false, XGU_SOURCE_COLOR, 2, cur_tex[i]->format, 1, cur_tex[i]->wshift, cur_tex[i]->hshift, 0);
        cmd = xgu_set_texture_address(cmd, i, cur_tex[i]->wrap_u, false, cur_tex[i]->wrap_v, false, XGU_CLAMP_TO_EDGE, false, false);
        cmd = xgu_set_texture_control0(cmd, i, true, 0, 0);
        cmd = xgu_set_texture_filter(cmd, i, 0, XGU_TEXTURE_CONVOLUTION_QUINCUNX, cur_tex[i]->filter, cur_tex[i]->filter, false, false, false, false);
    } else {
        cmd = xgu_set_texture_control0(cmd, i, false, 0, 0);
    }
}

static inline void draw_set_blending(const bool do_blend) {
    cmd = xgu_set_blend_enable(cmd, do_blend);
    cmd = xgu_set_blend_func_sfactor(cmd, XGU_FACTOR_SRC_ALPHA);
    cmd = xgu_set_blend_func_dfactor(cmd, XGU_FACTOR_ONE_MINUS_SRC_ALPHA);
}

static inline void draw_set_ztest(const bool z_test) {
    cmd = xgu_set_depth_test_enable(cmd, z_test);
    cmd = xgu_set_depth_func(cmd, XGU_FUNC_LESS_OR_EQUAL);
}

static inline void draw_set_zmask(const bool z_mask) {
    cmd = xgu_set_depth_mask(cmd, z_mask);
}

static inline void draw_set_vertex_shader(const XguTransformProgramInstruction *inst, const uint32_t size) {
    cmd = pb_begin();

    cmd = xgu_set_transform_program_start(cmd, 0);
    cmd = xgu_set_transform_execution_mode(cmd, XGU_PROGRAM, XGU_RANGE_MODE_PRIVATE);
    cmd = xgu_set_transform_program_cxt_write_enable(cmd, false);
    cmd = xgu_set_transform_program_load(cmd, 0);

    // FIXME: wait for xgu_set_transform_program to get fixed
    for(uint32_t i = 0; i < size / 16; i++) {
        cmd = push_command   (cmd, NV097_SET_TRANSFORM_PROGRAM, 4);
        cmd = push_parameters(cmd, &inst[i].i[0], 4);
    }

    pb_end(cmd);
}

static inline void draw_set_uniforms(const struct ShaderProgram *prg) {
    cmd = pb_begin();
    cmd = pb_push1(cmd, NV20_TCL_PRIMITIVE_3D_VP_UPLOAD_CONST_ID, 96);
    pb_push(cmd++, NV20_TCL_PRIMITIVE_3D_VP_UPLOAD_CONST_X, 16);
    memcpy(cmd, &mat_viewport.f[0], 16*4); cmd += 16;
    pb_end(cmd);
}

static inline void draw_set_vertex_attribs(const struct ShaderProgram *prg) {
    for (uint32_t i = 0; i < prg->num_attrs; ++i) {
        if (prg->attr[i].size)
            xgux_set_attrib_pointer(prg->attr[i].type, XGU_FLOAT, prg->attr[i].size, prg->stride, vtx_buf_ptr + prg->attr[i].ofs);
    }
}

static inline void draw_reset_vertex_attribs(void) {
    for(int i = 0; i < XGU_ATTRIBUTE_COUNT; i++)
        xgux_set_attrib_pointer(i, XGU_FLOAT, 0, 0, NULL);
}

static bool gfx_xbox_rapi_z_is_from_0_to_1(void) {
    return false;
}

static void gfx_xbox_rapi_unload_shader(struct ShaderProgram *old_prg) {
    if (cur_shader && (cur_shader == old_prg || !old_prg))
        cur_shader = NULL;
}

static void gfx_xbox_rapi_load_shader(struct ShaderProgram *new_prg) {
    while (pb_busy());

    cur_shader = new_prg;

    draw_set_vertex_shader(cur_shader->prog->vp_inst, *cur_shader->prog->vp_size);
    cmd = pb_begin();
    cmd = cur_shader->prog->fp_combiner(cmd);
    pb_end(cmd);

    cmd = pb_begin();
    cmd = xgu_set_alpha_test_enable(cmd, new_prg->cc.opt_texture_edge);
    pb_end(cmd);

    draw_set_uniforms(cur_shader);
    draw_reset_vertex_attribs();
    // draw_set_vertex_attribs will be called in draw_triangles
}

static struct ShaderProgram *gfx_xbox_rapi_create_and_load_new_shader(uint32_t shader_id) {
    const struct CompiledShader *csh = NULL;
    for (uint32_t i = 0; i < num_shader_objs; ++i) {
        if (shader_objs[i].shader_id == shader_id) {
            csh = shader_objs + i;
            break;
        }
    }

    if (!csh) {
        debugPrint("gfx_xbox_rapi_create_and_load_new_shader: could not find shader for id %08x\n", shader_id);
        pb_show_debug_screen();
        while (1) Sleep(100);
    }

    struct CCFeatures ccf;
    gfx_cc_get_features(shader_id, &ccf);

    struct ShaderProgram *prg = &shader_program_pool[num_shaders++];

    prg->shader_id = shader_id;
    prg->cc = ccf;
    prg->prog = csh;

    uint32_t num_attrs = 0;
    uint32_t cnt = 0;

    // position always exists
    prg->attr[num_attrs].ofs  = cnt;
    prg->attr[num_attrs].size = 4;
    prg->attr[num_attrs].type = XGU_VERTEX_ARRAY;
    cnt += 4; ++num_attrs;

    if (ccf.used_textures[0] || ccf.used_textures[1]) {
        // texcoords
        prg->attr[num_attrs].ofs  = cnt;
        prg->attr[num_attrs].size = 2;
        prg->attr[num_attrs].type = XGU_TEXCOORD0_ARRAY;
        cnt += 2; ++num_attrs;
    }

    if (ccf.opt_fog) {
        // fog rgb and intensity
        prg->attr[num_attrs].ofs  = cnt;
        prg->attr[num_attrs].size = 4;
        prg->attr[num_attrs].type = XGU_SECONDARY_COLOR_ARRAY;
        cnt += 4; ++num_attrs;
    }

    // all color inputs
    const int csiz = ccf.opt_alpha ? 4 : 3;
    for (int i = 0; i < ccf.num_inputs; i++) {
        prg->attr[num_attrs].ofs  = cnt;
        prg->attr[num_attrs].size = csiz;
        prg->attr[num_attrs].type = XGU_COLOR_ARRAY + i;
        cnt += csiz; ++num_attrs;
    }

    prg->num_attrs = num_attrs;
    prg->stride = cnt * sizeof(float);

    gfx_xbox_rapi_load_shader(prg);

    return prg;
}

static struct ShaderProgram *gfx_xbox_rapi_lookup_shader(uint32_t shader_id) {
    for (size_t i = 0; i < num_shaders; i++)
        if (shader_program_pool[i].shader_id == shader_id)
            return &shader_program_pool[i];
    return NULL;
}

static void gfx_xbox_rapi_shader_get_info(struct ShaderProgram *prg, uint8_t *num_inputs, bool used_textures[2]) {
    *num_inputs = prg->cc.num_inputs;
    used_textures[0] = prg->cc.used_textures[0];
    used_textures[1] = prg->cc.used_textures[1];
}

static uint32_t gfx_xbox_rapi_new_texture(void) {
    const uint32_t idx = num_textures++;

    last_tex = tex_pool + idx;
    last_tex->data = NULL;
    last_tex->addr = 0;
    last_tex->wrap_u = XGU_WRAP;
    last_tex->wrap_v = XGU_WRAP;
    last_tex->filter = NV_TEX_FILTER_LINEAR;

    return idx;
}

static void gfx_xbox_rapi_select_texture(int tile, uint32_t texture_id) {
    last_tile = tile;
    last_tex = cur_tex[tile] = tex_pool + texture_id;
    if (last_tex->data) {
        cmd = pb_begin();
        draw_set_texture(tile);
        pb_end(cmd);
    }
}

static void gfx_xbox_rapi_upload_texture(const uint8_t *rgba32_buf, int width, int height) {
    last_tex->width = width;
    last_tex->height = height;
    last_tex->pitch = width * 4;
    last_tex->wshift = log2_u32(last_tex->width);
    last_tex->hshift = log2_u32(last_tex->height);
    last_tex->format = XGU_TEXTURE_FORMAT_A8B8G8R8_SWIZZLED;

    const uint32_t in_size = height * last_tex->pitch;

    if (tex_cache_ptr + in_size > tex_cache_end) {
        debugPrint("gfx_xbox_rapi_upload_texture(%p, %d, %d): out of cache space!\n", rgba32_buf, width, height);
        tex_cache_ptr = tex_cache; // whatever, just continue from start
    }

    last_tex->data = tex_cache_ptr;
    last_tex->addr = (uint32_t)tex_cache_ptr & 0x03ffffff;

    swizzle_rect(rgba32_buf, width, height, tex_cache_ptr, last_tex->pitch, 4);

    cmd = pb_begin();
    draw_set_texture(last_tile);
    pb_end(cmd);

    tex_cache_ptr += in_size;
}

static inline uint32_t cm_to_nv(const uint32_t val) {
    if (val & G_TX_MIRROR) return XGU_MIRROR;
    return (val & G_TX_CLAMP) ? XGU_CLAMP_TO_EDGE : XGU_WRAP;
}

static void gfx_xbox_rapi_set_sampler_parameters(int tile, bool linear_filter, uint32_t cms, uint32_t cmt) {
    cur_tex[tile]->wrap_u = cm_to_nv(cms);
    cur_tex[tile]->wrap_v = cm_to_nv(cmt);
    cur_tex[tile]->filter = linear_filter ? NV_TEX_FILTER_LINEAR : NV_TEX_FILTER_NONE;
    if (cur_tex[tile]->data) {
        cmd = pb_begin();
        cmd = xgu_set_texture_address(cmd, tile, cur_tex[tile]->wrap_u, false, cur_tex[tile]->wrap_v, false, XGU_CLAMP_TO_EDGE, false, false);
        cmd = xgu_set_texture_control0(cmd, tile, true, 0, 0);
        cmd = xgu_set_texture_filter(cmd, tile, 0, XGU_TEXTURE_CONVOLUTION_QUINCUNX, cur_tex[tile]->filter, cur_tex[tile]->filter, false, false, false, false);
        pb_end(cmd);
    }
}

static void gfx_xbox_rapi_set_depth_test(bool depth_test) {
    z_test = depth_test;
    cmd = pb_begin();
    draw_set_ztest(depth_test);
    pb_end(cmd);
}

static void gfx_xbox_rapi_set_depth_mask(bool z_upd) {
    z_write = z_upd;
    cmd = pb_begin();
    draw_set_zmask(z_write);
    pb_end(cmd);
}

static void gfx_xbox_rapi_set_zmode_decal(bool zmode_decal) {
    z_decal = zmode_decal;
}

static void gfx_xbox_rapi_set_viewport(int x, int y, int width, int height) {
    mtx_viewport(&mat_viewport, x, y, width, height, 0.f, 1.f);
    if (cur_shader) draw_set_uniforms(cur_shader);
}

static void gfx_xbox_rapi_set_scissor(int x, int y, int width, int height) {
}

static void gfx_xbox_rapi_set_use_alpha(bool use_alpha) {
    do_blend = use_alpha;
    cmd = pb_begin();
    draw_set_blending(use_alpha);
    pb_end(cmd);
}

static void gfx_xbox_rapi_draw_triangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) {
    if (vtx_buf_ptr + buf_vbo_len > vtx_buf_end) {
        while (pb_busy());
        vtx_buf_ptr = vtx_buf;
    }

    draw_set_vertex_attribs(cur_shader);

    memcpy(vtx_buf_ptr, buf_vbo, buf_vbo_len * sizeof(float));
    xgux_draw_arrays(XGU_TRIANGLES, 0, buf_vbo_num_tris * 3);

    vtx_buf_ptr += buf_vbo_len;
}

static void gfx_xbox_rapi_init(void) {
    vtx_buf = (float *)MmAllocateContiguousMemoryEx(VTXBUF_FLOATS * 4, 0, 0x03FFAFFF, 0, PAGE_WRITECOMBINE | PAGE_READWRITE);
    if (!vtx_buf) {
        debugPrint("gfx_xbox_rapi_init: unable to alloc %u bytes for vertex buffer\n", VTXBUF_FLOATS * 4 * 2);
        pb_show_debug_screen();
        while (1) Sleep(100);
    }

    vtx_buf_ptr = vtx_buf;
    vtx_buf_end = vtx_buf + VTXBUF_FLOATS;

    tex_cache = (uint8_t *)MmAllocateContiguousMemoryEx(TEXCACHE_SIZE, 0, 0x03FFAFFF, 0, PAGE_WRITECOMBINE | PAGE_READWRITE);
    if (!tex_cache) {
        debugPrint("gfx_xbox_rapi_init: unable to alloc %u bytes for texture cache\n", TEXCACHE_SIZE);
        pb_show_debug_screen();
        while (1) Sleep(100);
    }

    tex_cache_ptr = tex_cache;
    tex_cache_end = tex_cache + TEXCACHE_SIZE;

    cmd = pb_begin();
    cmd = xgu_set_cull_face_enable(cmd, false);
    cmd = xgu_set_alpha_func(cmd, XGU_FUNC_GREATER_OR_EQUAL);
    cmd = xgu_set_alpha_ref(cmd, 170);
    cmd = xgu_set_color_clear_value(cmd, 0xff0000ff);
    pb_end(cmd);
}

static void gfx_xbox_rapi_on_resize(void) {
}

static void gfx_xbox_rapi_start_frame(void) {
    cmd = pb_begin();
    cmd = xgu_clear_surface(cmd, XGU_CLEAR_Z | XGU_CLEAR_STENCIL | XGU_CLEAR_COLOR);
    pb_end(cmd);

    vtx_buf_ptr = vtx_buf;
}

static void gfx_xbox_rapi_end_frame(void) {
}

static void gfx_xbox_rapi_finish_render(void) {
}

struct GfxRenderingAPI gfx_xbox_rapi = {
    gfx_xbox_rapi_z_is_from_0_to_1,
    gfx_xbox_rapi_unload_shader,
    gfx_xbox_rapi_load_shader,
    gfx_xbox_rapi_create_and_load_new_shader,
    gfx_xbox_rapi_lookup_shader,
    gfx_xbox_rapi_shader_get_info,
    gfx_xbox_rapi_new_texture,
    gfx_xbox_rapi_select_texture,
    gfx_xbox_rapi_upload_texture,
    gfx_xbox_rapi_set_sampler_parameters,
    gfx_xbox_rapi_set_depth_test,
    gfx_xbox_rapi_set_depth_mask,
    gfx_xbox_rapi_set_zmode_decal,
    gfx_xbox_rapi_set_viewport,
    gfx_xbox_rapi_set_scissor,
    gfx_xbox_rapi_set_use_alpha,
    gfx_xbox_rapi_draw_triangles,
    gfx_xbox_rapi_init,
    gfx_xbox_rapi_on_resize,
    gfx_xbox_rapi_start_frame,
    gfx_xbox_rapi_end_frame,
    gfx_xbox_rapi_finish_render
};
#endif

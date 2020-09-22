#ifdef TARGET_PS3

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>

#include <ppu-types.h>
#include <rsx/rsx.h>
#include <sysutil/video.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/gbi.h>

#include "gfx_cc.h"
#include "gfx_rendering_api.h"

#define MAX_TEXTURES 1024

#define RSX_ZNEAR 0.f
#define RSX_ZFAR  1.f

// this contains `struct ShaderData shader_objs[]` and `u32 num_shader_objs`
#include "ps3_shader_db.h"

#define RSX_MAXVERTS (1024 * 3)
#define RSX_BUFSIZE  (RSX_MAXVERTS * 32)

// vertex attributes
#define PRG_MAX_VATTRS 8

// fragment attributes
#define PRG_MAX_FATTRS 2
#define PRG_FATTR_TEX0 0
#define PRG_FATTR_TEX1 1

// fragment constants
#define PRG_MAX_FCONSTS 2 // noise stuff

extern gcmContextData *rsx_ctx;
extern videoResolution vid_mode;

struct Texture {
    gcmTexture tex;
    u32 *data;
    u32 wrap_s;
    u32 wrap_t;
    u32 min_filter;
    u32 mag_filter;
};

struct ShaderProgram {
    uint32_t shader_id;
    struct CCFeatures cc;

    // total vertex size in bytes
    u32 stride;

    // vertex attribute RSX offsets and sizes
    u32 vattr_ofs[PRG_MAX_VATTRS];
    u32 vattr_siz[PRG_MAX_VATTRS];
    u32 vattr_type[PRG_MAX_VATTRS];
    int num_vattrs;

    rsxProgramAttrib *fattrs[PRG_MAX_FATTRS];
    int num_fattrs;

    rsxProgramConst *fconsts[PRG_MAX_FCONSTS];
    int num_fconsts;

    rsxVertexProgram *vpo;
    void *vp_ucode;

    rsxFragmentProgram *fpo;
    void *fp_ucode;
    u32 *fp_buf;
    u32 fp_ofs;
};

static struct DrawEnv {
    struct Viewport {
        int x;
        int y;
        int w;
        int h;
        float zn;
        float zf;
        float scale[4];
        float offset[4];
    } view;

    struct Scissor {
        int x;
        int y;
        int w;
        int h;
    } clip;

    u32 blending;
    u32 z_test;
    u32 z_write;
    u32 z_decal;
} rsx_env;

static struct ShaderProgram shader_program_pool[64];
static uint8_t shader_program_pool_size;
static struct ShaderProgram *cur_prg;

static struct Texture tex_pool[MAX_TEXTURES];
static uint32_t tex_pool_size;
static struct Texture *cur_tex[2];
static struct Texture *last_tex;
static int last_tile;

static uint32_t frame_count;
static uint32_t current_height;

static f32 *rsx_vtx_buf[2];
static f32 *rsx_vtx_buf_end[2];
static f32 *rsx_vtx_ptr[2];
static u32 rsx_vtx_cur = 0;

static u16 *rsx_idx_buf;
static u32 rsx_idx_ofs;

static bool gfx_ps3_z_is_from_0_to_1(void) {
    return false;
}

static void gfx_ps3_unload_shader(struct ShaderProgram *old_prg) {
    if (old_prg != NULL) {
        for (int i = 0; i < old_prg->num_vattrs; ++i)
            rsxBindVertexArrayAttrib(rsx_ctx, old_prg->vattr_type[i], 0, 0, 0, 0, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    }
}

static inline void gfx_ps3_bind_attribs(const float *buf, const u32 stride, struct ShaderProgram *prg) {
    // set all vertex attribute pointers
    u32 ofs;
    for (int i = 0; i < prg->num_vattrs; ++i) {
        rsxAddressToOffset(buf + prg->vattr_ofs[i], &ofs);
        rsxBindVertexArrayAttrib(rsx_ctx, prg->vattr_type[i], 0, ofs, stride, prg->vattr_siz[i], GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    }
}

static void gfx_ps3_load_shader(struct ShaderProgram *prg) {
    cur_prg = prg;

    rsxLoadVertexProgram(rsx_ctx, prg->vpo, prg->vp_ucode);

    // TODO: set uniforms, if any

    rsxLoadFragmentProgramLocation(rsx_ctx, prg->fpo, prg->fp_ofs, GCM_LOCATION_RSX);

    // enable textures if needed

    rsxTextureControl(rsx_ctx, 0, prg->cc.used_textures[0], 0, 0, GCM_TEXTURE_MAX_ANISO_1);
    rsxTextureControl(rsx_ctx, 1, prg->cc.used_textures[1], 0, 0, GCM_TEXTURE_MAX_ANISO_1);
}

static struct ShaderProgram *gfx_ps3_create_and_load_new_shader(uint32_t shader_id) {
    struct CCFeatures ccf;
    gfx_cc_get_features(shader_id, &ccf);

    const struct ShaderData *shdat = NULL;
    for (u32 i = 0; i < num_shader_objs; ++i) {
        if (shader_objs[i].shader_id == shader_id) {
            shdat = &shader_objs[i];
            break;
        }
    }

    if (!shdat) {
        printf("gfx_ps3_create_and_load_new_shader(): no Cg shader for id %08x\n", shader_id);
        exit(1);
    }

    const u32 vpo_size = *shdat->vpo_size;
    const u32 fpo_size = *shdat->fpo_size;

    rsxVertexProgram *vpo = (rsxVertexProgram *)shdat->vpo_data;
    rsxFragmentProgram *fpo = (rsxFragmentProgram *)shdat->fpo_data;

    u32 size, fp_ofs;
    void *vp_ucode;
    void *fp_ucode, *fp_buf;

    rsxVertexProgramGetUCode(vpo, &vp_ucode, &size);
    rsxFragmentProgramGetUCode(fpo, &fp_ucode, &size);

    fp_buf = (u32 *)rsxMemalign(64, size);
    memcpy(fp_buf, fp_ucode, size);
    rsxAddressToOffset(fp_buf, &fp_ofs);

    struct ShaderProgram *prg = &shader_program_pool[shader_program_pool_size++];
    prg->shader_id = shader_id;
    prg->cc = ccf;
    prg->vpo = vpo;
    prg->fpo = fpo;
    prg->vp_ucode = vp_ucode;
    prg->fp_ucode = fp_ucode;
    prg->fp_buf = fp_buf;
    prg->fp_ofs = fp_ofs;

    // vertex attributes

    int num_vattrs = 0;
    int cnt = 0;

    // position always exists
    prg->vattr_ofs[num_vattrs] = cnt;
    prg->vattr_siz[num_vattrs] = 4;
    prg->vattr_type[num_vattrs] = GCM_VERTEX_ATTRIB_POS;
    cnt += 4; ++num_vattrs;

    if (ccf.used_textures[0] || ccf.used_textures[1]) {
        // texcoords
        prg->vattr_ofs[num_vattrs] = cnt;
        prg->vattr_siz[num_vattrs] = 2;
        prg->vattr_type[num_vattrs] = GCM_VERTEX_ATTRIB_TEX0;
        cnt += 2; ++num_vattrs;
    }

    if (ccf.opt_fog) {
        // fog RGB and intensity
        prg->vattr_ofs[num_vattrs] = cnt;
        prg->vattr_siz[num_vattrs] = 4;
        prg->vattr_type[num_vattrs] = GCM_VERTEX_ATTRIB_COLOR1; // there's usually max 1 input when fog is on
        cnt += 4; ++num_vattrs;
    }

    // all color inputs
    const int csiz = ccf.opt_alpha ? 4 : 3;
    for (int i = 0; i < ccf.num_inputs; i++) {
        prg->vattr_ofs[num_vattrs] = cnt;
        prg->vattr_siz[num_vattrs] = csiz;
        prg->vattr_type[num_vattrs] = GCM_VERTEX_ATTRIB_COLOR0 + i;
        cnt += csiz; ++num_vattrs;
    }

    prg->num_vattrs = num_vattrs;
    prg->stride = cnt * sizeof(float);

    // fragment attributes

    int num_fattrs = 0;

    if (ccf.used_textures[0])
        prg->fattrs[num_fattrs++] = rsxFragmentProgramGetAttrib(fpo, "uTex0");
    if (ccf.used_textures[1])
        prg->fattrs[num_fattrs++] = rsxFragmentProgramGetAttrib(fpo, "uTex1");

    prg->num_fattrs = num_fattrs;

    // fragment consts

    int num_fconsts = 0;

    /* nada */

    prg->num_fconsts = num_fconsts;

    gfx_ps3_load_shader(prg);

    return prg;
}

static struct ShaderProgram *gfx_ps3_lookup_shader(uint32_t shader_id) {
    for (size_t i = 0; i < shader_program_pool_size; i++) {
        if (shader_program_pool[i].shader_id == shader_id) {
            return &shader_program_pool[i];
        }
    }
    return NULL;
}

static void gfx_ps3_shader_get_info(struct ShaderProgram *prg, uint8_t *num_inputs, bool used_textures[2]) {
    *num_inputs = prg->cc.num_inputs;
    used_textures[0] = prg->cc.used_textures[0];
    used_textures[1] = prg->cc.used_textures[1];
}

static uint32_t gfx_ps3_new_texture(void) {
    u32 idx = tex_pool_size++;

    last_tex = tex_pool + idx;

    last_tex->tex.format    = (GCM_TEXTURE_FORMAT_A8R8G8B8 | GCM_TEXTURE_FORMAT_LIN);
    last_tex->tex.mipmap    = 1;
    last_tex->tex.dimension = GCM_TEXTURE_DIMS_2D;
    last_tex->tex.cubemap   = GCM_FALSE;
    last_tex->tex.location  = GCM_LOCATION_RSX;
    last_tex->tex.depth     = 1;
    last_tex->tex.remap     = (
        (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_B_SHIFT) |
        (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_G_SHIFT) |
        (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_R_SHIFT) |
        (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_A_SHIFT) |
        (GCM_TEXTURE_REMAP_COLOR_B << GCM_TEXTURE_REMAP_COLOR_A_SHIFT)   |
        (GCM_TEXTURE_REMAP_COLOR_G << GCM_TEXTURE_REMAP_COLOR_B_SHIFT)   |
        (GCM_TEXTURE_REMAP_COLOR_R << GCM_TEXTURE_REMAP_COLOR_G_SHIFT)   |
        (GCM_TEXTURE_REMAP_COLOR_A << GCM_TEXTURE_REMAP_COLOR_R_SHIFT)
    );
    last_tex->tex.offset = 0;

    return idx;
}

static void gfx_ps3_select_texture(int tile, uint32_t texture_id) {
    last_tile = tile;
    last_tex = cur_tex[tile] = tex_pool + texture_id;
    if (last_tex->tex.offset) {
        rsxInvalidateTextureCache(rsx_ctx, GCM_INVALIDATE_TEXTURE);
        rsxLoadTexture(rsx_ctx, tile, &last_tex->tex);
        rsxTextureWrapMode(rsx_ctx, tile, last_tex->wrap_s, last_tex->wrap_t, GCM_TEXTURE_CLAMP_TO_EDGE, 0, GCM_TEXTURE_ZFUNC_LESS, 0);
        rsxTextureFilter(rsx_ctx, tile, 0, last_tex->min_filter, last_tex->mag_filter, GCM_TEXTURE_CONVOLUTION_QUINCUNX);
    }
}

static void gfx_ps3_upload_texture(const uint8_t *rgba32_buf, int width, int height) {
    last_tex->data = (u32 *)rsxMemalign(128, width * height * 4);
    if (!last_tex->data) return;

    memcpy(last_tex->data, rgba32_buf, width * height * 4);
    rsxAddressToOffset(last_tex->data, &last_tex->tex.offset);

    last_tex->tex.width = width;
    last_tex->tex.height = height;
    last_tex->tex.pitch = width * 4;

    rsxInvalidateTextureCache(rsx_ctx, GCM_INVALIDATE_TEXTURE);
    rsxLoadTexture(rsx_ctx, last_tile, &last_tex->tex);
}

static inline u32 gfx_cm_to_gcm(uint32_t val) {
    if (val & G_TX_CLAMP) return GCM_TEXTURE_CLAMP_TO_EDGE;
    return (val & G_TX_MIRROR) ? GCM_TEXTURE_MIRRORED_REPEAT : GCM_TEXTURE_REPEAT;
}

static void gfx_ps3_set_sampler_parameters(int tile, bool linear_filter, uint32_t cms, uint32_t cmt) {
    const u32 wrap_s = gfx_cm_to_gcm(cms);
    const u32 wrap_t = gfx_cm_to_gcm(cmt);
    const u32 filter = linear_filter ? GCM_TEXTURE_LINEAR : GCM_TEXTURE_NEAREST;
    rsxTextureWrapMode(rsx_ctx, tile, wrap_s, wrap_t, GCM_TEXTURE_CLAMP_TO_EDGE, 0, GCM_TEXTURE_ZFUNC_LESS, 0);
    rsxTextureFilter(rsx_ctx, tile, 0, filter, filter, GCM_TEXTURE_CONVOLUTION_QUINCUNX);
    cur_tex[tile]->wrap_s = wrap_s;
    cur_tex[tile]->wrap_t = wrap_t;
    cur_tex[tile]->min_filter = filter;
    cur_tex[tile]->mag_filter = filter;
}

static void gfx_ps3_set_depth_test(bool depth_test) {
    rsx_env.z_test = depth_test;
    rsxSetDepthTestEnable(rsx_ctx, depth_test);
}

static void gfx_ps3_set_depth_mask(bool z_upd) {
    rsx_env.z_write = z_upd;
    rsxSetDepthWriteEnable(rsx_ctx, z_upd);
}

static void gfx_ps3_set_zmode_decal(bool zmode_decal) {
    rsx_env.z_decal = zmode_decal;
}

static void gfx_ps3_set_viewport(int x, int y, int width, int height) {
    y = vid_mode.height - y - height;
    current_height = height;
    rsx_env.view.x = x;
    rsx_env.view.y = y;
    rsx_env.view.w = width;
    rsx_env.view.h = height;
    rsx_env.view.scale[0]  = width * 0.5f;
    rsx_env.view.scale[1]  = height * -0.5f;
    rsx_env.view.scale[2]  = (RSX_ZFAR - RSX_ZNEAR) * 0.5f;
    rsx_env.view.scale[3]  = 0.0f;
    rsx_env.view.offset[0] = x + width * 0.5f;
    rsx_env.view.offset[1] = y + height * 0.5f;
    rsx_env.view.offset[2] = (RSX_ZFAR + RSX_ZNEAR) * 0.5f;
    rsx_env.view.offset[3] = 0.0f;
    rsxSetViewport(rsx_ctx, x, y, width, height, RSX_ZNEAR, RSX_ZFAR, rsx_env.view.scale, rsx_env.view.offset);
    for(int i = 0; i < 8; ++i) rsxSetViewportClip(rsx_ctx, i, vid_mode.width, vid_mode.height);
}

static void gfx_ps3_set_scissor(int x, int y, int width, int height) {
    rsx_env.clip.x = x;
    rsx_env.clip.y = y;
    rsx_env.clip.w = width;
    rsx_env.clip.h = height;
    rsxSetScissor(rsx_ctx, x, y, width, height);
}

static void gfx_ps3_set_use_alpha(bool use_alpha) {
    rsx_env.blending = use_alpha;
    rsxSetBlendEnable(rsx_ctx, use_alpha);
}

static inline void draw_set_environment(void) {
    rsxSetDepthTestEnable(rsx_ctx, rsx_env.z_test);
    rsxSetDepthWriteEnable(rsx_ctx, rsx_env.z_write);
    rsxSetBlendEnable(rsx_ctx, rsx_env.blending);

    rsxSetViewport(rsx_ctx, rsx_env.view.x, rsx_env.view.y, rsx_env.view.w, rsx_env.view.h, RSX_ZNEAR, RSX_ZFAR, rsx_env.view.scale, rsx_env.view.offset);
    for(int i = 0; i < 8; ++i) rsxSetViewportClip(rsx_ctx, i, vid_mode.width, vid_mode.height);

    rsxSetScissor(rsx_ctx, rsx_env.clip.x, rsx_env.clip.y, rsx_env.clip.w, rsx_env.clip.h);

    // some stuff that rarely changes

    rsxSetBlendFunc(rsx_ctx, GCM_SRC_ALPHA, GCM_ONE_MINUS_SRC_ALPHA, GCM_SRC_ALPHA, GCM_ONE_MINUS_SRC_ALPHA);
    rsxSetDepthFunc(rsx_ctx, GCM_LESS);
    rsxSetShadeModel(rsx_ctx, GCM_SHADE_MODEL_SMOOTH);
    rsxSetColorMask(
        rsx_ctx,
        GCM_COLOR_MASK_B |
        GCM_COLOR_MASK_G |
        GCM_COLOR_MASK_R |
        GCM_COLOR_MASK_A
    );
    rsxSetColorMaskMrt(rsx_ctx, 0);
    rsxSetClearColor(rsx_ctx, 0);
    rsxSetClearDepthStencil(rsx_ctx, 0xffffff00);
    rsxSetZControl(rsx_ctx, 1, 1, 1);
    rsxSetUserClipPlaneControl(
        rsx_ctx,
        GCM_USER_CLIP_PLANE_DISABLE,
        GCM_USER_CLIP_PLANE_DISABLE,
        GCM_USER_CLIP_PLANE_DISABLE,
        GCM_USER_CLIP_PLANE_DISABLE,
        GCM_USER_CLIP_PLANE_DISABLE,
        GCM_USER_CLIP_PLANE_DISABLE
    );
    rsxSetCullFaceEnable(rsx_ctx, GCM_FALSE);
    rsxSetStencilTestEnable(rsx_ctx, GCM_FALSE);

    // reload shader

    if (cur_prg) gfx_ps3_load_shader(cur_prg);

    // reload textures

    if (cur_tex[0]) gfx_ps3_select_texture(0, cur_tex[0] - tex_pool);
    if (cur_tex[1]) gfx_ps3_select_texture(1, cur_tex[1] - tex_pool);
}  

extern void rsx_wait_finish(void);

static void gfx_ps3_draw_triangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) {
    const size_t num_verts = buf_vbo_num_tris * 3;
    const size_t vert_len = buf_vbo_len / num_verts;

    if (rsx_vtx_ptr[rsx_vtx_cur] + buf_vbo_len >= rsx_vtx_buf_end[rsx_vtx_cur]) {
        // flush the buffer and wait, then we can resume at the start of the buffer
        printf("rsx vtx flush at %u floats\n", rsx_vtx_ptr[rsx_vtx_cur] - rsx_vtx_buf[rsx_vtx_cur]);
        rsx_wait_finish();
        draw_set_environment();
        rsx_vtx_ptr[rsx_vtx_cur] = rsx_vtx_buf[rsx_vtx_cur];
    }

    gfx_ps3_set_viewport(0, 0, 1280, 720);
    memcpy(rsx_vtx_ptr[rsx_vtx_cur], buf_vbo, buf_vbo_len * sizeof(float));
    gfx_ps3_bind_attribs(rsx_vtx_ptr[rsx_vtx_cur], vert_len * sizeof(float), cur_prg);
    rsxDrawIndexArray(rsx_ctx, GCM_TYPE_TRIANGLES, rsx_idx_ofs, num_verts, GCM_INDEX_TYPE_16B, GCM_LOCATION_RSX);

    rsx_vtx_ptr[rsx_vtx_cur] += buf_vbo_len;
}

static void gfx_ps3_init(void) {
    // allocate index buffer

    rsx_idx_buf = (u16 *)rsxMemalign(128, RSX_MAXVERTS * sizeof(u16));
    if (!rsx_idx_buf) {
        printf("gfx_ps3_init(): could not alloc %u word index buffer\n", RSX_MAXVERTS);
        exit(1);
    }

    // it's always the same sequence
    for (u16 i = 0; i < RSX_MAXVERTS; ++i) rsx_idx_buf[i] = i;

    rsxAddressToOffset(rsx_idx_buf, &rsx_idx_ofs);

    // allocate vertex double buffer

    rsx_vtx_buf[0] = (f32 *)rsxMemalign(128, RSX_BUFSIZE * sizeof(float));
    rsx_vtx_buf[1] = (f32 *)rsxMemalign(128, RSX_BUFSIZE * sizeof(float));
    if (!rsx_vtx_buf[0] || !rsx_vtx_buf[1]) {
        printf("gfx_ps3_init(): could not alloc 2x %u float vertex buffers\n", RSX_BUFSIZE);
        exit(1);
    }

    rsx_vtx_ptr[0] = rsx_vtx_buf[0];
    rsx_vtx_ptr[1] = rsx_vtx_buf[1];
    rsx_vtx_buf_end[0] = rsx_vtx_buf[0] + RSX_BUFSIZE;
    rsx_vtx_buf_end[1] = rsx_vtx_buf[1] + RSX_BUFSIZE;
}

static void gfx_ps3_on_resize(void) {

}

static void gfx_ps3_start_frame(void) {
    rsxClearSurface(
        rsx_ctx,
        GCM_CLEAR_R |
        GCM_CLEAR_G |
        GCM_CLEAR_B |
        GCM_CLEAR_A |
        GCM_CLEAR_S |
        GCM_CLEAR_Z
    );

    rsx_vtx_ptr[rsx_vtx_cur] = rsx_vtx_buf[rsx_vtx_cur];

    draw_set_environment();

    ++frame_count;
}

static void gfx_ps3_end_frame(void) {
    rsx_vtx_cur ^= 1;
}

static void gfx_ps3_finish_render(void) {

}

struct GfxRenderingAPI gfx_ps3_rapi = {
    gfx_ps3_z_is_from_0_to_1,
    gfx_ps3_unload_shader,
    gfx_ps3_load_shader,
    gfx_ps3_create_and_load_new_shader,
    gfx_ps3_lookup_shader,
    gfx_ps3_shader_get_info,
    gfx_ps3_new_texture,
    gfx_ps3_select_texture,
    gfx_ps3_upload_texture,
    gfx_ps3_set_sampler_parameters,
    gfx_ps3_set_depth_test,
    gfx_ps3_set_depth_mask,
    gfx_ps3_set_zmode_decal,
    gfx_ps3_set_viewport,
    gfx_ps3_set_scissor,
    gfx_ps3_set_use_alpha,
    gfx_ps3_draw_triangles,
    gfx_ps3_init,
    gfx_ps3_on_resize,
    gfx_ps3_start_frame,
    gfx_ps3_end_frame,
    gfx_ps3_finish_render
};

#endif // TARGET_PS3

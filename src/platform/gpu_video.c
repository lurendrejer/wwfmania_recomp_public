#include "gpu_video.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gl_min.h"

#include "assets/img.h"

/*
 * How it maps onto the CPU blitter (src/video/video.c):
 *
 *  - The index framebuffer is an RGBA8 texture with the same size as video.fb; a pixel's 16-bit value
 *    (palette << 8 | color) is stored as R = low byte, G = high byte. With true-color art a second texture
 *    of the same size holds video.detail (R, G, B residual and A luma, the bytes of the detail word).
 *  - video_dma, video_clear, video_fill_rows and video_put_pixel are recorded as a display list and played
 *    back in order, once into the index texture and once into the detail texture (flush_list).
 *  - A blit is a quad over the clipped rectangle. The fragment shader works out each pixel's source
 *    position with the integer maths of src_pos() (16.16 step, centre sampling, clamp, flip) carried out
 *    exactly in float32, reads the source texel (the image's pixels with the palette mask applied, or the
 *    override's) and applies the write mode: write a value or discard. Writes need no blending.
 *  - The present pass looks every index up in a 256 x 128 palette texture (colour RAM converted as
 *    img_color_argb does), turns pixels with bit 15 into high_pal_argb and adds the detail like
 *    gfx_apply_detail, then writes the visible part into the renderer's frame texture.
 */

/* ---- GL entry points (loaded through SDL, so nothing links against libGLESv2) ------------------ */

#define GL_FUNCS(X) \
    X(const GLubyte *, GetString, (GLenum)) \
    X(GLenum, GetError, (void)) \
    X(void, Finish, (void)) \
    X(void, GetIntegerv, (GLenum, GLint *)) \
    X(void, GetFloatv, (GLenum, GLfloat *)) \
    X(void, GetBooleanv, (GLenum, GLboolean *)) \
    X(void, GetShaderiv, (GLuint, GLenum, GLint *)) \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(void, GetProgramiv, (GLuint, GLenum, GLint *)) \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(void, GetVertexAttribiv, (GLuint, GLenum, GLint *)) \
    X(void, GenTextures, (GLsizei, GLuint *)) \
    X(void, DeleteTextures, (GLsizei, const GLuint *)) \
    X(void, BindTexture, (GLenum, GLuint)) \
    X(void, ActiveTexture, (GLenum)) \
    X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
    X(void, TexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *)) \
    X(void, TexParameteri, (GLenum, GLenum, GLint)) \
    X(void, PixelStorei, (GLenum, GLint)) \
    X(void, GenFramebuffers, (GLsizei, GLuint *)) \
    X(void, DeleteFramebuffers, (GLsizei, const GLuint *)) \
    X(void, BindFramebuffer, (GLenum, GLuint)) \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(GLenum, CheckFramebufferStatus, (GLenum)) \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, Scissor, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, Enable, (GLenum)) \
    X(void, Disable, (GLenum)) \
    X(GLboolean, IsEnabled, (GLenum)) \
    X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, Clear, (GLbitfield)) \
    X(void, ColorMask, (GLboolean, GLboolean, GLboolean, GLboolean)) \
    X(GLuint, CreateShader, (GLenum)) \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
    X(void, CompileShader, (GLuint)) \
    X(void, DeleteShader, (GLuint)) \
    X(GLuint, CreateProgram, (void)) \
    X(void, AttachShader, (GLuint, GLuint)) \
    X(void, BindAttribLocation, (GLuint, GLuint, const GLchar *)) \
    X(void, LinkProgram, (GLuint)) \
    X(void, UseProgram, (GLuint)) \
    X(void, DeleteProgram, (GLuint)) \
    X(GLint, GetUniformLocation, (GLuint, const GLchar *)) \
    X(void, Uniform1i, (GLint, GLint)) \
    X(void, Uniform1f, (GLint, GLfloat)) \
    X(void, Uniform2f, (GLint, GLfloat, GLfloat)) \
    X(void, Uniform3f, (GLint, GLfloat, GLfloat, GLfloat)) \
    X(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, GenBuffers, (GLsizei, GLuint *)) \
    X(void, DeleteBuffers, (GLsizei, const GLuint *)) \
    X(void, BindBuffer, (GLenum, GLuint)) \
    X(void, BufferData, (GLenum, GLsizeiptr, const void *, GLenum)) \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
    X(void, EnableVertexAttribArray, (GLuint)) \
    X(void, DisableVertexAttribArray, (GLuint)) \
    X(void, DrawArrays, (GLenum, GLint, GLsizei)) \
    X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *))

typedef struct {
#define X(ret, name, args) ret(GL_APIENTRY *name) args;
    GL_FUNCS(X)
#undef X
} gl_api;

/* ---- shaders ------------------------------------------------------------------------------------- */

static const char *const vs_src =
    "attribute vec2 a;\n"
    "uniform vec4 u_rect;\n"      /* x, y, w, h in texture pixels */
    "uniform vec2 u_inv2;\n"      /* 2 / texture size */
    "void main() {\n"
    "  vec2 p = u_rect.xy + a * u_rect.zw;\n"
    "  gl_Position = vec4(p * u_inv2 - 1.0, 0.0, 1.0);\n"
    "}\n";

#define FS_HEAD \
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\n" \
    "precision highp float;\n" \
    "precision highp sampler2D;\n" \
    "#else\n" \
    "#error highp floats are needed in fragment shaders\n" \
    "#endif\n"

static const char *const fs_blit_src =
    FS_HEAD
    "uniform sampler2D u_src;\n"  /* source pixels (luminance) */
    "uniform sampler2D u_det;\n"  /* override detail words (RGBA) */
    "uniform vec2 u_org;\n"       /* destination position of the source rectangle's top left */
    "uniform vec3 u_sx;\n"        /* step in x as (step >> 12, step & 4095, (step & 4095) >> 1) */
    "uniform vec3 u_sy;\n"
    "uniform vec4 u_geo;\n"       /* source rectangle x, y, w, h in texels */
    "uniform vec2 u_flip;\n"
    "uniform vec2 u_tex;\n"       /* source texture size */
    "uniform vec2 u_modes;\n"     /* write mode for non-zero, for zero pixels: 0 none, 1 constant, 2 pixel/palette */
    "uniform vec3 u_val;\n"       /* constant value low byte, high byte; palette byte */
    "uniform vec2 u_pass;\n"      /* 1 = write the detail plane; 1 = the image has detail words */
    /* floor((i * step + step / 2) / 65536) for step = 4096 a + b, in exact float arithmetic: every
     * intermediate is an integer below 2^24 and every division is by a power of two */
    "float srcpos(float i, vec3 st) {\n"
    "  float x = st.x * (2.0 * i + 1.0);\n"
    "  float xq = floor(x / 32.0);\n"
    "  float xr = x - xq * 32.0;\n"
    "  float ih = floor(i / 256.0);\n"
    "  float il = i - ih * 256.0;\n"
    "  float m = ih * st.y;\n"
    "  float mq = floor(m / 256.0);\n"
    "  float mr = m - mq * 256.0;\n"
    "  float inner = xr * 2048.0 + mr * 256.0 + il * st.y + st.z;\n"
    "  return xq + mq + floor(inner / 65536.0);\n"
    "}\n"
    "void main() {\n"
    "  vec2 d = floor(gl_FragCoord.xy) - u_org;\n"
    "  float sx = min(srcpos(d.x, u_sx), u_geo.z - 1.0);\n"
    "  float sy = min(srcpos(d.y, u_sy), u_geo.w - 1.0);\n"
    "  if (u_flip.x > 0.5) sx = u_geo.z - 1.0 - sx;\n"
    "  if (u_flip.y > 0.5) sy = u_geo.w - 1.0 - sy;\n"
    "  vec2 uv = (vec2(u_geo.x + sx, u_geo.y + sy) + 0.5) / u_tex;\n"
    "  float p = floor(texture2D(u_src, uv).r * 255.0 + 0.5);\n"
    "  float mode = p > 0.5 ? u_modes.x : u_modes.y;\n"
    "  if (mode < 0.5) discard;\n"
    "  if (u_pass.x > 0.5) {\n"
    "    if (p > 0.5 && mode > 1.5 && u_pass.y > 0.5) gl_FragColor = texture2D(u_det, uv);\n"
    "    else gl_FragColor = vec4(0.0);\n"
    "    return;\n"
    "  }\n"
    "  vec2 v = mode < 1.5 ? u_val.xy : vec2(p > 0.5 ? p : 0.0, u_val.z);\n"
    "  gl_FragColor = vec4(v / 255.0, 0.0, 1.0);\n"
    "}\n";

static const char *const fs_final_src =
    FS_HEAD
    "uniform sampler2D u_idx;\n"
    "uniform sampler2D u_det;\n"
    "uniform sampler2D u_pal;\n"
    "uniform vec2 u_org;\n"       /* framebuffer pixel of the target texture's (0, 0) */
    "uniform vec2 u_fb;\n"        /* framebuffer size */
    "uniform vec4 u_high;\n"      /* high_pal_argb */
    "uniform float u_usedet;\n"
    "void main() {\n"
    "  vec2 f = floor(gl_FragCoord.xy) + u_org;\n"
    "  if (f.x < 0.0 || f.y < 0.0 || f.x >= u_fb.x || f.y >= u_fb.y) {\n"
    "    gl_FragColor = texture2D(u_pal, vec2(0.5 / 256.0, 0.5 / 128.0));\n"
    "    return;\n"
    "  }\n"
    "  vec2 uv = (f + 0.5) / u_fb;\n"
    "  vec4 t = texture2D(u_idx, uv);\n"
    "  float lo = floor(t.r * 255.0 + 0.5);\n"
    "  float hi = floor(t.g * 255.0 + 0.5);\n"
    "  if (hi >= 128.0) { gl_FragColor = u_high; return; }\n"
    "  vec4 c = texture2D(u_pal, vec2((lo + 0.5) / 256.0, (hi + 0.5) / 128.0));\n"
    "  if (u_usedet > 0.5) {\n"
    "    vec4 db = floor(texture2D(u_det, uv) * 255.0 + 0.5);\n"
    "    if (db.r + db.g + db.b + db.a > 0.5) {\n"
    "      vec3 rgb = floor(c.rgb * 255.0 + 0.5);\n"
    "      float l = floor((rgb.r * 299.0 + rgb.g * 587.0 + rgb.b * 114.0 + 0.5) / 1000.0);\n"
    "      float ratio = 256.0;\n"
    "      if (l < db.a) ratio = floor((l * 256.0 + 0.5) / db.a);\n"
    "      vec3 sd = db.rgb - 256.0 * step(128.0, db.rgb);\n"
    "      vec3 q = sd * ratio;\n"
    "      q = sign(q) * floor(abs(q) / 256.0);\n"
    "      rgb = clamp(rgb + q, 0.0, 255.0);\n"
    "      c = vec4(rgb / 255.0, c.a);\n"
    "    }\n"
    "  }\n"
    "  gl_FragColor = c;\n"
    "}\n";

/* ---- state ---------------------------------------------------------------------------------------- */

enum { OP_FILL, OP_BLIT };
typedef struct {
    int type;
    int x, y, w, h;           /* OP_FILL */
    uint16_t value;
    video_blit_job j;         /* OP_BLIT */
} gop;

#define MAX_OPS 8192
#define ASYNC_QUEUE 512

typedef struct {
    GLuint id;
    GLint src, det, idx, pal, rect, inv2, org, sx, sy, geo, flip, tex, modes, val, pass, fb, high, usedet;
} gprog;

struct gpu_video {
    SDL_Renderer *ren;
    SDL_Window *win;
    SDL_GLContext ctx;
    gl_api gl;
    video_sink sink;
    int made;                 /* GL objects exist */
    int failed;
    int max_tex;
    char info[320];
    gprog blit, fin;
    GLuint vbo;
    GLuint pal_tex;
    uint16_t col_shadow[VIDEO_COLORS];
    int pal_valid;
    GLuint idx_tex, idx_fbo, det_tex, det_fbo;
    int tw, th;
    GLuint out_fbo;
    GLuint rb_tex;
    int rb_w, rb_h;
    gop *ops;
    int nops, cap;
    int list_detail;          /* a blit with detail words is in the list */
    GLuint *owned;
    int nowned, owned_cap;
    unsigned tag;             /* identifies this context's textures in gfx_image.sink_owner */
    int warned_big;
    long selftest_readbacks;  /* those of the start-up comparison */
    long readbacks;           /* times the picture was read back to the CPU (a cost: see docs/VIDEO.md) */
    /* timing of the last interval (gpu_video_stats): CPU time spent in the sink and in the GL calls */
    int profile;              /* glFinish after the list and the present pass, to time the GPU itself */
    double ms_record, ms_play, ms_final, ms_gpu_play, ms_gpu_final, ms_upload;
    long n_blit, n_fill, n_flush, n_upload, bytes_upload, rb_start;
    long px_reads, px_total;
    int suspended;            /* the CPU draws for now (gpu_video_suspend) */
    long fills_total, act_fill, act_rb;   /* for gpu_video_activity */
    uint32_t rb_tag[8];       /* who caused the read backs of the interval (video.sync_tag), most frequent few */
    long rb_tag_n[8];
    int rb_tags;
    /* async compute (gpu_video_set_async): the textures of an override are made a few milliseconds per frame, from a
     * queue, and until one is there its image is drawn from the original pixels */
    int async;
    double async_ms;          /* budget per frame (at least one image is made) */
    gfx_image *aq[ASYNC_QUEUE];
    int aq_head, aq_n;
    int pc_cursor;            /* gpu_video_precache: where the next scan starts */
    long n_async, n_orig_blits;   /* textures made from the queue, blits drawn from the original meanwhile (per interval) */
};

static double now_ms(void)
{
    return (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency();
}

static unsigned next_tag = 1;

/* Messages of the GPU path: logcat on Android (stderr goes nowhere there), stderr elsewhere. */
void gpu_video_log(const char *fmt, ...)
{
    char buf[600];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
#ifdef __ANDROID__
    SDL_Log("WWF: %s", buf);
#else
    fprintf(stderr, "%s\n", buf);
#endif
}

typedef struct {
    GLint fbo, prog, active, abuf, ebuf, vp[4], sc[4], unpack;
    GLint tex[3];
    GLint attr[4];
    GLfloat clear[4];
    GLboolean cmask[4];
    GLboolean scissor, blend, dither, depth, cull, stencil;
} gl_saved;

static void fail(gpu_video *g, const char *what, unsigned code)
{
    if (!g->failed)
        gpu_video_log("GPU path: %s failed (GL error 0x%x), going back to the CPU path", what, code);
    g->failed = 1;
}

static int check_gl(gpu_video *g, const char *what)
{
    GLenum e = g->gl.GetError();
    if (e != GL_NO_ERROR) {
        fail(g, what, (unsigned)e);
        return 0;
    }
    return 1;
}

static void own_tex(gpu_video *g, GLuint id)
{
    if (g->nowned == g->owned_cap) {
        int nc = g->owned_cap ? g->owned_cap * 2 : 256;
        GLuint *p = realloc(g->owned, (size_t)nc * sizeof *p);
        if (!p)
            return;
        g->owned = p;
        g->owned_cap = nc;
    }
    g->owned[g->nowned++] = id;
}

static void del_tex(gpu_video *g, GLuint id)
{
    if (!id)
        return;
    g->gl.DeleteTextures(1, &id);
    for (int i = 0; i < g->nowned; i++)
        if (g->owned[i] == id) {
            g->owned[i] = g->owned[--g->nowned];
            break;
        }
}

static GLuint new_tex(gpu_video *g)
{
    GLuint t = 0;
    g->gl.GenTextures(1, &t);
    if (!t)
        return 0;
    own_tex(g, t);
    g->gl.ActiveTexture(GL_TEXTURE2);     /* unit 2 is for setting textures up; 0 and 1 are what draws use */
    g->gl.BindTexture(GL_TEXTURE_2D, t);
    g->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    g->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    g->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    g->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

static void save_state(gpu_video *g, gl_saved *s)
{
    const gl_api *gl = &g->gl;
    gl->GetIntegerv(GL_FRAMEBUFFER_BINDING, &s->fbo);
    gl->GetIntegerv(GL_CURRENT_PROGRAM, &s->prog);
    gl->GetIntegerv(GL_ACTIVE_TEXTURE, &s->active);
    gl->GetIntegerv(GL_ARRAY_BUFFER_BINDING, &s->abuf);
    gl->GetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &s->ebuf);
    gl->GetIntegerv(GL_VIEWPORT, s->vp);
    gl->GetIntegerv(GL_SCISSOR_BOX, s->sc);
    gl->GetIntegerv(GL_UNPACK_ALIGNMENT, &s->unpack);
    for (int i = 0; i < 3; i++) {
        gl->ActiveTexture(GL_TEXTURE0 + (GLenum)i);
        gl->GetIntegerv(GL_TEXTURE_BINDING_2D, &s->tex[i]);
    }
    for (int i = 0; i < 4; i++)
        gl->GetVertexAttribiv((GLuint)i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &s->attr[i]);
    gl->GetFloatv(GL_COLOR_CLEAR_VALUE, s->clear);
    gl->GetBooleanv(GL_COLOR_WRITEMASK, s->cmask);
    s->scissor = gl->IsEnabled(GL_SCISSOR_TEST);
    s->blend = gl->IsEnabled(GL_BLEND);
    s->dither = gl->IsEnabled(GL_DITHER);
    s->depth = gl->IsEnabled(GL_DEPTH_TEST);
    s->cull = gl->IsEnabled(GL_CULL_FACE);
    s->stencil = gl->IsEnabled(GL_STENCIL_TEST);
}

static void restore_state(gpu_video *g, const gl_saved *s)
{
    const gl_api *gl = &g->gl;
    gl->BindFramebuffer(GL_FRAMEBUFFER, (GLuint)s->fbo);
    gl->UseProgram((GLuint)s->prog);
    for (int i = 0; i < 3; i++) {
        gl->ActiveTexture(GL_TEXTURE0 + (GLenum)i);
        gl->BindTexture(GL_TEXTURE_2D, (GLuint)s->tex[i]);
    }
    gl->ActiveTexture((GLenum)s->active);
    gl->BindBuffer(GL_ARRAY_BUFFER, (GLuint)s->abuf);
    gl->BindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)s->ebuf);
    gl->Viewport(s->vp[0], s->vp[1], s->vp[2], s->vp[3]);
    gl->Scissor(s->sc[0], s->sc[1], s->sc[2], s->sc[3]);
    gl->PixelStorei(GL_UNPACK_ALIGNMENT, s->unpack);
    for (int i = 0; i < 4; i++) {
        if (s->attr[i])
            gl->EnableVertexAttribArray((GLuint)i);
        else
            gl->DisableVertexAttribArray((GLuint)i);
    }
    gl->ClearColor(s->clear[0], s->clear[1], s->clear[2], s->clear[3]);
    gl->ColorMask(s->cmask[0], s->cmask[1], s->cmask[2], s->cmask[3]);
#define RESTORE(cap, v) do { if (v) gl->Enable(cap); else gl->Disable(cap); } while (0)
    RESTORE(GL_SCISSOR_TEST, s->scissor);
    RESTORE(GL_BLEND, s->blend);
    RESTORE(GL_DITHER, s->dither);
    RESTORE(GL_DEPTH_TEST, s->depth);
    RESTORE(GL_CULL_FACE, s->cull);
    RESTORE(GL_STENCIL_TEST, s->stencil);
#undef RESTORE
}

/* ---- creating the GL objects --------------------------------------------------------------------- */

static int load_gl(gpu_video *g, char *err, size_t n)
{
#define X(ret, name, args) \
    do { \
        void *p_ = SDL_GL_GetProcAddress("gl" #name); \
        if (!p_) { \
            snprintf(err, n, "GL entry point gl%s is missing", #name); \
            return 0; \
        } \
        memcpy(&g->gl.name, &p_, sizeof p_); \
    } while (0);
    GL_FUNCS(X)
#undef X
    return 1;
}

static GLuint compile(gpu_video *g, GLenum type, const char *src, char *err, size_t n)
{
    const gl_api *gl = &g->gl;
    GLuint s = gl->CreateShader(type);
    GLint ok = 0;
    gl->ShaderSource(s, 1, &src, NULL);
    gl->CompileShader(s);
    gl->GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[400] = "";
        gl->GetShaderInfoLog(s, sizeof log, NULL, log);
        for (char *c = log; *c; c++)
            if (*c == '\n')
                *c = ' ';
        snprintf(err, n, "%s shader: %s", type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        gl->DeleteShader(s);
        return 0;
    }
    return s;
}

static int make_program(gpu_video *g, gprog *p, const char *fs, char *err, size_t n)
{
    const gl_api *gl = &g->gl;
    GLuint v = compile(g, GL_VERTEX_SHADER, vs_src, err, n);
    GLuint f = v ? compile(g, GL_FRAGMENT_SHADER, fs, err, n) : 0;
    GLint ok = 0;
    if (!v || !f) {
        if (v)
            gl->DeleteShader(v);
        return 0;
    }
    p->id = gl->CreateProgram();
    gl->AttachShader(p->id, v);
    gl->AttachShader(p->id, f);
    gl->BindAttribLocation(p->id, 0, "a");
    gl->LinkProgram(p->id);
    gl->DeleteShader(v);
    gl->DeleteShader(f);
    gl->GetProgramiv(p->id, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[400] = "";
        gl->GetProgramInfoLog(p->id, sizeof log, NULL, log);
        snprintf(err, n, "shader link: %s", log);
        gl->DeleteProgram(p->id);
        p->id = 0;
        return 0;
    }
#define U(field, name) p->field = gl->GetUniformLocation(p->id, name)
    U(rect, "u_rect"); U(inv2, "u_inv2");
    U(src, "u_src"); U(det, "u_det"); U(idx, "u_idx"); U(pal, "u_pal");
    U(org, "u_org"); U(sx, "u_sx"); U(sy, "u_sy"); U(geo, "u_geo"); U(flip, "u_flip"); U(tex, "u_tex");
    U(modes, "u_modes"); U(val, "u_val"); U(pass, "u_pass"); U(fb, "u_fb"); U(high, "u_high");
    U(usedet, "u_usedet");
#undef U
    return 1;
}

static void free_targets(gpu_video *g)
{
    const gl_api *gl = &g->gl;
    if (g->idx_fbo)
        gl->DeleteFramebuffers(1, &g->idx_fbo);
    if (g->det_fbo)
        gl->DeleteFramebuffers(1, &g->det_fbo);
    del_tex(g, g->idx_tex);
    del_tex(g, g->det_tex);
    g->idx_fbo = g->det_fbo = g->idx_tex = g->det_tex = 0;
    g->tw = g->th = 0;
}

/* Frees the GL objects. `delete` is 0 when the context is gone and the names mean nothing. */
static void free_gl(gpu_video *g, int del)
{
    const gl_api *gl = &g->gl;
    if (del && g->made) {
        free_targets(g);
        for (int i = g->nowned - 1; i >= 0; i--)
            gl->DeleteTextures(1, &g->owned[i]);
        if (g->blit.id)
            gl->DeleteProgram(g->blit.id);
        if (g->fin.id)
            gl->DeleteProgram(g->fin.id);
        if (g->vbo)
            gl->DeleteBuffers(1, &g->vbo);
        if (g->out_fbo)
            gl->DeleteFramebuffers(1, &g->out_fbo);
    }
    g->nowned = 0;
    memset(&g->blit, 0, sizeof g->blit);
    memset(&g->fin, 0, sizeof g->fin);
    g->vbo = g->out_fbo = g->pal_tex = g->rb_tex = 0;
    g->idx_fbo = g->det_fbo = g->idx_tex = g->det_tex = 0;
    g->tw = g->th = g->rb_w = g->rb_h = 0;
    g->pal_valid = 0;
    g->made = 0;
    g->aq_head = g->aq_n = 0;  /* the images' sink_queued holds the old tag, so they can queue again */
    g->tag = next_tag++;       /* every gfx_image texture of the old objects is void */
}

static int make_gl(gpu_video *g, char *err, size_t n)
{
    const gl_api *gl = &g->gl;
    static const GLfloat quad[8] = {0, 0, 1, 0, 0, 1, 1, 1};
    GLint mt = 0;
    gl->GetIntegerv(GL_MAX_TEXTURE_SIZE, &mt);
    g->max_tex = mt;
    if (!make_program(g, &g->blit, fs_blit_src, err, n) || !make_program(g, &g->fin, fs_final_src, err, n)) {
        free_gl(g, 1);
        return 0;
    }
    gl->GenBuffers(1, &g->vbo);
    gl->BindBuffer(GL_ARRAY_BUFFER, g->vbo);
    gl->BufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    gl->GenFramebuffers(1, &g->out_fbo);
    g->pal_tex = new_tex(g);
    gl->PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, VIDEO_PALETTES, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    g->made = 1;
    if (gl->GetError() != GL_NO_ERROR) {
        snprintf(err, n, "GL error while setting up");
        free_gl(g, 1);
        return 0;
    }
    return 1;
}

/* Makes the GL context current and the state ours. 0 if the GPU path is unusable. */
static int enter(gpu_video *g, gl_saved *s)
{
    if (g->failed)
        return 0;
#if SDL_VERSION_ATLEAST(2, 0, 10)
    SDL_RenderFlush(g->ren);
#endif
    if (SDL_GL_GetCurrentContext() != g->ctx && SDL_GL_MakeCurrent(g->win, g->ctx) != 0) {
        fail(g, "making the GL context current", 0);
        return 0;
    }
    while (g->gl.GetError() != GL_NO_ERROR)
        ;
    save_state(g, s);
    if (!g->made) {
        char err[300];
        if (!make_gl(g, err, sizeof err)) {
            gpu_video_log("GPU path: %s", err);
            g->failed = 1;
            restore_state(g, s);
            return 0;
        }
    }
    g->gl.Disable(GL_BLEND);
    g->gl.Disable(GL_DEPTH_TEST);
    g->gl.Disable(GL_CULL_FACE);
    g->gl.Disable(GL_STENCIL_TEST);
    g->gl.Disable(GL_DITHER);
    g->gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    g->gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    return 1;
}

static void leave(gpu_video *g, const gl_saved *s)
{
    restore_state(g, s);
}

/* ---- targets --------------------------------------------------------------------------------------- */

static int make_target(gpu_video *g, int w, int h, GLuint *tex, GLuint *fbo)
{
    const gl_api *gl = &g->gl;
    *tex = new_tex(g);
    if (!*tex)
        return 0;
    gl->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    gl->GenFramebuffers(1, fbo);
    gl->BindFramebuffer(GL_FRAMEBUFFER, *fbo);
    gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *tex, 0);
    if (gl->CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 0;
    gl->Disable(GL_SCISSOR_TEST);
    gl->ClearColor(0, 0, 0, 0);
    gl->Clear(GL_COLOR_BUFFER_BIT);
    return 1;
}

/* Index texture of the size of v's framebuffer (and the detail texture when asked for). */
static int ensure_targets(gpu_video *g, const video *v, int need_det)
{
    if (g->idx_tex && (g->tw != v->w || g->th != v->h))
        free_targets(g);
    if (!g->idx_tex) {
        if (v->w > g->max_tex || v->h > g->max_tex) {
            gpu_video_log("GPU path: the %dx%d framebuffer is larger than the GPU's texture limit %d", v->w, v->h,
                          g->max_tex);
            g->failed = 1;
            return 0;
        }
        g->tw = v->w;
        g->th = v->h;
        if (!make_target(g, v->w, v->h, &g->idx_tex, &g->idx_fbo)) {
            fail(g, "creating the index framebuffer", g->gl.GetError());
            return 0;
        }
    }
    if (need_det && !g->det_tex && !make_target(g, v->w, v->h, &g->det_tex, &g->det_fbo)) {
        fail(g, "creating the detail framebuffer", g->gl.GetError());
        return 0;
    }
    return 1;
}

/* ---- source textures ------------------------------------------------------------------------------- */

static GLuint image_tex(gpu_video *g, gfx_image *gi, int slot, int *tw, int *th)
{
    const gl_api *gl = &g->gl;
    const img_image *im = gi->img;
    if (gi->sink_owner != g->tag || gi->sink_gen != gi->gen) {
        if (gi->sink_owner == g->tag)
            for (int i = 0; i < 3; i++)
                del_tex(g, gi->sink_tex[i]);
        memset(gi->sink_tex, 0, sizeof gi->sink_tex);
        gi->sink_owner = g->tag;
        gi->sink_gen = gi->gen;
    }
    int w = im->width, h = im->height;
    if (slot > 0) {
        if (!gi->hi || gi->hi_state != 1)
            return 0;
        w *= gi->hi_factor;
        h *= gi->hi_factor;
    }
    *tw = w;
    *th = h;
    if (gi->sink_tex[slot])
        return gi->sink_tex[slot];
    if (w > g->max_tex || h > g->max_tex || w < 1 || h < 1)
        return 0;
    uint8_t *buf = malloc((size_t)w * h * (slot == 2 ? 4 : 1));
    if (!buf)
        return 0;
    if (slot == 0) {
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                buf[(size_t)y * w + x] = im->pixels[(size_t)y * im->stride + x] & gi->mask;
    } else if (slot == 1) {
        memcpy(buf, gi->hi, (size_t)w * h);
    } else {
        if (!gi->hi_detail) {
            free(buf);
            return 0;
        }
        for (size_t i = 0; i < (size_t)w * h; i++) {
            uint32_t d = gi->hi_detail[i];
            buf[4 * i] = (uint8_t)d;
            buf[4 * i + 1] = (uint8_t)(d >> 8);
            buf[4 * i + 2] = (uint8_t)(d >> 16);
            buf[4 * i + 3] = (uint8_t)(d >> 24);
        }
    }
    GLuint t = new_tex(g);
    if (t) {
        GLenum fmt = slot == 2 ? GL_RGBA : GL_LUMINANCE;
        double t0 = now_ms();
        gl->TexImage2D(GL_TEXTURE_2D, 0, (GLint)fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, buf);
        g->ms_upload += now_ms() - t0;
        g->n_upload++;
        g->bytes_upload += (long)w * h * (slot == 2 ? 4 : 1);
        gi->sink_tex[slot] = t;
    }
    free(buf);
    return t;
}

/* ---- the display list ------------------------------------------------------------------------------ */

static void set_viewport_target(gpu_video *g, int w, int h)
{
    g->gl.Viewport(0, 0, w, h);
}

static void draw_quad(gpu_video *g, const gprog *p, int x, int y, int w, int h, int tw, int th)
{
    const gl_api *gl = &g->gl;
    gl->Uniform4f(p->rect, (GLfloat)x, (GLfloat)y, (GLfloat)w, (GLfloat)h);
    gl->Uniform2f(p->inv2, 2.0f / (GLfloat)tw, 2.0f / (GLfloat)th);
    gl->DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static void play(gpu_video *g, int pass)
{
    const gl_api *gl = &g->gl;
    GLuint fbo = pass ? g->det_fbo : g->idx_fbo;
    int scissor = 0;
    gl->BindFramebuffer(GL_FRAMEBUFFER, fbo);
    set_viewport_target(g, g->tw, g->th);
    gl->UseProgram(g->blit.id);
    gl->BindBuffer(GL_ARRAY_BUFFER, g->vbo);
    gl->VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    gl->EnableVertexAttribArray(0);
    gl->Uniform1i(g->blit.src, 0);
    gl->Uniform1i(g->blit.det, 1);
    GLuint last_tex = 0, last_det = 0;
    for (int i = 0; i < g->nops; i++) {
        const gop *o = &g->ops[i];
        if (o->type == OP_FILL) {
            if (!scissor) {
                gl->Enable(GL_SCISSOR_TEST);
                scissor = 1;
            }
            gl->Scissor(o->x, o->y, o->w, o->h);
            if (pass)
                gl->ClearColor(0, 0, 0, 0);
            else
                gl->ClearColor((GLfloat)(o->value & 255) / 255.0f, (GLfloat)(o->value >> 8) / 255.0f, 0, 1);
            gl->Clear(GL_COLOR_BUFFER_BIT);
            continue;
        }
        const video_blit_job *j = &o->j;
        gfx_image *gi = (gfx_image *)j->image;
        int tw = 0, th = 0, dw = 0, dh = 0;
        GLuint tex = image_tex(g, gi, j->use_hi ? 1 : 0, &tw, &th);
        if (!tex)
            continue;
        GLuint det = 0;
        if (pass && j->detail && j->use_hi)
            det = image_tex(g, gi, 2, &dw, &dh);
        if (scissor) {
            gl->Disable(GL_SCISSOR_TEST);
            scissor = 0;
        }
        if (tex != last_tex) {
            gl->ActiveTexture(GL_TEXTURE0);
            gl->BindTexture(GL_TEXTURE_2D, tex);
            last_tex = tex;
        }
        if (pass && det != last_det) {
            gl->ActiveTexture(GL_TEXTURE1);
            gl->BindTexture(GL_TEXTURE_2D, det);
            last_det = det;
        }
        const gprog *p = &g->blit;
        gl->Uniform2f(p->org, (GLfloat)j->x0, (GLfloat)j->y0);
        gl->Uniform3f(p->sx, (GLfloat)(j->step_x >> 12), (GLfloat)(j->step_x & 4095), (GLfloat)((j->step_x & 4095) >> 1));
        gl->Uniform3f(p->sy, (GLfloat)(j->step_y >> 12), (GLfloat)(j->step_y & 4095), (GLfloat)((j->step_y & 4095) >> 1));
        gl->Uniform4f(p->geo, (GLfloat)j->src_x, (GLfloat)j->src_y, (GLfloat)j->src_w, (GLfloat)j->src_h);
        gl->Uniform2f(p->flip, j->flip_h ? 1.0f : 0.0f, j->flip_v ? 1.0f : 0.0f);
        gl->Uniform2f(p->tex, (GLfloat)tw, (GLfloat)th);
        float nzm = j->ctrl & DMA_CNZ ? 1.0f : j->ctrl & DMA_WNZ ? 2.0f : 0.0f;
        float zm = j->ctrl & DMA_CZ ? 1.0f : j->ctrl & DMA_WZ ? 2.0f : 0.0f;
        gl->Uniform2f(p->modes, nzm, zm);
        gl->Uniform3f(p->val, (GLfloat)(j->cval & 255), (GLfloat)(j->cval >> 8), (GLfloat)(j->pal >> 8));
        gl->Uniform2f(p->pass, (GLfloat)pass, det ? 1.0f : 0.0f);
        draw_quad(g, p, j->xs, j->ys, j->xe - j->xs + 1, j->ye - j->ys + 1, g->tw, g->th);
    }
    if (scissor)
        gl->Disable(GL_SCISSOR_TEST);
    gl->ActiveTexture(GL_TEXTURE0);
}

/* Plays the list into the index texture and, when there is detail, the detail texture. 0 on failure. */
static int flush_list(gpu_video *g, const video *v)
{
    if (g->nops == 0)
        return 1;
    int need_det = g->list_detail || g->det_tex;
    if (!ensure_targets(g, v, need_det))
        return 0;
    double t0 = now_ms();
    play(g, 0);
    if (g->det_tex)
        play(g, 1);
    double t1 = now_ms();
    g->ms_play += t1 - t0;
    g->n_flush++;
    if (g->profile) {
        g->gl.Finish();
        g->ms_gpu_play += now_ms() - t1;
    }
    g->nops = 0;
    g->list_detail = 0;
    return check_gl(g, "playing the display list");
}

/* ---- the sink ---------------------------------------------------------------------------------------- */

static gop *push(gpu_video *g)
{
    if (g->nops == g->cap) {
        int nc = g->cap ? g->cap * 2 : 1024;
        gop *p = realloc(g->ops, (size_t)nc * sizeof *p);
        if (!p)
            return NULL;
        g->ops = p;
        g->cap = nc;
    }
    return &g->ops[g->nops++];
}

static void room(gpu_video *g, video *v)
{
    if (g->nops < MAX_OPS)
        return;
    gl_saved s;
    if (enter(g, &s)) {
        flush_list(g, v);
        leave(g, &s);
    }
    if (g->failed)
        g->nops = 0;
}

static int sink_blit_raw(void *user, video *v, const video_blit_job *j)
{
    gpu_video *g = user;
    const img_image *im = j->image->img;
    if (g->failed)
        return 0;
    int w = j->use_hi ? im->width * j->image->hi_factor : im->width;
    int h = j->use_hi ? im->height * j->image->hi_factor : im->height;
    if (w > g->max_tex || h > g->max_tex) {
        if (!g->warned_big) {
            gpu_video_log("GPU path: an image of %dx%d is larger than the GPU's texture limit %d: those frames are "
                          "drawn on the CPU", w, h, g->max_tex);
            g->warned_big = 1;
        }
        return 0;
    }
    room(g, v);
    gop *o = push(g);
    if (!o)
        return 0;
    o->type = OP_BLIT;
    o->j = *j;
    if (j->detail && j->use_hi)
        g->list_detail = 1;
    return 1;
}

static void sink_fill_raw(void *user, video *v, int x, int y, int w, int h, uint16_t value)
{
    gpu_video *g = user;
    if (g->failed)
        return;
    int x1 = x + w, y1 = y + h;
    x = x < 0 ? 0 : x;
    y = y < 0 ? 0 : y;
    x1 = x1 > v->w ? v->w : x1;
    y1 = y1 > v->h ? v->h : y1;
    if (x1 <= x || y1 <= y)
        return;
    /* a run of fills of the same row band with one value (the pixels of a line) becomes one rectangle */
    if (g->nops > 0) {
        gop *l = &g->ops[g->nops - 1];
        if (l->type == OP_FILL && l->value == value && l->y == y && l->h == y1 - y && l->x + l->w == x) {
            l->w = x1 - l->x;
            return;
        }
    }
    room(g, v);
    gop *o = push(g);
    if (!o)
        return;
    o->type = OP_FILL;
    o->x = x;
    o->y = y;
    o->w = x1 - x;
    o->h = y1 - y;
    o->value = value;
}

static int sink_blit(void *user, video *v, const video_blit_job *j)
{
    gpu_video *g = user;
    double t0 = now_ms();
    int r = sink_blit_raw(user, v, j);
    g->ms_record += now_ms() - t0;
    g->n_blit++;
    return r;
}

static void sink_fill(void *user, video *v, int x, int y, int w, int h, uint16_t value)
{
    gpu_video *g = user;
    double t0 = now_ms();
    sink_fill_raw(user, v, x, y, w, h, value);
    g->ms_record += now_ms() - t0;
    g->n_fill++;
    g->fills_total++;
}

static void read_target(gpu_video *g, GLuint fbo, int w, int h, uint8_t *buf, int rows, int y0)
{
    g->gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    g->gl.ReadPixels(0, y0, w, rows, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    (void)h;
}

static int sink_read_pixel(void *user, video *v, int x, int y)
{
    gpu_video *g = user;
    gl_saved s;
    uint8_t px[4] = {0, 0, 0, 0};
    if (g->failed || !enter(g, &s))
        return -1;
    if (!flush_list(g, v) || !g->idx_tex || g->tw != v->w || g->th != v->h || x < 0 || y < 0 || x >= v->w || y >= v->h) {
        leave(g, &s);
        return -1;
    }
    g->gl.BindFramebuffer(GL_FRAMEBUFFER, g->idx_fbo);
    g->gl.ReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    leave(g, &s);
    g->px_reads++;
    g->px_total++;
    return px[0] | px[1] << 8;
}

static void sink_read_back(void *user, video *v)
{
    gpu_video *g = user;
    gl_saved s;
    if (g->failed || !enter(g, &s))
        return;
    g->readbacks++;
    {
        int k;
        for (k = 0; k < g->rb_tags && g->rb_tag[k] != v->sync_tag; k++)
            ;
        if (k == g->rb_tags && k < 8) {
            g->rb_tag[k] = v->sync_tag;
            g->rb_tag_n[k] = 0;
            g->rb_tags++;
        }
        if (k < 8)
            g->rb_tag_n[k]++;
    }
    flush_list(g, v);
    if (g->idx_tex && g->tw == v->w && g->th == v->h && !g->failed) {
        enum { ROWS = 128 };
        uint8_t *buf = malloc((size_t)v->w * ROWS * 4);
        if (buf) {
            for (int y = 0; y < v->h; y += ROWS) {
                int rows = v->h - y < ROWS ? v->h - y : ROWS;
                read_target(g, g->idx_fbo, v->w, v->h, buf, rows, y);
                for (size_t i = 0; i < (size_t)v->w * rows; i++)
                    v->fb[(size_t)y * v->w + i] = (uint16_t)(buf[4 * i] | buf[4 * i + 1] << 8);
            }
            if (g->det_tex) {
                if (!v->detail)
                    v->detail = calloc((size_t)v->w * v->h, sizeof *v->detail);
                if (v->detail)
                    for (int y = 0; y < v->h; y += ROWS) {
                        int rows = v->h - y < ROWS ? v->h - y : ROWS;
                        read_target(g, g->det_fbo, v->w, v->h, buf, rows, y);
                        for (size_t i = 0; i < (size_t)v->w * rows; i++)
                            v->detail[(size_t)y * v->w + i] = (uint32_t)buf[4 * i] | (uint32_t)buf[4 * i + 1] << 8 |
                                                              (uint32_t)buf[4 * i + 2] << 16 | (uint32_t)buf[4 * i + 3] << 24;
                    }
            }
            free(buf);
        }
        check_gl(g, "reading the picture back");
    } else if (!g->idx_tex) {
        memset(v->fb, 0, (size_t)v->w * v->h * sizeof *v->fb);    /* nothing was ever drawn */
        if (v->detail)
            memset(v->detail, 0, (size_t)v->w * v->h * sizeof *v->detail);
    }
    leave(g, &s);
}

/* The CPU copy is the one that counts: send it to the GPU. */
static int upload_fb(gpu_video *g, video *v)
{
    const gl_api *gl = &g->gl;
    g->nops = 0;
    g->list_detail = 0;
    if (g->idx_tex && (g->tw != v->w || g->th != v->h))
        free_targets(g);
    if (!ensure_targets(g, v, v->detail != NULL))
        return 0;
    enum { ROWS = 128 };
    uint8_t *buf = malloc((size_t)v->w * ROWS * 4);
    if (!buf)
        return 0;
    gl->ActiveTexture(GL_TEXTURE2);
    gl->BindTexture(GL_TEXTURE_2D, g->idx_tex);
    for (int y = 0; y < v->h; y += ROWS) {
        int rows = v->h - y < ROWS ? v->h - y : ROWS;
        for (size_t i = 0; i < (size_t)v->w * rows; i++) {
            uint16_t px = v->fb[(size_t)y * v->w + i];
            buf[4 * i] = (uint8_t)px;
            buf[4 * i + 1] = (uint8_t)(px >> 8);
            buf[4 * i + 2] = 0;
            buf[4 * i + 3] = 255;
        }
        gl->TexSubImage2D(GL_TEXTURE_2D, 0, 0, y, v->w, rows, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    }
    if (g->det_tex) {
        gl->BindTexture(GL_TEXTURE_2D, g->det_tex);
        if (v->detail) {
            for (int y = 0; y < v->h; y += ROWS) {
                int rows = v->h - y < ROWS ? v->h - y : ROWS;
                for (size_t i = 0; i < (size_t)v->w * rows; i++) {
                    uint32_t d = v->detail[(size_t)y * v->w + i];
                    buf[4 * i] = (uint8_t)d;
                    buf[4 * i + 1] = (uint8_t)(d >> 8);
                    buf[4 * i + 2] = (uint8_t)(d >> 16);
                    buf[4 * i + 3] = (uint8_t)(d >> 24);
                }
                gl->TexSubImage2D(GL_TEXTURE_2D, 0, 0, y, v->w, rows, GL_RGBA, GL_UNSIGNED_BYTE, buf);
            }
        } else {
            gl->BindFramebuffer(GL_FRAMEBUFFER, g->det_fbo);
            gl->Disable(GL_SCISSOR_TEST);
            gl->ClearColor(0, 0, 0, 0);
            gl->Clear(GL_COLOR_BUFFER_BIT);
        }
    }
    free(buf);
    v->cpu_auth = 0;
    return check_gl(g, "uploading the framebuffer");
}

/* ---- the present pass ------------------------------------------------------------------------------- */

static void update_palette(gpu_video *g, const video *v)
{
    if (g->pal_valid && memcmp(g->col_shadow, v->colram, sizeof g->col_shadow) == 0)
        return;
    uint8_t *buf = malloc((size_t)VIDEO_COLORS * 4);
    if (!buf)
        return;
    for (int i = 0; i < VIDEO_COLORS; i++) {
        uint32_t c = img_color_argb(v->colram[i]);
        buf[4 * i] = (uint8_t)(c >> 16);
        buf[4 * i + 1] = (uint8_t)(c >> 8);
        buf[4 * i + 2] = (uint8_t)c;
        buf[4 * i + 3] = (uint8_t)(c >> 24);
    }
    g->gl.ActiveTexture(GL_TEXTURE2);
    g->gl.BindTexture(GL_TEXTURE_2D, g->pal_tex);
    g->gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, VIDEO_PALETTES, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    free(buf);
    memcpy(g->col_shadow, v->colram, sizeof g->col_shadow);
    g->pal_valid = 1;
}

/*
 * Converts the framebuffer into the part (tx, ty, w, h) of the texture `target` (tw x th). Texture pixel
 * (tx + i, ty + j) shows the view pixel (ax + i, ay + j), as video_to_argb_area(ax, ay) would give it.
 */
static int final_pass(gpu_video *g, video *v, GLuint target, int tw, int th, int tx, int ty, int w, int h, int ax, int ay)
{
    const gl_api *gl = &g->gl;
    if (v->cpu_auth && !upload_fb(g, v))
        return 0;
    if (!flush_list(g, v) || !ensure_targets(g, v, 0))
        return 0;
    update_palette(g, v);
    gl->BindFramebuffer(GL_FRAMEBUFFER, g->out_fbo);
    gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
    if (gl->CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fail(g, "the frame texture as render target", 0);
        return 0;
    }
    gl->Disable(GL_SCISSOR_TEST);
    gl->Viewport(0, 0, tw, th);
    const gprog *p = &g->fin;
    gl->UseProgram(p->id);
    gl->ActiveTexture(GL_TEXTURE0);
    gl->BindTexture(GL_TEXTURE_2D, g->idx_tex);
    gl->ActiveTexture(GL_TEXTURE1);
    gl->BindTexture(GL_TEXTURE_2D, g->det_tex);
    gl->ActiveTexture(GL_TEXTURE2);
    gl->BindTexture(GL_TEXTURE_2D, g->pal_tex);
    gl->Uniform1i(p->idx, 0);
    gl->Uniform1i(p->det, 1);
    gl->Uniform1i(p->pal, 2);
    gl->Uniform1f(p->usedet, g->det_tex ? 1.0f : 0.0f);
    gl->Uniform2f(p->org, (GLfloat)((v->view_x - v->view_pad) * v->scale + ax - tx),
                  (GLfloat)((v->view_y - v->view_pad_y) * v->scale + ay - ty));
    gl->Uniform2f(p->fb, (GLfloat)v->w, (GLfloat)v->h);
    uint32_t hp = v->high_pal_argb;
    gl->Uniform4f(p->high, (GLfloat)(hp >> 16 & 255) / 255.0f, (GLfloat)(hp >> 8 & 255) / 255.0f,
                  (GLfloat)(hp & 255) / 255.0f, (GLfloat)(hp >> 24) / 255.0f);
    gl->BindBuffer(GL_ARRAY_BUFFER, g->vbo);
    gl->VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    gl->EnableVertexAttribArray(0);
    draw_quad(g, p, tx, ty, w, h, tw, th);
    gl->ActiveTexture(GL_TEXTURE0);
    return check_gl(g, "the present pass");
}

/* ---- async compute --------------------------------------------------------------------------------- */

/* Everything a blit of this override needs is on the GPU (or it cannot be there: too big for a texture, which
 * sink_blit_raw refuses as it always did). */
static int hi_textures_ready(const gpu_video *g, const gfx_image *gi)
{
    if (gi->sink_owner == g->tag && gi->sink_gen == gi->gen && gi->sink_tex[1] && (!gi->hi_detail || gi->sink_tex[2]))
        return 1;
    return gi->img->width * gi->hi_factor > g->max_tex || gi->img->height * gi->hi_factor > g->max_tex;
}

/* Is everything a blit of this override needs on the GPU? If not, queues the image and says no: video_dma then
 * records the blit from the original pixels. Only bookkeeping, no GL call (this runs while the game draws). */
static int sink_hi_ready(void *user, const gfx_image *ci)
{
    gpu_video *g = user;
    gfx_image *gi = (gfx_image *)ci;
    if (g->failed || hi_textures_ready(g, gi))
        return 1;
    if (gi->sink_queued != g->tag && g->aq_n < ASYNC_QUEUE) {
        g->aq[(g->aq_head + g->aq_n++) % ASYNC_QUEUE] = gi;
        gi->sink_queued = g->tag;
    }
    g->n_orig_blits++;
    return 0;
}

void gpu_video_precache(gpu_video *g, gfx_image *imgs, int n, double budget_ms, int *total, int *loaded, int *on_gpu)
{
    int tot = 0, ld = 0, up = 0, entered = 0;
    gl_saved st;
    double t0 = now_ms();
    if (!g->failed && n > 0) {
        for (int k = 0; k < n; k++) {
            int i = (g->pc_cursor + k) % n;
            gfx_image *gi = &imgs[i];
            if (!gi->entry || !gi->entry->has_override || gi->hi_off || gi->hi_state != 1 || !gi->hi ||
                hi_textures_ready(g, gi))
                continue;
            if (!entered) {
                if (!enter(g, &st))
                    break;
                entered = 1;
                g->gl.ActiveTexture(GL_TEXTURE0);
            }
            int tw, th;
            image_tex(g, gi, 1, &tw, &th);
            if (gi->hi_detail)
                image_tex(g, gi, 2, &tw, &th);
            g->n_async++;
            g->pc_cursor = (i + 1) % n;
            if (now_ms() - t0 >= budget_ms)
                break;
        }
        if (entered) {
            check_gl(g, "making the override textures");
            leave(g, &st);
        }
    }
    for (int i = 0; i < n; i++) {
        const gfx_image *gi = &imgs[i];
        if (!gi->entry || !gi->entry->has_override || gi->hi_off)
            continue;
        tot++;
        if (gi->hi_state == 1 && gi->hi) {
            ld++;
            up += g->failed || hi_textures_ready(g, gi);   /* without a GPU there is nothing to wait for */
        }
    }
    *total = tot;
    *loaded = ld;
    *on_gpu = up;
}

/* Makes the textures of queued overrides until the frame's budget is spent (the first one always, so the queue moves).
 * Runs with the context current, before the picture is drawn. */
static void async_pump(gpu_video *g)
{
    if (!g->async || g->aq_n == 0)
        return;
    double t0 = now_ms();
    while (g->aq_n > 0) {
        gfx_image *gi = g->aq[g->aq_head];
        g->aq_head = (g->aq_head + 1) % ASYNC_QUEUE;
        g->aq_n--;
        gi->sink_queued = 0;
        if (gi->hi && gi->hi_state == 1) {      /* it may have been freed again while it waited */
            int tw, th;
            image_tex(g, gi, 1, &tw, &th);
            if (gi->hi_detail)
                image_tex(g, gi, 2, &tw, &th);
            g->n_async++;
        }
        if (now_ms() - t0 >= g->async_ms)
            break;
    }
    check_gl(g, "making the override textures");
}

void gpu_video_set_async(gpu_video *g, int on, double budget_ms)
{
    if (!g)
        return;
    g->async = on != 0;
    g->async_ms = budget_ms > 0 ? budget_ms : 2.0;
    g->sink.hi_ready = on ? sink_hi_ready : NULL;
    if (!on) {
        while (g->aq_n > 0) {
            g->aq[g->aq_head]->sink_queued = 0;
            g->aq_head = (g->aq_head + 1) % ASYNC_QUEUE;
            g->aq_n--;
        }
    }
}

int gpu_video_render(gpu_video *g, video *v, SDL_Texture *tex, SDL_Rect src)
{
    gl_saved s;
    float fw = 0, fh = 0;
    int tw = 0, th = 0, ok = 0;
    double t0 = now_ms();
    if (!enter(g, &s))
        return 0;
    g->gl.ActiveTexture(GL_TEXTURE0);
    async_pump(g);
    if (g->failed) {
        leave(g, &s);
        return 0;
    }
    SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
    g->gl.ActiveTexture(GL_TEXTURE0);
    if (SDL_GL_BindTexture(tex, &fw, &fh) == 0) {
        GLint id = 0;
        g->gl.GetIntegerv(GL_TEXTURE_BINDING_2D, &id);
        ok = final_pass(g, v, (GLuint)id, tw, th, src.x, src.y, src.w, src.h, src.x, src.y);
    } else {
        fail(g, "binding the frame texture (SDL_GL_BindTexture)", 0);
    }
    double t1 = now_ms();
    v->px_reads = 0;
    g->ms_final += t1 - t0;
    if (g->profile && ok) {
        g->gl.Finish();
        g->ms_gpu_final += now_ms() - t1;
    }
    leave(g, &s);
    return ok;
}

int gpu_video_to_argb(gpu_video *g, video *v, uint32_t *out, SDL_Rect src)
{
    gl_saved s;
    const gl_api *gl = &g->gl;
    int ok = 0;
    if (!enter(g, &s))
        return 0;
    if (g->rb_w < src.w || g->rb_h < src.h) {
        del_tex(g, g->rb_tex);
        g->rb_tex = new_tex(g);
        g->rb_w = src.w > g->rb_w ? src.w : g->rb_w;
        g->rb_h = src.h > g->rb_h ? src.h : g->rb_h;
        gl->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g->rb_w, g->rb_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    }
    if (final_pass(g, v, g->rb_tex, g->rb_w, g->rb_h, 0, 0, src.w, src.h, src.x, src.y)) {
        uint8_t *buf = malloc((size_t)src.w * src.h * 4);
        if (buf) {
            gl->ReadPixels(0, 0, src.w, src.h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
            for (size_t i = 0; i < (size_t)src.w * src.h; i++)
                out[i] = (uint32_t)buf[4 * i + 3] << 24 | (uint32_t)buf[4 * i] << 16 | (uint32_t)buf[4 * i + 1] << 8 |
                         buf[4 * i + 2];
            free(buf);
            ok = check_gl(g, "reading the converted picture");
        }
    }
    leave(g, &s);
    return ok;
}

/* ---- creating and destroying ------------------------------------------------------------------------- */

void gpu_video_activity(gpu_video *g, long *fills, long *readbacks)
{
    *fills = g->fills_total - g->act_fill;
    *readbacks = g->readbacks - g->act_rb;
    g->act_fill = g->fills_total;
    g->act_rb = g->readbacks;
}

void gpu_video_totals(const gpu_video *g, long *readbacks, long *pixel_reads)
{
    *readbacks = g->readbacks - g->selftest_readbacks;
    *pixel_reads = g->px_total;
}

int gpu_video_suspended(const gpu_video *g)
{
    return g->suspended;
}

void gpu_video_suspend(gpu_video *g, video *v)
{
    if (g->suspended || v->sink != &g->sink)
        return;
    if (!g->failed && !v->cpu_auth)
        video_sync_cpu(v);                 /* the CPU copy now holds the picture */
    video_set_sink(v, NULL);
    v->cpu_auth = 0;
    g->suspended = 1;
    g->act_fill = g->fills_total;
    g->act_rb = g->readbacks;
    gpu_video_log("GPU path: paused, the picture moves pixel by pixel (CPU drawing for now)");
}

void gpu_video_resume(gpu_video *g, video *v)
{
    if (!g->suspended)
        return;
    g->suspended = 0;
    g->nops = 0;
    g->list_detail = 0;
    video_set_sink(v, &g->sink);           /* the CPU copy counts until the next present has uploaded it */
    g->act_fill = g->fills_total;
    g->act_rb = g->readbacks;
    gpu_video_log("GPU path: resumed");
}

int gpu_video_readback_callers(gpu_video *g, uint32_t *tag, long *count, int max)
{
    int n = g->rb_tags < max ? g->rb_tags : max;
    for (int i = 0; i < n; i++) {
        tag[i] = g->rb_tag[i];
        count[i] = g->rb_tag_n[i];
    }
    g->rb_tags = 0;
    return n;
}

void gpu_video_release_images(gpu_video *g, gfx_image **imgs, int n)
{
    gl_saved s;
    if (g->failed || !enter(g, &s))
        return;
    for (int k = 0; k < n; k++) {
        gfx_image *gi = imgs[k];
        if (gi->sink_owner != g->tag)
            continue;
        for (int i = 1; i < 3; i++) {          /* the override's pixels and detail; the original's stay */
            del_tex(g, gi->sink_tex[i]);
            gi->sink_tex[i] = 0;
        }
    }
    leave(g, &s);
}

void gpu_video_set_profile(gpu_video *g, int on)
{
    if (g)
        g->profile = on;
}

void gpu_video_stats(gpu_video *g, char *out, size_t n)
{
    snprintf(out, n,
             "gpu: record %.0f ms (%ld blits, %ld fills), play %.0f ms (%ld flushes), final %.0f ms, texture uploads %.0f ms "
             "(%ld, %ld KB), pixel reads %ld, readbacks %ld%s",
             g->ms_record, g->n_blit, g->n_fill, g->ms_play, g->n_flush, g->ms_final, g->ms_upload, g->n_upload,
             g->bytes_upload / 1024, g->px_reads, g->readbacks - g->rb_start, "");
    if (g->async) {
        size_t l = strlen(out);
        snprintf(out + l, n - l, ", async compute: %ld textures made from the queue, %ld blits drawn from the original "
                 "meanwhile, %d waiting", g->n_async, g->n_orig_blits, g->aq_n);
    }
    g->n_async = g->n_orig_blits = 0;
    if (g->profile) {
        size_t l = strlen(out);
        snprintf(out + l, n - l, ", GPU busy after play %.0f ms, after final %.0f ms", g->ms_gpu_play, g->ms_gpu_final);
    }
    g->ms_record = g->ms_play = g->ms_final = g->ms_gpu_play = g->ms_gpu_final = g->ms_upload = 0;
    g->n_blit = g->n_fill = g->n_flush = g->n_upload = g->bytes_upload = 0;
    g->rb_start = g->readbacks;
    g->px_reads = 0;
}

void gpu_video_context_lost(gpu_video *g)
{
    free_gl(g, 0);
    g->nops = 0;
    g->list_detail = 0;
}

const char *gpu_video_info(const gpu_video *g)
{
    return g->info;
}

gpu_video *gpu_video_create(SDL_Renderer *ren, video *v, char *err, size_t err_len)
{
    SDL_RendererInfo ri;
    gpu_video *g = calloc(1, sizeof *g);
    gl_saved s;
    if (!g) {
        snprintf(err, err_len, "out of memory");
        return NULL;
    }
    g->ren = ren;
    g->win = SDL_RenderGetWindow(ren);
    g->tag = next_tag++;
    if (SDL_GetRendererInfo(ren, &ri) != 0 || strcmp(ri.name, "opengles2") != 0) {
        snprintf(err, err_len, "the SDL renderer is %s, not opengles2", SDL_GetRendererInfo(ren, &ri) == 0 ? ri.name : "unknown");
        goto fail_out;
    }
#if SDL_VERSION_ATLEAST(2, 0, 10)
    SDL_RenderFlush(ren);
#endif
    g->ctx = SDL_GL_GetCurrentContext();
    if (!g->ctx) {
        snprintf(err, err_len, "the renderer has no current GL context");
        goto fail_out;
    }
    if (!load_gl(g, err, err_len))
        goto fail_out;
    const char *ver = (const char *)g->gl.GetString(GL_VERSION);
    const char *rnd = (const char *)g->gl.GetString(GL_RENDERER);
    snprintf(g->info, sizeof g->info, "%s, %s", ver ? ver : "?", rnd ? rnd : "?");
    if (!ver || strncmp(ver, "OpenGL ES", 9) != 0) {
        snprintf(err, err_len, "not an OpenGL ES context (%s)", ver ? ver : "?");
        goto fail_out;
    }
    g->sink.user = g;
    g->sink.blit = sink_blit;
    g->sink.fill = sink_fill;
    g->sink.read_back = sink_read_back;
    g->sink.read_pixel = sink_read_pixel;
    if (!enter(g, &s)) {
        snprintf(err, err_len, "GL setup failed");
        goto fail_out;
    }
    leave(g, &s);
    {
        char msg[300];
        if (!gpu_video_selftest(g, 10, 3, 12345u, msg, sizeof msg) ||
            !gpu_video_selftest(g, 4, 1, 54321u, msg, sizeof msg)) {
            snprintf(err, err_len, "the comparison with the CPU path failed: %s", msg);
            goto fail_out;
        }
        g->selftest_readbacks = g->readbacks;
    }
    if (g->failed) {
        snprintf(err, err_len, "GL error in the comparison");
        goto fail_out;
    }
    video_set_sink(v, &g->sink);
    return g;

fail_out:
    free_gl(g, g->made);
    free(g->ops);
    free(g->owned);
    free(g);
    return NULL;
}

void gpu_video_destroy(gpu_video *g, video *v)
{
    if (!g)
        return;
    if (g->readbacks > g->selftest_readbacks)
        gpu_video_log("GPU path: the picture was read back to the CPU %ld times (and %ld single pixels read)",
                      g->readbacks - g->selftest_readbacks, g->px_reads);
    if (v && v->sink == &g->sink) {
        if (!g->failed && v->fb)
            video_sync_cpu(v);
        video_set_sink(v, NULL);
        v->cpu_auth = 0;
    }
    gl_saved s;
    if (!g->failed && enter(g, &s)) {
        free_gl(g, 1);
        leave(g, &s);
    }
    free(g->ops);
    free(g->owned);
    free(g);
}

/* ---- the comparison against the CPU blitter ----------------------------------------------------------- */

typedef struct {
    img_image im;
    gfx_image gi;
    uint8_t *px;
} test_image;

static unsigned rnd_next(unsigned *s)
{
    *s = *s * 1664525u + 1013904223u;
    return *s >> 8;
}

static int rr(unsigned *s, int lo, int hi)
{
    return lo + (int)(rnd_next(s) % (unsigned)(hi - lo + 1));
}

#define NIMG_BLANK 7

static void make_test_image(test_image *t, unsigned *s, int kind)
{
    int w = rr(s, 1, 60), h = rr(s, 1, 50);
    memset(t, 0, sizeof *t);
    t->px = malloc((size_t)w * h);
    t->im.width = (uint16_t)w;
    t->im.height = (uint16_t)h;
    t->im.stride = (uint16_t)w;
    t->im.palette = IMG_NONE;
    t->im.pixels = t->px;
    for (int i = 0; i < w * h; i++) {
        unsigned r = rnd_next(s);
        t->px[i] = (r & 3) == 0 ? 0 : (uint8_t)(r >> 3);
    }
    gfx_image_from_img(&t->gi, NULL, &t->im);
    t->gi.hi_state = -1;
    t->gi.mask = kind == 0 ? 0xFF : kind == 1 ? 0x3F : 0x0F;
    if (kind == NIMG_BLANK) {
        memset(t->px, 0, (size_t)w * h);
        t->gi.mask = 0xFF;
        t->gi.blank = 1;
    } else if (kind >= 3) {          /* an override at k times the size, with detail words for k >= 4 */
        int k = rr(s, 1, 3);
        size_t n = (size_t)w * k * h * k;
        t->gi.hi = malloc(n);
        t->gi.hi_factor = k;
        t->gi.hi_state = 1;
        for (size_t i = 0; i < n; i++) {
            unsigned r = rnd_next(s);
            t->gi.hi[i] = (r & 3) == 0 ? 0 : (uint8_t)(r >> 3);
        }
        if (kind >= 4) {
            t->gi.hi_detail = malloc(n * sizeof *t->gi.hi_detail);
            for (size_t i = 0; i < n; i++) {
                unsigned r = rnd_next(s);
                t->gi.hi_detail[i] = (r & 7) == 0 ? 0 : GFX_DETAIL_PACK((int)(r & 255) - 128, (int)(r >> 8 & 255) - 128,
                                                                      (int)(r >> 16 & 255) - 128, 1 + (int)(r >> 4 & 127));
            }
        }
    }
    t->gi.gen++;
}

static void free_test_image(test_image *t)
{
    free(t->px);
    free(t->gi.hi);
    free(t->gi.hi_detail);
}

static int compare_video(video *a, video *b, char *msg, size_t n, const char *when)
{
    size_t cnt = (size_t)a->w * a->h;
    for (size_t i = 0; i < cnt; i++) {
        if (a->fb[i] != b->fb[i]) {
            snprintf(msg, n, "%s: index at (%d,%d) is %04x on the CPU, %04x on the GPU", when, (int)(i % (size_t)a->w),
                     (int)(i / (size_t)a->w), a->fb[i], b->fb[i]);
            return 0;
        }
        uint32_t da = a->detail ? a->detail[i] : 0, db = b->detail ? b->detail[i] : 0;
        if (da != db) {
            snprintf(msg, n, "%s: detail at (%d,%d) is %08x on the CPU, %08x on the GPU", when, (int)(i % (size_t)a->w),
                     (int)(i / (size_t)a->w), da, db);
            return 0;
        }
    }
    return 1;
}

int gpu_video_selftest(gpu_video *g, int rounds, int scale, unsigned seed, char *msg, size_t msg_len)
{
    enum { NIMG = 8, BW = 420, BH = 260 };
    static const uint16_t scales[] = {0, 0x100, 0x100, 0x100, 0x80, 0xC0, 0x155, 0x1FF, 0x300, 0x101, 0xFF, 0x7F, 0x2A0, 0x1A};
    unsigned s = seed;
    video a, b;
    test_image imgs[NIMG];
    int ok = 1;
    video_init_bitmap(&a, scale, BW, BH);
    video_init_bitmap(&b, scale, BW, BH);
    for (int i = 0; i < NIMG; i++)
        make_test_image(&imgs[i], &s, i);
    video_set_sink(&b, &g->sink);
    msg[0] = 0;
    {
        SDL_Rect one = {0, 0, 1, 1};
        uint32_t px;
        gpu_video_to_argb(g, &b, &px, one);     /* hands the (empty) picture to the GPU: drawing goes there now */
    }
    uint32_t *oa = NULL, *ob = NULL;
    size_t ocap = 0;
    for (int round = 0; round < rounds && ok; round++) {
        for (int i = 0; i < VIDEO_COLORS; i++)
            a.colram[i] = (uint16_t)(rnd_next(&s) & 0x7FFF);
        memcpy(b.colram, a.colram, sizeof a.colram);
        a.high_pal_argb = b.high_pal_argb = 0xFF000000u | (rnd_next(&s) & 0xFFFFFFu);
        a.view_x = b.view_x = rr(&s, 0, 20);
        a.view_y = b.view_y = rr(&s, 0, 6);
        a.view_pad = b.view_pad = rr(&s, 0, 10);
        a.view_pad_y = b.view_pad_y = rr(&s, 0, 6);
        int nops = rr(&s, 10, 60);
        for (int o = 0; o < nops && ok; o++) {
            int type = rr(&s, 0, 19);
            if (type < 13) {
                test_image *t = &imgs[rr(&s, 0, NIMG - 1)];
                dma_blit d;
                memset(&d, 0, sizeof d);
                d.image = &t->gi;
                d.x = rr(&s, -50, BW + 10);
                d.y = rr(&s, -40, BH + 10);
                d.ctrl = (uint16_t)(rr(&s, 0, 15) | (rr(&s, 0, 2) == 0 ? DMA_FLIPH : 0) | (rr(&s, 0, 2) == 0 ? DMA_FLIPV : 0));
                d.pal = (uint16_t)(rnd_next(&s) & 0xFF00);
                d.color = (uint16_t)rnd_next(&s);
                d.scale_x = scales[rr(&s, 0, (int)(sizeof scales / sizeof *scales) - 1)];
                d.scale_y = scales[rr(&s, 0, (int)(sizeof scales / sizeof *scales) - 1)];
                if (rr(&s, 0, 2) == 0) {
                    d.src_w = rr(&s, 1, t->im.width);
                    d.src_h = rr(&s, 1, t->im.height);
                    d.src_x = rr(&s, 0, t->im.width - d.src_w);
                    d.src_y = rr(&s, 0, t->im.height - d.src_h);
                }
                if (rr(&s, 0, 9) == 0)
                    d.src_w = t->im.width + 1;      /* invalid: both refuse it */
                video_dma(&a, &d);
                video_dma(&b, &d);
            } else if (type == 13) {
                int l = rr(&s, 0, BW - 1), r = rr(&s, l, BW - 1), tp = rr(&s, 0, BH - 1), bt = rr(&s, tp, BH - 1);
                if (rr(&s, 0, 3) == 0) {
                    l = 0; tp = 0; r = BW - 1; bt = BH - 1;
                }
                video_set_window(&a, l, tp, r, bt);
                video_set_window(&b, l, tp, r, bt);
            } else if (type == 14) {
                uint16_t val = (uint16_t)rnd_next(&s);
                int row = rr(&s, -5, BH), cnt = rr(&s, 1, 40);
                video_fill_rows(&a, row, cnt, val);
                video_fill_rows(&b, row, cnt, val);
            } else if (type <= 17) {
                uint16_t val = (uint16_t)rnd_next(&s);
                int x = rr(&s, -2, BW + 2), y = rr(&s, -2, BH + 2);
                for (int k = 0; k < 6; k++) {       /* a short run: the fill merging */
                    video_put_pixel(&a, x + k, y, val);
                    video_put_pixel(&b, x + k, y, val);
                }
            } else if (type == 18) {
                if (rr(&s, 0, 6) == 0) {
                    uint16_t val = (uint16_t)rnd_next(&s);
                    video_clear(&a, val);
                    video_clear(&b, val);
                }
            } else {
                int x = rr(&s, 0, BW - 1), y = rr(&s, 0, BH - 1);
                if (video_get_pixel(&a, x, y) != video_get_pixel(&b, x, y)) {   /* a CPU read: reads back mid-frame */
                    snprintf(msg, msg_len, "round %d: video_get_pixel(%d,%d) differs", round, x, y);
                    ok = 0;
                }
            }
        }
        if (!ok)
            break;
        /* the converted picture: a random part of the view */
        SDL_Rect r;
        int vw = video_view_width(&a), vh = video_view_height(&a);
        r.w = rr(&s, 1, vw);
        r.h = rr(&s, 1, vh);
        r.x = rr(&s, 0, vw - r.w);
        r.y = rr(&s, 0, vh - r.h);
        if (rr(&s, 0, 2) == 0) {
            r.x = r.y = 0;
            r.w = vw;
            r.h = vh;
        }
        size_t need = (size_t)r.w * r.h;
        if (need > ocap) {
            oa = realloc(oa, need * sizeof *oa);
            ob = realloc(ob, need * sizeof *ob);
            ocap = need;
        }
        video_to_argb_area(&a, oa, r.x, r.y, r.w, r.h);
        if (!gpu_video_to_argb(g, &b, ob, r)) {
            snprintf(msg, msg_len, "round %d: the GPU path failed", round);
            ok = 0;
            break;
        }
        for (size_t i = 0; i < need; i++)
            if (oa[i] != ob[i]) {
                snprintf(msg, msg_len, "round %d: converted pixel (%d,%d) of the view is %08x on the CPU, %08x on the GPU",
                         round, r.x + (int)(i % (size_t)r.w), r.y + (int)(i / (size_t)r.w), oa[i], ob[i]);
                ok = 0;
                break;
            }
        if (!ok)
            break;
        char what[40];
        snprintf(what, sizeof what, "round %d", round);
        if (rr(&s, 0, 1) == 0) {
            video_sync_cpu(&b);
            ok = compare_video(&a, &b, msg, msg_len, what);
            SDL_Rect one = {0, 0, 1, 1};
            if (ok && !gpu_video_to_argb(g, &b, ob, one)) {      /* hands the picture back to the GPU */
                snprintf(msg, msg_len, "round %d: the GPU path failed after the read back", round);
                ok = 0;
            }
        }
    }
    if (ok) {
        video_sync_cpu(&b);
        ok = compare_video(&a, &b, msg, msg_len, "end");
    }
    free(oa);
    free(ob);
    video_set_sink(&b, NULL);
    {
        gl_saved sv;
        if (!g->failed && enter(g, &sv)) {
            for (int i = 0; i < NIMG; i++)
                if (imgs[i].gi.sink_owner == g->tag)
                    for (int k = 0; k < 3; k++)
                        del_tex(g, imgs[i].gi.sink_tex[k]);
            leave(g, &sv);
        }
    }
    for (int i = 0; i < NIMG; i++)
        free_test_image(&imgs[i]);
    video_free(&a);
    video_free(&b);
    /* the scratch bitmap's targets and textures must not be taken for the game's */
    {
        gl_saved sv;
        g->nops = 0;
        g->list_detail = 0;
        if (!g->failed && enter(g, &sv)) {
            free_targets(g);
            leave(g, &sv);
        }
    }
    return ok && !g->failed;
}

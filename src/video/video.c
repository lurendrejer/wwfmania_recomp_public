#include "video.h"

#include <stdlib.h>
#include <string.h>

/* Drawing goes to the sink (if any) rather than to fb. */
static int to_sink(const video *v)
{
    return v->sink && !v->cpu_auth;
}

void video_sync_cpu(video *v)
{
    if (to_sink(v)) {
        v->sink->read_back(v->sink->user, v);
        v->cpu_auth = 1;
    }
}

void video_set_sink(video *v, const video_sink *sink)
{
    v->sink = sink;
    v->cpu_auth = sink != NULL;     /* fb is what counts until the sink has taken it over */
}

void video_fb_replaced(video *v)
{
    if (v->sink)
        v->cpu_auth = 1;
}

int video_init_bitmap(video *v, int scale, int bw, int bh)
{
    memset(v, 0, sizeof *v);
    if (scale < 1 || scale > 16 || bw < VIDEO_W || bh < VIDEO_H)
        return 0;
    v->scale = scale;
    v->bw = bw;
    v->bh = bh;
    v->w = bw * scale;
    v->h = bh * scale;
    v->high_pal_argb = 0xFF000000u;
    v->fb = calloc((size_t)v->w * v->h, sizeof *v->fb);
    if (!v->fb)
        return 0;
    video_set_window(v, 0, 0, bw - 1, bh - 1);
    return 1;
}

int video_init(video *v, int scale)
{
    return video_init_bitmap(v, scale, VIDEO_W, VIDEO_H);
}

void video_free(video *v)
{
    free(v->fb);
    free(v->detail);
    v->fb = NULL;
    v->detail = NULL;
}

void video_clear(video *v, uint16_t value)
{
    if (to_sink(v)) {
        v->sink->fill(v->sink->user, v, 0, 0, v->w, v->h, value);
        return;
    }
    size_t n = (size_t)v->w * v->h;
    for (size_t i = 0; i < n; i++)
        v->fb[i] = value;
    if (v->detail)
        memset(v->detail, 0, n * sizeof *v->detail);
}

static int clampi(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }

void video_set_window(video *v, int left, int top, int right, int bottom)
{
    v->win_l = clampi(left, 0, v->bw - 1);
    v->win_t = clampi(top, 0, v->bh - 1);
    v->win_r = clampi(right, 0, v->bw - 1);
    v->win_b = clampi(bottom, 0, v->bh - 1);
}

void video_fill_rows(video *v, int row, int count, uint16_t value)
{
    if (row < 0) {
        count += row;
        row = 0;
    }
    if (row + count > v->bh)
        count = v->bh - row;
    if (count <= 0)
        return;
    if (to_sink(v)) {
        v->sink->fill(v->sink->user, v, 0, row * v->scale, v->w, count * v->scale, value);
        return;
    }
    uint16_t *p = v->fb + (size_t)row * v->scale * v->w;
    size_t n = (size_t)count * v->scale * v->w;
    for (size_t i = 0; i < n; i++)
        p[i] = value;
    if (v->detail)
        memset(v->detail + (size_t)row * v->scale * v->w, 0, n * sizeof *v->detail);
}

uint16_t video_get_pixel(video *v, int x, int y)
{
    if (x < 0 || y < 0 || x >= v->bw || y >= v->bh)
        return 0;
    v->cpu_px_ops++;
    if (v->sink && !v->cpu_auth && v->sink->read_pixel && v->px_reads < 16) {
        int r = v->sink->read_pixel(v->sink->user, v, x * v->scale, y * v->scale);
        if (r >= 0) {
            v->px_reads++;
            return (uint16_t)r;
        }
    }
    video_sync_cpu(v);
    return v->fb[(size_t)y * v->scale * v->w + (size_t)x * v->scale];
}

void video_put_pixel(video *v, int x, int y, uint16_t value)
{
    if (x < 0 || y < 0 || x >= v->bw || y >= v->bh)
        return;
    v->cpu_px_ops++;
    if (to_sink(v)) {
        v->sink->fill(v->sink->user, v, x * v->scale, y * v->scale, v->scale, v->scale, value);
        return;
    }
    for (int j = 0; j < v->scale; j++) {
        uint16_t *p = v->fb + ((size_t)y * v->scale + j) * v->w + (size_t)x * v->scale;
        for (int i = 0; i < v->scale; i++)
            p[i] = value;
        if (v->detail)
            memset(v->detail + ((size_t)y * v->scale + j) * v->w + (size_t)x * v->scale, 0,
                   (size_t)v->scale * sizeof *v->detail);
    }
}

void video_set_palette(video *v, int palnum, const uint16_t *colors, int ncolors)
{
    if (palnum < 0 || palnum >= VIDEO_PALETTES)
        return;
    if (ncolors > 256)
        ncolors = 256;
    memcpy(&v->colram[palnum * 256], colors, (size_t)ncolors * sizeof *colors);
}

int dma_scaled_size(int size, uint16_t scale)
{
    if (scale == 0)
        scale = DMA_SCALE_1X;
    return (int)(((long)size * DMA_SCALE_1X) / scale);
}

/*
 * Source position (in source pixels) for framebuffer offset i, sampling at
 * the pixel center: floor((i + 0.5) * step), step in 16.16.
 */
static int src_pos(int i, uint32_t step, int limit)
{
    int p = (int)(((uint64_t)i * step + step / 2) >> 16);
    return p < limit ? p : limit - 1;
}

void video_dma(video *v, const dma_blit *b)
{
    const gfx_image *gi = b->image;
    const img_image *im = gi->img;
    uint16_t sxs = b->scale_x ? b->scale_x : DMA_SCALE_1X;
    uint16_t sys = b->scale_y ? b->scale_y : DMA_SCALE_1X;

    /* Source rectangle in original pixels. */
    int rx = b->src_x, ry = b->src_y, rw = b->src_w, rh = b->src_h;
    if (rw <= 0 || rh <= 0) {
        rx = ry = 0;
        rw = im->width;
        rh = im->height;
    }
    if (rx < 0 || ry < 0 || rx + rw > im->width || ry + rh > im->height)
        return;
    int dw = dma_scaled_size(rw, sxs);
    int dh = dma_scaled_size(rh, sys);
    if (dw <= 0 || dh <= 0)
        return;

    int k = 1, src_stride = im->stride;
    const uint8_t *src = im->pixels;
    uint8_t mask = gi->mask;
    const uint32_t *det = NULL;
    int use_hi = 0;
    if (gi->hi_state == 1 && gi->hi) {
        use_hi = 1;
        k = gi->hi_factor;
        src = gi->hi;
        det = gi->hi_detail;
        src_stride = im->width * k;
        mask = 0xFF;
    }
    size_t src_off = (size_t)ry * k * src_stride + (size_t)rx * k;
    src += src_off;
    if (det)
        det += src_off;
    int sw = rw * k, sh = rh * k;

    const int s = v->scale;
    int x0 = b->x * s, y0 = b->y * s;
    int xs = x0 > v->win_l * s ? x0 : v->win_l * s;
    int ys = y0 > v->win_t * s ? y0 : v->win_t * s;
    int xe = x0 + dw * s - 1, ye = y0 + dh * s - 1;
    if (xe > (v->win_r + 1) * s - 1)
        xe = (v->win_r + 1) * s - 1;
    if (ye > (v->win_b + 1) * s - 1)
        ye = (v->win_b + 1) * s - 1;
    if (xs > xe || ys > ye)
        return;

    /* Source pixels per framebuffer pixel: scale/256 original pixels per
     * original pixel, times k override pixels, over s framebuffer pixels. */
    uint32_t stepx = (uint32_t)(((uint64_t)sxs * (uint32_t)k << 16) / (256u * (uint32_t)s));
    uint32_t stepy = (uint32_t)(((uint64_t)sys * (uint32_t)k << 16) / (256u * (uint32_t)s));

    const uint16_t pal = b->pal & 0x7F00;
    const uint16_t cval = (uint16_t)(pal | (b->color & 0xFF));
    const uint16_t ctrl = b->ctrl;
    const int fliph = (ctrl & DMA_FLIPH) != 0, flipv = (ctrl & DMA_FLIPV) != 0;

    if (to_sink(v)) {
        if (gi->blank) {        /* only zero pixels: a rectangle of one value, or nothing */
            if (ctrl & (DMA_CZ | DMA_WZ))
                v->sink->fill(v->sink->user, v, xs, ys, xe - xs + 1, ye - ys + 1, ctrl & DMA_CZ ? cval : pal);
            return;
        }
        video_blit_job j;
        j.image = gi;
        j.use_hi = use_hi;
        j.k = k;
        j.src_x = rx * k;
        j.src_y = ry * k;
        j.src_w = sw;
        j.src_h = sh;
        j.x0 = x0;
        j.y0 = y0;
        j.xs = xs;
        j.ys = ys;
        j.xe = xe;
        j.ye = ye;
        j.step_x = stepx;
        j.step_y = stepy;
        j.flip_h = fliph;
        j.flip_v = flipv;
        j.ctrl = ctrl;
        j.pal = pal;
        j.cval = cval;
        j.detail = det != NULL;
        if (v->sink->blit(v->sink->user, v, &j))
            return;
        v->sync_tag = 1;
        video_sync_cpu(v);      /* the sink cannot draw it: do it here */
    }
    if (det && !v->detail) {
        v->detail = calloc((size_t)v->w * v->h, sizeof *v->detail);
        if (!v->detail)
            det = NULL;
    }

    for (int y = ys; y <= ye; y++) {
        int sy = src_pos(y - y0, stepy, sh);
        if (flipv)
            sy = sh - 1 - sy;
        const uint8_t *row = src + (size_t)sy * src_stride;
        const uint32_t *drow = det ? det + (size_t)sy * src_stride : NULL;
        uint16_t *dst = v->fb + (size_t)y * v->w;
        uint32_t *dd = v->detail ? v->detail + (size_t)y * v->w : NULL;
        for (int x = xs; x <= xe; x++) {
            int sx = src_pos(x - x0, stepx, sw);
            if (fliph)
                sx = sw - 1 - sx;
            uint8_t p = row[sx] & mask;
            if (p) {
                if (ctrl & DMA_CNZ) {
                    dst[x] = cval;
                    if (dd)
                        dd[x] = 0;
                } else if (ctrl & DMA_WNZ) {
                    dst[x] = (uint16_t)(pal | p);
                    if (dd)
                        dd[x] = drow ? drow[sx] : 0;
                }
            } else {
                if (ctrl & DMA_CZ) {
                    dst[x] = cval;
                    if (dd)
                        dd[x] = 0;
                } else if (ctrl & DMA_WZ) {
                    dst[x] = pal;
                    if (dd)
                        dd[x] = 0;
                }
            }
        }
    }
}

int video_view_width(const video *v)
{
    return (VIDEO_W + 2 * v->view_pad) * v->scale;
}

int video_view_height(const video *v)
{
    return (VIDEO_H + 2 * v->view_pad_y) * v->scale;
}

void video_to_argb(video *v, uint32_t *out)
{
    video_to_argb_area(v, out, 0, 0, video_view_width(v), video_view_height(v));
}

void video_to_argb_area(video *v, uint32_t *out, int ax, int ay, int aw, int ah)
{
    v->sync_tag = 2;
    video_sync_cpu(v);
    for (int i = 0; i < VIDEO_COLORS; i++)
        v->argb[i] = img_color_argb(v->colram[i]);
    int ow = aw, oh = ah;
    int x0 = (v->view_x - v->view_pad) * v->scale + ax, y0 = (v->view_y - v->view_pad_y) * v->scale + ay;
    for (int y = 0; y < oh; y++) {
        int sy = y0 + y;
        uint32_t *dst = out + (size_t)y * ow;
        if (sy < 0 || sy >= v->h) {
            for (int x = 0; x < ow; x++)
                dst[x] = v->argb[0];
            continue;
        }
        const uint16_t *src = v->fb + (size_t)sy * v->w;
        const uint32_t *dt = v->detail ? v->detail + (size_t)sy * v->w : NULL;
        for (int x = 0; x < ow; x++) {
            int sx = x0 + x;
            if (sx < 0 || sx >= v->w) {
                dst[x] = v->argb[0];
            } else if (src[sx] & 0x8000) {
                dst[x] = v->high_pal_argb;
            } else {
                dst[x] = v->argb[src[sx] & (VIDEO_COLORS - 1)];
                if (dt && dt[sx])
                    dst[x] = gfx_apply_detail(dst[x], dt[sx]);
            }
        }
    }
}

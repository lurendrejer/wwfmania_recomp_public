/*
 * Save states: the machine's state between two video frames, in one file. The file is only good for
 * the same build and view settings (framebuffer size is checked); it is not a format to keep.
 * Not saved: the CMOS (settings and audits stay), the sound player (what plays keeps playing),
 * and the true-color detail words of the framebuffer (a frame later the game has redrawn them).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wolf.h"

#define STATE_MAGIC 0x31535746u /* "FWS1" */
#define RAM_BYTES 0x80000u      /* 0x01000000..0x013FFFFF is 4 Mbit of bit addresses = 512 KiB */

static int put(FILE *f, const void *p, size_t n) { return fwrite(p, 1, n, f) == n; }
static int get(FILE *f, void *p, size_t n) { return fread(p, 1, n, f) == n; }

typedef struct {
    uint32_t r[32], pc, st_rest;
    uint8_t n, c, z, v, fs[2], fe[2];
    int32_t budget;
    uint16_t io[32];
} cpu_img;

typedef struct {
    uint16_t dma[16], win_l, win_r, win_t, win_b, cmapsel, sysctrl, vmux;
    int display_row, view_x, view_y, win_l2, win_t2, win_r2, win_b2;
    int pic_running, pic_bit12, pic_last_cmd, pic_index;
    uint8_t pic_serial[16], pic_out;
    uint64_t frames;
} hw_img;

static size_t ram_bytes(const wolf *w)
{
    (void)w;
    return RAM_BYTES;
}

int wolf_state_save(wolf *w, const char *path)
{
    w->v.sync_tag = 3;
    video_sync_cpu(&w->v);   /* with the GPU path the picture is read back first */
    FILE *f = fopen(path, "wb");
    uint32_t magic = STATE_MAGIC, fbn = (uint32_t)((size_t)w->v.w * (size_t)w->v.h);
    cpu_img c;
    hw_img h;
    int ok;

    if (!f)
        return 0;
    memcpy(c.r, w->cpu.r, sizeof c.r);
    c.pc = w->cpu.pc;
    c.st_rest = w->cpu.st_rest;
    c.n = w->cpu.n; c.c = w->cpu.c; c.z = w->cpu.z; c.v = w->cpu.v;
    memcpy(c.fs, w->cpu.fs, 2);
    memcpy(c.fe, w->cpu.fe, 2);
    c.budget = w->cpu.budget;
    memcpy(c.io, w->cpu.io, sizeof c.io);
    memset(&h, 0, sizeof h);
    memcpy(h.dma, w->dma, sizeof h.dma);
    h.win_l = w->win_l; h.win_r = w->win_r; h.win_t = w->win_t; h.win_b = w->win_b;
    h.cmapsel = w->cmapsel; h.sysctrl = w->sysctrl; h.vmux = w->vmux;
    h.display_row = w->display_row;
    h.view_x = w->v.view_x; h.view_y = w->v.view_y;
    h.win_l2 = w->v.win_l; h.win_t2 = w->v.win_t; h.win_r2 = w->v.win_r; h.win_b2 = w->v.win_b;
    h.pic_running = w->pic_running; h.pic_bit12 = w->pic_bit12;
    h.pic_last_cmd = w->pic_last_cmd; h.pic_index = w->pic_index;
    memcpy(h.pic_serial, w->pic_serial, sizeof h.pic_serial);
    h.pic_out = w->pic_out;
    h.frames = w->frames;
    ok = put(f, &magic, 4) && put(f, &fbn, 4) && put(f, &c, sizeof c) && put(f, &h, sizeof h) &&
         put(f, w->ram, ram_bytes(w)) && put(f, w->v.fb, (size_t)fbn * 2) &&
         put(f, w->v.colram, sizeof w->v.colram);
    return fclose(f) == 0 && ok;
}

int wolf_state_load(wolf *w, const char *path)
{
    FILE *f = fopen(path, "rb");
    uint32_t magic = 0, fbn = 0;
    cpu_img c;
    hw_img h;
    uint8_t *ram = NULL;
    uint16_t *fb = NULL;
    int ok = 0;

    if (!f)
        return 0;
    if (!get(f, &magic, 4) || magic != STATE_MAGIC || !get(f, &fbn, 4) ||
        fbn != (uint32_t)((size_t)w->v.w * (size_t)w->v.h) || !get(f, &c, sizeof c) || !get(f, &h, sizeof h))
        goto out;
    ram = malloc(ram_bytes(w));
    fb = malloc((size_t)fbn * 2);
    if (!ram || !fb || !get(f, ram, ram_bytes(w)) || !get(f, fb, (size_t)fbn * 2) ||
        !get(f, w->v.colram, sizeof w->v.colram))
        goto out; /* colram may be half written; the next palette write of the game fixes it */
    memcpy(w->ram, ram, ram_bytes(w));
    memcpy(w->v.fb, fb, (size_t)fbn * 2);
    video_fb_replaced(&w->v);
    memcpy(w->cpu.r, c.r, sizeof c.r);
    w->cpu.pc = c.pc;
    w->cpu.st_rest = c.st_rest;
    w->cpu.n = c.n; w->cpu.c = c.c; w->cpu.z = c.z; w->cpu.v = c.v;
    memcpy(w->cpu.fs, c.fs, 2);
    memcpy(w->cpu.fe, c.fe, 2);
    w->cpu.budget = c.budget;
    memcpy(w->cpu.io, c.io, sizeof c.io);
    memcpy(w->dma, h.dma, sizeof h.dma);
    w->win_l = h.win_l; w->win_r = h.win_r; w->win_t = h.win_t; w->win_b = h.win_b;
    w->cmapsel = h.cmapsel; w->sysctrl = h.sysctrl; w->vmux = h.vmux;
    w->display_row = h.display_row;
    w->v.view_x = h.view_x; w->v.view_y = h.view_y;
    w->v.win_l = h.win_l2; w->v.win_t = h.win_t2; w->v.win_r = h.win_r2; w->v.win_b = h.win_b2;
    w->pic_running = h.pic_running; w->pic_bit12 = h.pic_bit12;
    w->pic_last_cmd = h.pic_last_cmd; w->pic_index = h.pic_index;
    memcpy(w->pic_serial, h.pic_serial, sizeof w->pic_serial);
    w->pic_out = h.pic_out;
    w->frames = h.frames;
    ok = 1;
out:
    free(ram);
    free(fb);
    fclose(f);
    return ok;
}

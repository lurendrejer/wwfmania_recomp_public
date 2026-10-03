#include "wolf.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/artsrc.h"
#include "assets/png_read.h"
#include "util/fsutil.h"

uint8_t *gsp_ram;

/* ---- address map (bit addresses) -------------------------------------------- */

#define VRAM_END 0x00400000u     /* 512 x 1024 pixels, 8 bits each in the CPU view */
#define PALENB 0x0800u           /* SYSCTRL: 1 = pixel plane, 0 = palette plane */
#define RAM_BASE 0x01000000u
#define RAM_END 0x01400000u
#define CMOS_BASE 0x01400000u
#define CMOS_END 0x01500000u
#define PIC_ADDR 0x01600000u
#define SOUND_ADDR 0x01680000u
#define VMUX_ADDR 0x01800000u
#define IO_BASE 0x01860000u
#define IO_END 0x01860100u
#define COLRAM_BASE 0x01880000u
#define COLRAM_END 0x01900000u
#define DMA_BASE 0x01A00000u
#define DMA_END 0x01A00100u
#define CMAPSEL_ADDR 0x01A80080u
#define SYSCTRL_ADDR 0x01B00000u
#define GSPIO_BASE 0xC0000000u
#define GSPIO_END 0xC0000200u
/* The program ROM is 0xFF800000..0xFFFFFFFF; the 1 MB below it, which the board does not decode,
 * holds the code and data the mods add (tools/gsp/gspasm.py EXT_ROM_BASE). */
#define ROM_BASE 0xFF000000u
#define ROM_BYTES 0x200000u

#define XPAD 56            /* SCRNXP: left padding of the screen in the bitmap */

/* The game's bitmap is 512 pixels wide and it addresses x in 0..511. With
 * wolf_set_extra_width the port's bitmap is 512 + 2 * extra wide and every x
 * the game uses is shifted right by `extra`, so the game code never knows. */
/* The two display pages (rows 0..255 and 256..511 in the game) are 256 + 2 *
 * extra_y rows apart in the bitmap, the game's row 0 of each at extra_y. */
static int ymap_page(const wolf *w, int page, int y)
{
    return page * (256 + 2 * w->extra_y) + w->extra_y + (y - 256 * page);
}

/* Rows 512 and up are not display pages, but the power-up test writes and
 * reads all 1024 rows, so they keep room of their own after the two pages. */
static int ymap(const wolf *w, int y)
{
    if (y >= 512)
        return 2 * (256 + 2 * w->extra_y) + (y - 512);
    return ymap_page(w, y >= 256 ? 1 : 0, y);
}

static void wput(wolf *w, int x, int y, uint16_t value)
{
    video_put_pixel(&w->v, x + w->extra, ymap(w, y), value);
}

static uint16_t wget(wolf *w, int x, int y)
{
    return video_get_pixel(&w->v, x + w->extra, ymap(w, y));
}

/* The DMA clip window: the game's full width (0..511) means the whole bitmap. */
static void set_dma_window(wolf *w)
{
    int l = w->win_l <= 0 ? 0 : w->win_l + w->extra;
    int r = w->win_r >= 511 ? w->v.bw - 1 : w->win_r + w->extra;
    if (w->extra_y > 0) {       /* the whole page the window is on */
        int page = w->win_t >= 256 ? 1 : 0;
        video_set_window(&w->v, l, page * (256 + 2 * w->extra_y), r,
                         page * (256 + 2 * w->extra_y) + 255 + 2 * w->extra_y);
        return;
    }
    video_set_window(&w->v, l, w->win_t, r, w->win_b);
}
#define LINES_PER_FRAME 289

enum { IO_NONE, IO_COINS, IO_SWITCH, IO_SWITCH2, IO_DIP, IO_SOUNDIRQ, IO_WATCHDOG, IO_COINCTR };

/* I/O addresses per VMUX mode (orig/SYS.EQU and orig/WWFSEC.EQU). */
static const struct {
    uint32_t coins, sw, sw2, watchdog, dip, soundirq, coinctr;
} io_maps[5] = {
    {0x01860030, 0x01860000, 0x01860010, 0x01860030, 0x01860020, 0x01860040, 0x01860010},
    {0x01860090, 0x01860040, 0x01860080, 0x01860090, 0x01860010, 0x01860020, 0x01860080},
    {0x01860060, 0x01860080, 0x01860020, 0x01860060, 0x01860040, 0x01860010, 0x01860020},
    {0x018600a0, 0x01860010, 0x01860080, 0x018600a0, 0x01860020, 0x01860050, 0x01860080},
    {0x01860070, 0x01860020, 0x01860040, 0x01860070, 0x01860010, 0x01860080, 0x01860040},
};

static int io_read_fn(const wolf *w, uint32_t a)
{
    int m = w->vmux < 5 ? w->vmux : 0;
    if (a == io_maps[m].coins) return IO_COINS;
    if (a == io_maps[m].sw) return IO_SWITCH;
    if (a == io_maps[m].sw2) return IO_SWITCH2;
    if (a == io_maps[m].dip) return IO_DIP;
    if (a == io_maps[m].soundirq) return IO_SOUNDIRQ;
    return IO_NONE;
}

static int io_write_fn(const wolf *w, uint32_t a)
{
    int m = w->vmux < 5 ? w->vmux : 0;
    if (a == io_maps[m].watchdog) return IO_WATCHDOG;
    if (a == io_maps[m].coinctr) return IO_COINCTR;
    return IO_NONE;
}

/* ---- bit-field access to byte arrays ------------------------------------------ */

static uint32_t get_bits(const uint8_t *mem, uint32_t bitoff, int bits)
{
    const uint8_t *p = mem + (bitoff >> 3);
    uint64_t v = 0;
    int shift = (int)(bitoff & 7);
    int nbytes = (shift + bits + 7) >> 3;
    for (int i = 0; i < nbytes; i++)
        v |= (uint64_t)p[i] << (8 * i);
    v >>= shift;
    return bits >= 32 ? (uint32_t)v : (uint32_t)v & ((1u << bits) - 1);
}

static void put_bits(uint8_t *mem, uint32_t bitoff, int bits, uint32_t value)
{
    uint8_t *p = mem + (bitoff >> 3);
    int shift = (int)(bitoff & 7);
    int nbytes = (shift + bits + 7) >> 3;
    uint64_t v = 0;
    for (int i = 0; i < nbytes; i++)
        v |= (uint64_t)p[i] << (8 * i);
    uint64_t mask = (bits >= 32 ? 0xFFFFFFFFull : ((1ull << bits) - 1)) << shift;
    v = (v & ~mask) | (((uint64_t)value << shift) & mask);
    for (int i = 0; i < nbytes; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

static wolf *hw_of(gsp_t *c) { return (wolf *)c->hw; }

/* ---- DMA ---------------------------------------------------------------------- */

static const wolf_romimg *find_img(const wolf *w, uint32_t sag)
{
    int lo = 0, hi = w->nimgs - 1, best = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (w->imgs[mid].sag <= sag) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    if (best < 0)
        return NULL;
    const wolf_romimg *r = &w->imgs[best];
    const img_image *im = r->img->img;
    if (sag - r->sag >= (uint32_t)im->width * im->height * 8u)
        return NULL;
    return r;
}

/* Image ROM addresses that hold no IMG image read as zero, both for the
 * DMA and for the CPU's IROM window (the diagnostics compare the two). */
static uint8_t zero_pixels[512 * 256];

static void record_draw(wolf *w, const char *name, int x, int y, int ww, int hh, uint8_t hd, uint8_t bg)
{
    int page = w->win_t >= 256 ? 1 : 0;
    if (!w->draws[page])
        return;
    if (w->draws_frame[page] != w->frames) {      /* the first draw on this page this frame: a new picture */
        w->draws_frame[page] = w->frames;
        w->ndraws[page] = 0;
    }
    if (w->ndraws[page] >= WOLF_MAX_DRAWS)
        return;
    wolf_draw *d = &w->draws[page][w->ndraws[page]++];
    snprintf(d->name, sizeof d->name, "%s", name);
    d->x = x;
    d->y = y;
    d->w = ww;
    d->h = hh;
    d->ctrl = w->dma[1];
    d->pal = w->dma[8];
    d->color = w->dma[9];
    d->hd = hd;
    d->background = bg;
}

static void dma_blank(wolf *w)
{
    uint16_t *d = w->dma;
    int hs = d[6], vs = d[7];
    if (hs <= 0 || vs <= 0 || hs > 512 || (long)hs * vs > (long)sizeof zero_pixels)
        return;
    img_image im;
    memset(&im, 0, sizeof im);
    im.width = (uint16_t)hs;
    im.height = (uint16_t)vs;
    im.stride = (uint16_t)hs;
    im.palette = IMG_NONE;
    im.pixels = zero_pixels;
    gfx_image gi;
    memset(&gi, 0, sizeof gi);
    gi.img = &im;
    gi.mask = 0xFF;
    gi.blank = 1;
    gi.hi_state = -1;
    dma_blit b;
    memset(&b, 0, sizeof b);
    b.image = &gi;
    b.ctrl = d[1];
    b.pal = d[8];
    b.color = d[9];
    b.scale_x = d[10] ? d[10] : DMA_SCALE_1X;
    b.scale_y = d[11] ? d[11] : DMA_SCALE_1X;
    b.x = (int16_t)d[4] + w->extra;
    b.y = ymap_page(w, w->win_t >= 256 ? 1 : 0, (int16_t)d[5]);
    set_dma_window(w);
    video_dma(&w->v, &b);
    if (w->draws[0])
        record_draw(w, "", b.x, b.y, dma_scaled_size(hs, b.scale_x), dma_scaled_size(vs, b.scale_y), 0, 0);
}

static void dma_run(wolf *w)
{
    uint16_t *d = w->dma;
    uint32_t sag = (uint32_t)d[3] << 16 | d[2];
    const wolf_romimg *r = find_img(w, sag);
    w->cpu.io[0x12] |= GSP_INT_X1;     /* the blit completes at once */
    if (!r) {
        w->unmapped_count++;
        if (w->log_unmapped)
            fprintf(stderr, "DMA from unmapped image address %08X (pc %08X)\n", sag, w->cpu.pc);
        if (d[1] & (DMA_WZ | DMA_CZ))
            dma_blank(w);
        return;
    }
    const img_image *im = r->img->img;
    uint32_t pix = (sag - r->sag) / 8;
    int sx = (int)(pix % im->width), sy = (int)(pix / im->width);
    int hs = d[6], vs = d[7];
    if (hs <= 0 || vs <= 0)
        return;
    /* Register 0 drops pixels from each row (MAME midtunit_v.cpp): with
     * control bit 6 its low byte is a start skip and its high byte an end
     * skip; without it the whole word is an end skip. The life bars hide
     * the lost health this way. As in MAME, a start skip moves the source
     * but not the destination. */
    int start_skip = 0, end_skip = d[0];
    if (d[1] & 0x40) {
        start_skip = d[0] & 0xFF;
        end_skip = d[0] >> 8;
    }
    hs -= end_skip;
    sx += start_skip;
    hs -= start_skip;
    if (sx + hs > im->width)
        hs = im->width - sx;
    if (sy + vs > im->height)
        vs = im->height - sy;
    if (hs <= 0 || vs <= 0)
        return;

    dma_blit b;
    memset(&b, 0, sizeof b);
    if (r->ci)
        gfx_get(&w->gc, r->ci);   /* loads the override on first use */
    b.image = r->img;
    b.ctrl = d[1];
    b.pal = d[8];
    b.color = d[9];
    b.scale_x = d[10] ? d[10] : DMA_SCALE_1X;
    b.scale_y = d[11] ? d[11] : DMA_SCALE_1X;
    b.src_x = sx;
    b.src_y = sy;
    b.src_w = hs;
    b.src_h = vs;
    b.x = (int16_t)d[4] + w->extra;
    b.y = ymap_page(w, w->win_t >= 256 ? 1 : 0, (int16_t)d[5]);
    /* Flipped blits start at the far edge and draw backwards. */
    if (b.ctrl & DMA_FLIPH)
        b.x -= dma_scaled_size(hs, b.scale_x) - 1;
    if (b.ctrl & DMA_FLIPV)
        b.y -= dma_scaled_size(vs, b.scale_y) - 1;
    /* The recover meter's side bars wait outside the 400 pixel screen and slide
     * in when needed. They were always culled; in a wide view they would sit
     * there as loose banners, so leave them out while they are fully outside. */
    if (w->draw_margin > 32 && w->hud_dx <= 0 && r->img->entry && strncmp(r->img->entry->name, "RECVR", 5) == 0) {
        int bw = dma_scaled_size(hs, b.scale_x);
        if (b.x + bw <= XPAD + w->extra || b.x >= XPAD + VIDEO_W + w->extra)
            return;
    }
    set_dma_window(w);
    video_dma(&w->v, &b);
    if (w->draws[0])
        record_draw(w, r->name, b.x, b.y, dma_scaled_size(hs, b.scale_x), dma_scaled_size(vs, b.scale_y),
                    r->img->hi_state == 1 && r->img->hi, r->background);
}

/* ---- I/O registers ------------------------------------------------------------ */

static uint16_t read16(wolf *w, uint32_t a)
{
    gsp_t *c = &w->cpu;
    if (a >= GSPIO_BASE && a < GSPIO_END) {
        int i = (int)((a - GSPIO_BASE) >> 4);
        if (i == 0x1D) {     /* VCOUNT */
            int32_t used = w->frame_insns - c->budget;
            return (uint16_t)(used < 0 ? 0 : (int64_t)used * LINES_PER_FRAME / w->frame_insns);
        }
        if (i == 0x1C) {     /* HCOUNT */
            /* The pixel clock within the line. The game mixes it into its random numbers (RNDRNG0,
             * RNDPER, UTIL.ASM), so on the board it changes with the exact moment of the read. With a
             * constant register the numbers depended only on how many were drawn before, and a
             * game started at the same menu step always got the same ladder of opponents. Here it
             * follows the instructions used in the frame, which is as deterministic as the machine
             * (recordings and save states still reproduce) but differs with the timing of the player. */
            int32_t used = w->frame_insns - c->budget;
            return (uint16_t)(used < 0 ? 0 : (((int64_t)used * LINES_PER_FRAME * 512 / w->frame_insns) & 511));
        }
        return c->io[i];
    }
    if (a >= DMA_BASE && a < DMA_END) {
        int i = (int)((a - DMA_BASE) >> 4);
        return i == 1 ? (uint16_t)(w->dma[1] & 0x7FFF) : w->dma[i];   /* never busy */
    }
    if (a >= COLRAM_BASE && a < COLRAM_END)
        return w->v.colram[((a - COLRAM_BASE) >> 4) & (VIDEO_COLORS - 1)];
    if (a >= IO_BASE && a < IO_END) {
        switch (io_read_fn(w, a)) {
        case IO_SWITCH: return (uint16_t)~(w->player[0] | w->player[1] << 8);
        case IO_SWITCH2: return (uint16_t)~(w->player[2] | w->player[3] << 8);
        case IO_COINS: return (uint16_t)~w->coin_bits;
        case IO_DIP: {
            uint16_t d = w->skip_selftest ? (uint16_t)(w->dip & ~0x0002u) : w->dip;
            /* Active low, and READ_DIP (DIAG.ASM) reverses the bits within each byte: the coinage
             * field DPCOINAGE (0E00h) comes from raw bits 14 to 12, all on for free play. */
            return w->free_play ? (uint16_t)(d & ~0x7000u) : d;
        }
        case IO_SOUNDIRQ: {
            /* bit 12: PIC (0 = data available); bit 11: sound board ready
             * for the next byte; bit 10: 0 = sound data available */
            int reply = w->snd ? snd_reply_pending(w->snd) : w->snd_count > 0;
            return (uint16_t)((w->pic_bit12 ? 1u << 12 : 0) | (1u << 11) | (reply ? 0 : 1u << 10));
        }
        default: return 0xFFFF;
        }
    }
    if (a == SOUND_ADDR) {
        if (w->snd)
            return snd_read(w->snd);
        w->snd_just_reset = 0;
        if (!w->snd_count)
            return 0xFFFF;
        uint16_t r = w->snd_queue[0];
        memmove(w->snd_queue, w->snd_queue + 1, (size_t)--w->snd_count * sizeof *w->snd_queue);
        return r;
    }
    if (a == PIC_ADDR)
        return w->pic_out;
    if (a == VMUX_ADDR)
        return w->vmux;
    if (a == SYSCTRL_ADDR)
        return w->sysctrl;
    if (a == CMAPSEL_ADDR)
        return w->cmapsel;
    if (w->log_unmapped)
        fprintf(stderr, "read16 unmapped %08X (pc %08X)\n", a, c->pc);
    return 0xFFFF;
}

static void write16(wolf *w, uint32_t a, uint16_t v)
{
    gsp_t *c = &w->cpu;
    if (a >= GSPIO_BASE && a < GSPIO_END) {
        int i = (int)((a - GSPIO_BASE) >> 4);
        if (i == 0x12) {     /* INTPEND: writing 0 clears DI/WV */
            uint16_t clr = (uint16_t)(~v & (GSP_INT_DI | GSP_INT_WV));
            c->io[i] &= (uint16_t)~clr;
            return;
        }
        c->io[i] = v;
        return;
    }
    if (a >= DMA_BASE && a < DMA_END) {
        int i = (int)((a - DMA_BASE) >> 4);
        w->dma[i] = v;
        if (i == 12 || i == 13) {
            if (w->dma[15] & 0x20) {
                if (i == 12) w->win_t = v; else w->win_b = v;
            } else {
                if (i == 12) w->win_l = v; else w->win_r = v;
            }
        }
        if (i == 1) {
            c->io[0x12] &= (uint16_t)~GSP_INT_X1;
            if (v & 0x8000)
                dma_run(w);
        }
        return;
    }
    if (a >= COLRAM_BASE && a < COLRAM_END) {
        w->v.colram[((a - COLRAM_BASE) >> 4) & (VIDEO_COLORS - 1)] = v & 0x7FFF;
        return;
    }
    if (a >= IO_BASE && a < IO_END) {
        if (io_write_fn(w, a) == IO_COINCTR) {
            /* bit 4: sound board reset. On release the DCS boots and reports
             * 0x79 plus a ROM diagnostic code (1 = no bad ROM), unless the
             * game writes a "bypass" byte first. */
            int reset = (v & 0x10) != 0;
            if (w->snd)
                snd_reset_line(w->snd, reset);
            if (w->snd_reset_line && !reset) {
                w->snd_count = 0;
                w->snd_queue[w->snd_count++] = 0x79;
                w->snd_queue[w->snd_count++] = 0x01;
                w->snd_have_high = 0;
                w->snd_just_reset = 1;
            }
            w->snd_reset_line = reset;
            int run = (v & 0x20) != 0;
            if (run && !w->pic_running) {
                w->pic_bit12 = 0;
                w->pic_last_cmd = -1;
                w->pic_index = 0;
            }
            w->pic_running = run;
        }
        return;
    }
    if (a == PIC_ADDR) {
        /* Bit 4 set: idle/acknowledge (bit 12 of SOUNDIRQ goes high).
         * Bit 4 clear: run command v & 15 and present its data byte (bit 12
         * goes low): 0 = next serial number byte, 15 = echo test. */
        if (!w->pic_running)
            return;
        if (v & 0x10) {
            if (w->pic_last_cmd == 0)
                w->pic_index++;
            else if (v == 0x10)
                w->pic_index = 0;
            w->pic_bit12 = 1;
            w->pic_last_cmd = -1;
        } else {
            int cmd = v & 15;
            w->pic_out = cmd == 0 ? w->pic_serial[w->pic_index & 15] : cmd == 15 ? 0x0F : 0;
            w->pic_bit12 = 0;
            w->pic_last_cmd = cmd;
        }
        return;
    }
    if (a == SOUND_ADDR) {
        uint16_t b = v & 0xFF;
        if (w->snd) {
            snd_write(w->snd, (uint8_t)b);
            return;
        }
        if (w->snd_just_reset) {     /* bypass the DCS self test */
            w->snd_just_reset = 0;
            w->snd_count = 0;
            return;
        }
        if (!w->snd_have_high) {
            w->snd_high = b;
            w->snd_have_high = 1;
            return;
        }
        w->snd_have_high = 0;
        uint16_t code = (uint16_t)(w->snd_high << 8 | b);
        if (code == 999 && w->snd_count < 8)   /* revision request */
            w->snd_queue[w->snd_count++] = 0x0013;
        return;
    }
    if (a == VMUX_ADDR) {
        w->vmux = v & 7;
        return;
    }
    if (a == SYSCTRL_ADDR) {
        w->sysctrl = v;
        return;
    }
    if (a == CMAPSEL_ADDR) {
        w->cmapsel = v;
        w->dma[8] = v;
        return;
    }
    if (w->log_unmapped)
        fprintf(stderr, "write16 unmapped %08X = %04X (pc %08X)\n", a, v, c->pc);
}

/* ---- the CPU's memory interface ------------------------------------------------ */

static int is_mmio(uint32_t a)
{
    return (a >= PIC_ADDR && a < 0x02000000u) || (a >= GSPIO_BASE && a < GSPIO_END);
}

uint32_t gsp_read(gsp_t *c, uint32_t addr, int bits)
{
    wolf *w = hw_of(c);
    if (addr >= RAM_BASE && addr < RAM_END)
        return get_bits(w->ram, addr - RAM_BASE, bits);
    if (addr >= ROM_BASE)
        return get_bits(w->rom, addr - ROM_BASE, bits);
    if (addr >= CMOS_BASE && addr < CMOS_END)
        return get_bits(w->cmos, addr - CMOS_BASE, bits);
    if (addr < VRAM_END) {
        int pal_plane = !(w->sysctrl & PALENB);
        uint32_t v = 0;
        w->v.sync_tag = c->pc;
        for (int i = 0; i < bits; i += 8) {
            uint32_t pa = addr + (uint32_t)i;
            uint16_t px = wget(w, (int)((pa >> 3) & 511), (int)(pa >> 12));
            v |= (uint32_t)(pal_plane ? px >> 8 : px & 0xFF) << i;
        }
        return bits >= 32 ? v : v & ((1u << bits) - 1);
    }
    if (addr >= 0x02000000u && addr < 0x02800000u)
        return 0;   /* image ROM window: see zero_pixels */
    if (is_mmio(addr)) {
        uint32_t base = addr & ~15u;
        int shift = (int)(addr & 15);
        uint32_t v = read16(w, base);
        if (shift + bits > 16)
            v |= (uint32_t)read16(w, base + 16) << 16;
        if (shift + bits > 32)
            return (v >> shift) | ((uint32_t)read16(w, base + 32) << (32 - shift));
        v >>= shift;
        return bits >= 32 ? v : v & ((1u << bits) - 1);
    }
    if (w->log_unmapped)
        fprintf(stderr, "read unmapped %08X (pc %08X)\n", addr, c->pc);
    return 0;
}

void gsp_write(gsp_t *c, uint32_t addr, int bits, uint32_t value)
{
    wolf *w = hw_of(c);
    if (addr >= RAM_BASE && addr < RAM_END) {
        put_bits(w->ram, addr - RAM_BASE, bits, value);
        return;
    }
    if (addr >= CMOS_BASE && addr < CMOS_END) {
        put_bits(w->cmos, addr - CMOS_BASE, bits, value);
        return;
    }
    if (addr < VRAM_END) {
        /* Pixel plane: the palette byte comes from the DMA palette register
         * (as in MAME's midtunit_vram_w). Palette plane: only the high byte. */
        int pal_plane = !(w->sysctrl & PALENB);
        w->v.sync_tag = c->pc;
        for (int i = 0; i < bits; i += 8) {
            uint32_t pa = addr + (uint32_t)i;
            int x = (int)((pa >> 3) & 511), y = (int)(pa >> 12);
            uint16_t b = (uint16_t)((value >> i) & 0xFF);
            if (pal_plane)
                wput(w, x, y, (uint16_t)(b << 8 | (wget(w, x, y) & 0xFF)));
            else
                wput(w, x, y, (uint16_t)((w->dma[8] & 0xFF) << 8 | b));
        }
        return;
    }
    if (is_mmio(addr)) {
        uint32_t base = addr & ~15u;
        int shift = (int)(addr & 15);
        if (shift == 0 && bits == 16) {
            write16(w, base, (uint16_t)value);
        } else if (shift == 0 && bits == 32) {
            write16(w, base, (uint16_t)value);
            write16(w, base + 16, (uint16_t)(value >> 16));
        } else {
            /* partial register write (e.g. 1-bit INTENB/INTPEND updates) */
            uint32_t mask = (bits >= 32 ? 0xFFFFFFFFu : (1u << bits) - 1) << shift;
            uint32_t old = read16(w, base);
            if (shift + bits > 16)
                old |= (uint32_t)read16(w, base + 16) << 16;
            uint32_t nv = (old & ~mask) | ((value << shift) & mask);
            if (base >= GSPIO_BASE && (base - GSPIO_BASE) >> 4 == 0x12) {
                /* INTPEND: only the written bits may be cleared */
                uint16_t clr = (uint16_t)(mask & ~((uint32_t)value << shift));
                c->io[0x12] &= (uint16_t)~(clr & (GSP_INT_DI | GSP_INT_WV));
                return;
            }
            write16(w, base, (uint16_t)nv);
            if (shift + bits > 16)
                write16(w, base + 16, (uint16_t)(nv >> 16));
        }
        return;
    }
    if (addr >= ROM_BASE)
        return;   /* writes to ROM are ignored */
    if (w->log_unmapped)
        fprintf(stderr, "write unmapped %08X = %08X (pc %08X)\n", addr, value, c->pc);
}

/* ---- graphics instructions --------------------------------------------------------- */
/* B-file graphics registers: SADDR b0, SPTCH b1, DADDR b2, DPTCH b3, OFFSET b4,
 * DYDX b7, COLOR0 b8, COLOR1 b9. XY coordinates map 1:1 to bitmap pixels
 * (the game and the diagnostics use OFFSET 0 and a 4096-bit pitch). */
#define GR(c, n) ((c)->r[GSP_B(n)])
#define XY_X(v) ((int)(int16_t)((v) & 0xFFFF))
#define XY_Y(v) ((int)(int16_t)((v) >> 16))

static uint16_t cpu_pixel(const wolf *w, uint32_t color)
{
    return (uint16_t)((w->dma[8] & 0xFF) << 8 | (color & 0xFF));
}

void gsp_pixt_to_xy(gsp_t *c, uint32_t value, uint32_t xy)
{
    wolf *w = hw_of(c);
    wput(w, XY_X(xy), XY_Y(xy), cpu_pixel(w, value));
}

void gsp_drav(gsp_t *c, int rs, int rd)
{
    wolf *w = hw_of(c);
    uint32_t p = c->r[rd];
    wput(w, XY_X(p), XY_Y(p), cpu_pixel(w, GR(c, 9)));
    uint32_t inc = c->r[rs];
    c->r[rd] = ((p + (inc & 0xFFFF)) & 0xFFFF) | ((((p >> 16) + (inc >> 16)) & 0xFFFF) << 16);
}

void gsp_fill(gsp_t *c, int xy)
{
    wolf *w = hw_of(c);
    uint32_t dydx = GR(c, 7);
    int dx = (int)(dydx & 0xFFFF), dy = (int)(dydx >> 16);
    if (xy) {
        uint32_t d = GR(c, 2);
        uint16_t px = cpu_pixel(w, GR(c, 9));
        for (int y = 0; y < dy; y++)
            for (int x = 0; x < dx; x++)
                wput(w, XY_X(d) + x, XY_Y(d) + y, px);
        return;
    }
    uint32_t daddr = GR(c, 2), pitch = GR(c, 3);
    if (c->io[0x08] & 0x0800) {
        /* VRAM shift-register mode (DPYCTL SRT): every write copies the
         * erase line into a whole row of `pitch` bits. This is the page
         * erase in the display interrupt. */
        int lines = (int)(pitch / 4096u);
        int row = (int)(daddr >> 12), n = dy * (lines ? lines : 1);
        if (row + n > 512) {                        /* rows past the pages */
            int a = row > 512 ? row : 512;
            video_fill_rows(&w->v, ymap(w, a), row + n - a, 0);
        }
        for (int page = 0; page < 2; page++) {      /* page by page: a whole page also erases its extra rows */
            int a = row > 256 * page ? row : 256 * page;
            int e = row + n < 256 * page + 256 ? row + n : 256 * page + 256;
            if (e <= a)
                continue;
            if (w->extra_y > 0 && e - a >= 240)
                video_fill_rows(&w->v, ymap_page(w, page, 256 * page) - w->extra_y, 256 + 2 * w->extra_y, 0);
            else
                video_fill_rows(&w->v, ymap_page(w, page, a), e - a, 0);
        }
        return;
    }
    int psize = c->io[0x15] ? c->io[0x15] : 8;
    for (int y = 0; y < dy; y++)
        for (int x = 0; x < dx; x++)
            gsp_write(c, daddr + (uint32_t)y * pitch + (uint32_t)(x * psize), psize, GR(c, 9));
}

void gsp_pixblt_b_xy(gsp_t *c)
{
    /* Binary (1 bit/pixel) source expanded to COLOR1/COLOR0 at XY. */
    wolf *w = hw_of(c);
    uint32_t src = GR(c, 0), spitch = GR(c, 1), d = GR(c, 2), dydx = GR(c, 7);
    int transparent = (c->io[0x0B] & 0x20) != 0;
    uint16_t on = cpu_pixel(w, GR(c, 9)), off = cpu_pixel(w, GR(c, 8));
    for (int y = 0; y < (int)(dydx >> 16); y++)
        for (int x = 0; x < (int)(dydx & 0xFFFF); x++) {
            int bit = (int)gsp_read(c, src + (uint32_t)y * spitch + (uint32_t)x, 1);
            if (bit || !transparent)
                wput(w, XY_X(d) + x, XY_Y(d) + y, bit ? on : off);
        }
}

void gsp_line(gsp_t *c, int mode) { (void)c; (void)mode; }

uint32_t gsp_cvxyl(gsp_t *c, uint32_t xy)
{
    int psize = c->io[0x15] ? c->io[0x15] : 8;
    return GR(c, 4) + (uint32_t)XY_Y(xy) * GR(c, 3) + (uint32_t)(XY_X(xy) * psize);
}

/* ---- setup -------------------------------------------------------------------------- */

static uint8_t *load_image(const char *dir, const char *name, size_t want)
{
    char path[1024];
    size_t size;
    fs_join(path, sizeof path, dir, name);
    uint8_t *data = fs_read_file(path, &size);
    if (!data)
        return NULL;
    uint8_t *buf = calloc(1, want + 16);
    if (buf)
        memcpy(buf, data, size < want ? size : want);
    free(data);
    return buf;
}

static int cmp_romimg(const void *a, const void *b)
{
    uint32_t x = ((const wolf_romimg *)a)->sag, y = ((const wolf_romimg *)b)->sag;
    return x < y ? -1 : x > y;
}

static const img_lib *bdd_lib(wolf *w, const char *img_dir, const char *name)
{
    for (int i = 0; i < w->nbdd; i++)
        if (strcmp(w->bdd_name[i], name) == 0)
            return &w->bdd[i];
    if (w->nbdd >= 64)
        return NULL;
    char path[1024], err[512];
    if (!fs_resolve_ci(path, sizeof path, img_dir, name) ||
        !img_lib_load_bdd(&w->bdd[w->nbdd], path, err, sizeof err))
        return NULL;
    snprintf(w->bdd_name[w->nbdd], sizeof w->bdd_name[0], "%s", name);
    return &w->bdd[w->nbdd++];
}

/* The palette a background image is drawn with (gen/bddpal.txt, tools/gsp/genimg.py). The .BDD files carry
 * none; true-colour overrides need one to find each pixel's palette index (src/video/gfx.c). */
struct wolf_bddpal {
    char file[32];
    int index;
    img_palette pal;
};

static void load_bddpal(wolf *w, const char *gen_dir)
{
    char path[1024], line[2048];
    fs_join(path, sizeof path, gen_dir, "bddpal.txt");
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    int cap = 0;
    while (fgets(line, sizeof line, f)) {
        char file[32];
        int index, n, used;
        if (line[0] == '#' || sscanf(line, "%31s %d %d%n", file, &index, &n, &used) != 3 || n <= 0 || n > 256)
            continue;
        uint16_t *cols = calloc((size_t)n, sizeof *cols);
        if (!cols)
            break;
        const char *p = line + used;
        int got = 0;
        for (; got < n; got++) {
            unsigned c;
            int adv;
            if (sscanf(p, "%x%n", &c, &adv) != 1)
                break;
            cols[got] = (uint16_t)(c & 0x7FFF);
            p += adv;
        }
        if (got != n) {
            free(cols);
            continue;
        }
        if (w->nbddpal == cap) {
            int nc = cap ? cap * 2 : 256;
            struct wolf_bddpal *nb = realloc(w->bddpal, (size_t)nc * sizeof *nb);
            if (!nb) {
                free(cols);
                break;
            }
            w->bddpal = nb;
            cap = nc;
        }
        struct wolf_bddpal *b = &w->bddpal[w->nbddpal++];
        memset(b, 0, sizeof *b);
        snprintf(b->file, sizeof b->file, "%s", file);
        b->index = index;
        b->pal.ncolors = (uint16_t)n;
        b->pal.bitspix = 8;
        b->pal.colors = cols;
    }
    fclose(f);
}

static const img_palette *bdd_palette(const wolf *w, const char *file, int index)
{
    for (int i = 0; i < w->nbddpal; i++)
        if (w->bddpal[i].index == index && strcmp(w->bddpal[i].file, file) == 0)
            return &w->bddpal[i].pal;
    return NULL;
}

/* Wrestler frames the game draws with another palette than their own (gen/drawpal.txt, tools/gsp/genimg.py):
 * a true-colour override of one is mapped onto the palette it is drawn with. */
typedef struct {
    char label[IMG_NAME_MAX + 1];
    char pal[IMG_PAL_NAME_MAX + 1];
} drawpal;

static drawpal *load_drawpal(const char *gen_dir, int *count)
{
    char path[1024], line[256];
    *count = 0;
    fs_join(path, sizeof path, gen_dir, "drawpal.txt");
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;
    drawpal *list = NULL;
    int cap = 0;
    while (fgets(line, sizeof line, f)) {
        char label[64], pal[64];
        if (line[0] == '#' || sscanf(line, "%63s %63s", label, pal) != 2 || strlen(label) > IMG_NAME_MAX ||
            strlen(pal) > IMG_PAL_NAME_MAX)
            continue;
        if (*count == cap) {
            int nc = cap ? cap * 2 : 32;
            drawpal *nl = realloc(list, (size_t)nc * sizeof *nl);
            if (!nl)
                break;
            list = nl;
            cap = nc;
        }
        memcpy(list[*count].label, label, strlen(label) + 1);   /* lengths checked above */
        memcpy(list[*count].pal, pal, strlen(pal) + 1);
        (*count)++;
    }
    fclose(f);
    return list;
}

/* The palette `name` is drawn with, if gen/drawpal.txt names one in its library, else NULL. */
static const img_palette *draw_palette(const drawpal *list, int n, const img_lib *il, const char *name)
{
    for (int i = 0; i < n; i++)
        if (strcmp(list[i].label, name) == 0)
            for (int k = 0; k < il->npalettes; k++)
                if (strcmp(il->palettes[k].name, list[i].pal) == 0)
                    return &il->palettes[k];
    return NULL;
}

/* Names in the override directory, listed once (lower case, sorted). */
typedef struct {
    char (*names)[80];
    int n, cap;
} name_set;

static void name_set_cb(const char *name, void *user)
{
    name_set *s = user;
    if (strlen(name) >= sizeof s->names[0])
        return;
    if (s->n == s->cap) {
        int nc = s->cap ? s->cap * 2 : 1024;
        void *p = realloc(s->names, (size_t)nc * sizeof s->names[0]);
        if (!p)
            return;
        s->names = p;
        s->cap = nc;
    }
    char *d = s->names[s->n++];
    size_t i = 0;
    for (; name[i]; i++)
        d[i] = (char)tolower((unsigned char)name[i]);
    d[i] = 0;
}

static int name_cmp(const void *a, const void *b)
{
    return strcmp(a, b);
}

/* Override for an image without a catalog entry (backgrounds): <dir>/<NAME>.png */
static void load_own_override(wolf *w, const name_set *have, gfx_image *gi, const char *name)
{
    char file[96], key[96], path[1024], err[512];
    if (!w->override_dir[0])
        return;
    snprintf(file, sizeof file, "%s.png", name);
    size_t i = 0;
    for (; file[i]; i++)
        key[i] = (char)tolower((unsigned char)file[i]);
    key[i] = 0;
    if (!have->n || !bsearch(key, have->names, (size_t)have->n, sizeof have->names[0], name_cmp))
        return;
    snprintf(path, sizeof path, "%s", file);
    png_indexed png;
    if (!art_load_png(w->override_dir, file, &png, err, sizeof err)) {
        fprintf(stderr, "warning: override ignored: %s\n", err);
        return;
    }
    if (!gfx_attach_override(gi, &png, err, sizeof err))
        fprintf(stderr, "warning: override ignored: %s: %s\n", path, err);
    png_indexed_free(&png);
}

static int load_imgrom(wolf *w, const char *gen_dir, const char *img_dir)
{
    char path[1024];
    fs_join(path, sizeof path, gen_dir, "imgrom.txt");
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    int cap = 0;
    char line[512];
    name_set have = {0};
    load_bddpal(w, gen_dir);
    int ndrawpal;
    drawpal *dpal = load_drawpal(gen_dir, &ndrawpal);
    if (w->override_dir[0]) {
        art_list(w->override_dir, name_set_cb, &have);
        qsort(have.names, (size_t)have.n, sizeof have.names[0], name_cmp);
    }
    while (fgets(line, sizeof line, f)) {
        unsigned sag, wd, ht;
        int index;
        char lib[128], name[64];
        if (line[0] == '#' || sscanf(line, "%x %u %u %127s %d %63s", &sag, &wd, &ht, lib, &index, name) != 6)
            continue;
        const img_lib *il = NULL;
        size_t ln = strlen(lib);
        int is_bdd = ln > 4 && strcmp(lib + ln - 4, ".BDD") == 0;
        if (is_bdd) {
            il = bdd_lib(w, img_dir, lib);
        } else {
            for (int i = 0; i < w->cat.nlibs; i++)
                if (strcmp(w->cat.libs[i].file, lib) == 0 && w->cat.libs[i].loaded)
                    il = &w->cat.libs[i].lib;
        }
        if (!il || index < 0 || index >= il->nimages)
            continue;
        if (w->nimgs == cap) {
            cap = cap ? cap * 2 : 4096;
            wolf_romimg *n = realloc(w->imgs, (size_t)cap * sizeof *n);
            if (!n) {
                fclose(f);
                free(have.names);
                free(dpal);
                return 0;
            }
            w->imgs = n;
        }
        wolf_romimg *r = &w->imgs[w->nimgs++];
        memset(r, 0, sizeof *r);
        r->sag = sag;
        size_t nl = strlen(name) < sizeof r->name ? strlen(name) : sizeof r->name - 1;
        memcpy(r->name, name, nl);
        r->name[nl] = 0;
        r->background = (uint8_t)is_bdd;
        const img_image *im = &il->images[index];
        /* Use the catalog entry (and its override) when it is this very image. */
        const cat_image *ci = is_bdd ? NULL : catalog_find(&w->cat, name);
        const img_palette *dp = is_bdd ? NULL : draw_palette(dpal, ndrawpal, il, name);
        if (ci && catalog_image(&w->cat, ci) == im) {
            r->ci = ci;
            r->img = &w->gc.images[ci - w->cat.images];
            if (dp)
                w->gc.images[ci - w->cat.images].pal = dp;   /* only for mapping a true-colour override */
        } else {
            gfx_image_from_img(&r->own, il, im);   /* pointer set after sorting */
            if (dp)
                r->own.pal = dp;
            if (is_bdd) {
                const img_palette *bp = bdd_palette(w, lib, index);
                if (bp)
                    r->own.pal = bp;                /* only for mapping a true-colour override; mask unchanged */
            }
            load_own_override(w, &have, &r->own, name);
        }
    }
    fclose(f);
    free(have.names);
    free(dpal);
    qsort(w->imgs, (size_t)w->nimgs, sizeof *w->imgs, cmp_romimg);
    for (int i = 0; i < w->nimgs; i++)
        if (!w->imgs[i].img)
            w->imgs[i].img = &w->imgs[i].own;
    return 1;
}

/* Skips the waits on the power-up info screen (version, CMOS, coinage, serial number: DIAG.ASM CTMP_WAIT, a counted
 * delay of 512 rounds, and the wait for a button after an error). Runs at the instructions while the game boots, when SKIP
 * SELF TEST is on. */
uint32_t wolf_trace_pc(const wolf *w, unsigned back)
{
    if (back >= 256 || back >= w->trace_n)
        return 0;
    return w->trace_ring[(w->trace_n - 1 - back) & 255];
}

static void intro_skip(gsp_t *c, void *user)
{
    wolf *w = user;
    w->trace_ring[w->trace_n++ & 255] = c->pc;
    if (!w->skip_selftest)
        return;
    if (c->pc >= w->intro_lo && c->pc < w->intro_hi)
        c->r[0] = 1;
    /* A new or invalid CMOS (a first start, or one that was never saved) makes the screen say "CMOS INVALID -- FACTORY
     * SETTINGS RESTORED" and wait for a button: the loop at CTMP1 compares the buttons now with the ones it saved in
     * SWSET1 and ends when they differ. Whenever the game is seen inside the loop (it calls out to read the switches)
     * the saved ones are made to be something that the buttons cannot be. */
    else if (c->pc >= w->errwait_lo && c->pc < w->errwait_hi && w->swset1_addr >= RAM_BASE && w->swset1_addr < RAM_END)
        put_bits(w->ram, w->swset1_addr - RAM_BASE, 32, 0xFFFFFFFFu);
}

static uint32_t find_symbol_addr(const char *gen_dir, const char *name)
{
    char path[1024];
    fs_join(path, sizeof path, gen_dir, "symbols.txt");
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    char line[256];
    uint32_t found = 0;
    while (!found && fgets(line, sizeof line, f)) {
        unsigned a;
        char n[200];
        if (sscanf(line, "%x %199s", &a, n) == 2 && strcmp(n, name) == 0)
            found = a;
    }
    fclose(f);
    return found;
}

int wolf_set_extra_size(wolf *w, int extra, int extra_y)
{
    if (extra < 0)
        extra = 0;
    if (extra > 3000)
        extra = 3000;
    if (extra_y < 0)
        extra_y = 0;
    if (extra_y > 1400)
        extra_y = 1400;
    const video_sink *sink = w->v.sink;
    video_free(&w->v);
    if (!video_init_bitmap(&w->v, w->scale, 512 + 2 * extra, 2 * (256 + 2 * extra_y) + 512))
        return 0;
    video_set_sink(&w->v, sink);
    w->extra = extra;
    w->extra_y = extra_y;
    w->v.view_x = XPAD + extra;
    return 1;
}

int wolf_set_extra_width(wolf *w, int extra)
{
    return wolf_set_extra_size(w, extra, 0);
}

void wolf_set_draw_margin_y(wolf *w, int margin)
{
    w->draw_margin_y = margin < 0 ? -1 : margin > w->extra_y ? w->extra_y : margin;
}

void wolf_set_draw_margin(wolf *w, int margin)
{
    w->draw_margin = margin < 0 ? -1 : margin > XPAD + w->extra ? XPAD + w->extra : margin;
}

void wolf_set_scroll_inset(wolf *w, int inset)
{
    w->scroll_inset = inset < 0 ? 0 : inset;
}

uint32_t wolf_symbol_addr(const wolf *w, const char *name)
{
    return find_symbol_addr(w->gen_dir, name);
}

void wolf_set_free_play(wolf *w, int on)
{
    w->free_play = on != 0;
}

void wolf_set_flashes(wolf *w, int no_white, int no_red)
{
    w->no_flash_white = no_white != 0;
    w->no_flash_red = no_red != 0;
}

void wolf_set_select_timer(wolf *w, int off)
{
    w->no_select_timer = off != 0;
}

void wolf_set_match_timer(wolf *w, int off)
{
    w->no_match_timer = off != 0;
}

void wolf_set_options(wolf *w, int skip_selftest, unsigned powerup_flags, int no_ringout_timer, int all_shadows)
{
    w->all_shadows = all_shadows;
    w->skip_selftest = skip_selftest;
    /* bit 7 (128, the game's buddy mode) is a computer buddy for player 1 and bit 8 one for player 2. The game's
     * own buddy mode gives both of them one; one alone needs the fourplayer mod, which makes the partners one at a
     * time (mods/fourplayer/mod.c). */
    w->buddy_sides = ((powerup_flags >> 7) & 1u) | (((powerup_flags >> 8) & 1u) << 1);
    w->powerup_flags = (powerup_flags & 0x7Fu) | (w->buddy_sides == 3 ? 128u : 0u);
    w->no_ringout_timer = no_ringout_timer;
}

#define RING_TIME_BIT_OFFSET 0xC40      /* PLYR.EQU RING_TIME in the wrestler process */

/* Fixes for bugs of the original, always on. */
#define NUM_WRES_PROCS 7
#define WRESTLERNUM_BIT_OFFSET 0x590     /* PLYR.EQU WRESTLERNUM in the wrestler process */

#define OBJ_XPOSINT_BIT_OFFSET 0x110     /* PLYR.EQU OBJ_XPOSINT in the wrestler process */
#define PLYRMODE_BIT_OFFSET 0x5C0        /* PLYR.EQU PLYRMODE */
#define MODE_DEAD_VAL 9                  /* PLYR.EQU MODE_DEAD */

int wolf_wrestler_extent(const wolf *w, int *min_x, int *max_x)
{
    int n = 0, lo = 0, hi = 0, all_n = 0, all_lo = 0, all_hi = 0;
    if (!w->procptrs_addr)
        return 0;
    for (int i = 0; i < NUM_WRES_PROCS - 1; i++) {      /* the last process is the referee's */
        uint32_t p = get_bits(w->ram, w->procptrs_addr - RAM_BASE + 32u * (unsigned)i, 32);
        if (p < RAM_BASE || p + OBJ_XPOSINT_BIT_OFFSET + 16 >= RAM_END)
            continue;
        int x = (int16_t)get_bits(w->ram, p + OBJ_XPOSINT_BIT_OFFSET - RAM_BASE, 16);
        unsigned mode = (unsigned)get_bits(w->ram, p + PLYRMODE_BIT_OFFSET - RAM_BASE, 16);
        if (all_n == 0 || x < all_lo)
            all_lo = x;
        if (all_n == 0 || x > all_hi)
            all_hi = x;
        all_n++;
        if (mode == MODE_DEAD_VAL)                                   /* the dead do not count */
            continue;
        if (n == 0 || x < lo)
            lo = x;
        if (n == 0 || x > hi)
            hi = x;
        n++;
    }
    if (n == 0) {                                   /* everybody is dead: all of them, as before */
        n = all_n;
        lo = all_lo;
        hi = all_hi;
    }
    *min_x = lo;
    *max_x = hi;
    return n;
}

static void apply_fixes(wolf *w)
{
    /* Buddy mode can give a drone wrestler number 7, which does not exist (6 is
     * Doink), and the game then jumps to address 0. The MAME cheat "Broken
     * Doink - Crash patch" turns the 7 into a 6 in wrestlers 3 and 4 (the buddy
     * drones; 010E9DD0 and 010EB150 are their WRESTLERNUM bytes). Here every
     * wrestler process is checked, wherever it was allocated. A mod that brings
     * number 7 (mods/adambomb) sets wrestler7 while it is on, and the 7 stays. */
    if (w->wrestler7)
        return;
    if (w->procptrs_addr) {
        for (int i = 0; i < NUM_WRES_PROCS; i++) {
            uint32_t p = get_bits(w->ram, w->procptrs_addr - RAM_BASE + 32u * (unsigned)i, 32);
            if (p >= RAM_BASE && p + WRESTLERNUM_BIT_OFFSET + 16 < RAM_END &&
                get_bits(w->ram, p + WRESTLERNUM_BIT_OFFSET - RAM_BASE, 16) == 7)
                put_bits(w->ram, p + WRESTLERNUM_BIT_OFFSET - RAM_BASE, 16, 6);
        }
    }
    static const uint32_t cheat_bytes[2] = {0x010E9DD0u, 0x010EB150u};
    for (int i = 0; i < 2; i++) {
        uint32_t bit = cheat_bytes[i] - RAM_BASE;
        if (get_bits(w->ram, bit, 8) == 7)
            put_bits(w->ram, bit, 8, 6);
    }
}

/* reduce_bog = wrestlers above two (WRESTLE.ASM init_reduce_bog); while it is not
 * zero, shadows and other work are left out. */
#define PTIME_BIT_OFFSET 0x30           /* mproc.equ PTIME in a process */

static void apply_shadows(wolf *w)
{
    if (!w->all_shadows || !w->reduce_bog_addr)
        return;
    if (get_bits(w->ram, w->reduce_bog_addr - RAM_BASE, 16) == 0)
        return;
    put_bits(w->ram, w->reduce_bog_addr - RAM_BASE, 16, 0);
    /* The crowd process sleeps for 0x7FFF ticks when it sees reduce_bog set, and
     * the game wakes it (PTIME = 1) when it clears reduce_bog itself
     * (WRESTLE2.ASM). Do the same, or the spectators stay still. */
    if (w->crowd_process_addr) {
        uint32_t p = get_bits(w->ram, w->crowd_process_addr - RAM_BASE, 32);
        if (p >= RAM_BASE && p + PTIME_BIT_OFFSET + 16 < RAM_END)
            put_bits(w->ram, p + PTIME_BIT_OFFSET - RAM_BASE, 16, 1);
    }
}

static void apply_game_options(wolf *w)
{
    apply_fixes(w);
    apply_shadows(w);
    if (w->leave_ring_addr)
        put_bits(w->ram, w->leave_ring_addr - RAM_BASE, 16, w->no_ringout_timer ? 1 : 0);
    if (w->no_flash_white_addr)
        put_bits(w->ram, w->no_flash_white_addr - RAM_BASE, 16, w->no_flash_white ? 1 : 0);
    if (w->no_flash_red_addr)
        put_bits(w->ram, w->no_flash_red_addr - RAM_BASE, 16, w->no_flash_red ? 1 : 0);
    if (w->no_match_timer_addr)
        put_bits(w->ram, w->no_match_timer_addr - RAM_BASE, 16, w->no_match_timer ? 1 : 0);
    if (w->no_select_timer_addr)
        put_bits(w->ram, w->no_select_timer_addr - RAM_BASE, 16, w->no_select_timer ? 1 : 0);
    if (w->powerup_flags && w->pu1_addr && w->pu2_addr) {
        uint32_t a[2] = {w->pu1_addr, w->pu2_addr};
        for (int i = 0; i < 2; i++)
            put_bits(w->ram, a[i] - RAM_BASE, 32, get_bits(w->ram, a[i] - RAM_BASE, 32) | w->powerup_flags);
    }
    if (w->no_ringout_timer && w->procptrs_addr) {
        for (int i = 0; i < NUM_WRES_PROCS; i++) {
            uint32_t p = get_bits(w->ram, w->procptrs_addr - RAM_BASE + 32u * (unsigned)i, 32);
            if (p >= RAM_BASE && p + RING_TIME_BIT_OFFSET + 16 < RAM_END)
                put_bits(w->ram, p + RING_TIME_BIT_OFFSET - RAM_BASE, 16, 1);
        }
    }
}

void wolf_set_hud_spread(wolf *w, int dx, int dy)
{
    w->hud_dx = dx < 0 ? 0 : dx;
    w->hud_dy = dy < 0 ? 0 : dy;
}

/* WORLDTLX inside scroll_world's range: a level screen, not a menu. */
static int level_screen(const wolf *w)
{
    if (!w->worldtlx_addr)
        return 0;
    int32_t x = (int32_t)get_bits(w->ram, w->worldtlx_addr - RAM_BASE, 32);
    return x >= (0x12F << 16) && x <= (0x648 << 16);
}

/* The object's flags say it is placed on the screen, not in the world. */
#define OFLAGS_BIT_OFFSET 0xE0
#define B_SCRNREL_MASK (1u << 13)

#define OOID_BIT_OFFSET 0x190
/* more of DISPLAY.EQU's object block */
#define OLINK_BIT_OFFSET 0x000
#define OXPOS_BIT_OFFSET 0x090
#define OYPOS_BIT_OFFSET 0x0B0
#define OSIZEX_BIT_OFFSET 0x130
#define OSIZEY_BIT_OFFSET 0x140
#define ODXOFF_BIT_OFFSET 0x220
#define ODYOFF_BIT_OFFSET 0x230

/* The strings on the screen: every letter is an object of its own (print_string, JAM_STR), so the letters of
 * one line are put together by place: letters side by side (overlapping rows, at most HUD_TEXT_GAP apart) make
 * one box. Walks the object list from obj to its end. */
#define HUD_TEXT_GAP 12

static int hud_text_near(const int *a, const int *b)
{
    return a[0] <= b[2] + HUD_TEXT_GAP && b[0] <= a[2] + HUD_TEXT_GAP && a[1] < b[3] && b[1] < a[3];
}

static void hud_text_box(gsp_t *c, uint32_t obj, int *b)
{
    b[0] = (int16_t)gsp_read(c, obj + OXPOS_BIT_OFFSET, 16) - (int16_t)gsp_read(c, obj + ODXOFF_BIT_OFFSET, 16);
    b[1] = (int16_t)gsp_read(c, obj + OYPOS_BIT_OFFSET, 16) - (int16_t)gsp_read(c, obj + ODYOFF_BIT_OFFSET, 16);
    b[2] = b[0] + (int)gsp_read(c, obj + OSIZEX_BIT_OFFSET, 16);
    b[3] = b[1] + (int)gsp_read(c, obj + OSIZEY_BIT_OFFSET, 16);
}

/* A letter: a small object placed on the screen, at most HUD_TEXT_LETTER each way. The OID does not tell (the
 * strings use many: TYPTEXT, the Royal Rumble's damage line CLSDEAD, ...), and bigger pieces (the banner of "MATCH
 * AWARDED TO", the winner's name in one picture, the life bars) keep their own place rules. */
#define HUD_TEXT_LETTER 40

static int hud_text_is(gsp_t *c, uint32_t obj)
{
    return (gsp_read(c, obj + OFLAGS_BIT_OFFSET, 16) & B_SCRNREL_MASK) &&
           (int)gsp_read(c, obj + OSIZEX_BIT_OFFSET, 16) <= HUD_TEXT_LETTER &&
           (int)gsp_read(c, obj + OSIZEY_BIT_OFFSET, 16) <= HUD_TEXT_LETTER;
}

static void hud_text_scan(wolf *w, gsp_t *c, uint32_t obj)
{
    enum { MAXG = (int)(sizeof w->hud_text.g / sizeof w->hud_text.g[0]) };
    w->hud_text.n = 0;
    for (int guard = 0; obj >= RAM_BASE && obj < RAM_END && guard < 4096; guard++) {
        if (hud_text_is(c, obj)) {
            int b[4], k;
            hud_text_box(c, obj, b);
            for (k = 0; k < w->hud_text.n && !hud_text_near(w->hud_text.g[k], b); k++)
                ;
            if (k < w->hud_text.n) {
                int *g = w->hud_text.g[k];
                if (b[0] < g[0]) g[0] = b[0];
                if (b[1] < g[1]) g[1] = b[1];
                if (b[2] > g[2]) g[2] = b[2];
                if (b[3] > g[3]) g[3] = b[3];
            } else if (w->hud_text.n < MAXG) {
                memcpy(w->hud_text.g[w->hud_text.n++], b, sizeof b);
            }
        }
        obj = gsp_read(c, obj + OLINK_BIT_OFFSET, 32);
    }
    /* a letter that came between two boxes joins them */
    for (int i = 0; i < w->hud_text.n; i++)
        for (int j = i + 1; j < w->hud_text.n; j++)
            if (hud_text_near(w->hud_text.g[i], w->hud_text.g[j])) {
                int *g = w->hud_text.g[i], *h = w->hud_text.g[j];
                if (h[0] < g[0]) g[0] = h[0];
                if (h[1] < g[1]) g[1] = h[1];
                if (h[2] > g[2]) g[2] = h[2];
                if (h[3] > g[3]) g[3] = h[3];
                memcpy(h, w->hud_text.g[--w->hud_text.n], sizeof w->hud_text.g[0]);
                i = -1;
                break;
            }
}

void gsp_hud_shift(gsp_t *c)
{
    wolf *w = hw_of(c);
    if (w->hide_ropes) {
        /* the front and back rope objects (GAME.EQU: CLSNEUT|TYPNEUT|SUBROPE|SUBHORZ, 0x1A): an empty blit; the
         * side ropes (SUBSIDE, 0x2A) stay */
        uint32_t oid = gsp_read(c, c->r[0] + OOID_BIT_OFFSET, 16);
        if ((oid & 0xFFFFu) == 0x001Au)
            c->r[9] = 0;
    }
    if ((w->hud_dx <= 0 && w->hud_dy <= 0) || !w->hud_level)
        return;
    uint32_t obj = c->r[0];
    if (!(gsp_read(c, obj + OFLAGS_BIT_OFFSET, 16) & B_SCRNREL_MASK))
        return;
    int x = (int16_t)(c->r[10] & 0xFFFF), y = (int16_t)(c->r[10] >> 16);
    int wd = (int)(c->r[9] & 0xFFFF), ht = (int)(c->r[9] >> 16);
    if (ht > 200)                       /* whole-screen overlays stay */
        return;
    /* For a horizontally flipped blit the display code has moved the destination to the far
     * (right) edge: the picture is drawn backwards from there (B_FLIPH is bit 4 of A12). */
    if ((c->r[12] >> 4) & 1)
        x -= wd - 1;
    int cx = x + wd / 2, cy = y + ht / 2, dx = 0, dy = 0;
    /* A letter goes with its whole line: "CHALLENGER FOUND!" in the middle of the screen came apart, each
     * letter moved by the side it is on. The boxes are made once a frame, at the first text object drawn (the
     * display draws the list from its head, so that walk sees all of them). */
    if (hud_text_is(c, obj)) {
        if (w->hud_text.frame != w->frames + 1) {
            w->hud_text.frame = w->frames + 1;
            hud_text_scan(w, c, obj);
        }
        int b[4];
        hud_text_box(c, obj, b);
        for (int pass = 0; pass < 2; pass++) {
            int k = 0;
            while (k < w->hud_text.n && !(b[0] >= w->hud_text.g[k][0] && b[2] <= w->hud_text.g[k][2] &&
                                          b[1] >= w->hud_text.g[k][1] && b[3] <= w->hud_text.g[k][3]))
                k++;
            if (k < w->hud_text.n) {
                const int *g = w->hud_text.g[k];
                wd = g[2] - g[0];
                cx = g[0] + wd / 2;
                cy = (g[1] + g[3]) / 2;
                break;
            }
            hud_text_scan(w, c, obj);   /* on another object list, or added since */
        }
    }
    /* Wide strips (the special move message's bars are 395 wide and slide in from
     * a side) go with their side; one that is centred stays. */
    if (wd > 300 && cx > 200 - 25 && cx < 200 + 25)
        return;
    /* Pieces go with the side they lie on; whatever is in the middle stays, however wide (the timer, the
     * credit, the winner's name and "perfect" are 130 to 200 wide and were pushed to the right). */
    if (cx < 200 - 25 || cx > 200 + 25)
        dx = cx < 200 ? -w->hud_dx : w->hud_dx;
    if (cy < 100)
        dy = -w->hud_dy;
    else if (cy > 170)
        dy = w->hud_dy;
    if ((c->r[12] >> 4) & 1)
        x += wd - 1;                   /* back to the destination the display code uses */
    c->r[10] = ((uint32_t)(y + dy) << 16) | (uint32_t)((x + dx) & 0xFFFF);
}

void wolf_set_scroll_inset_y(wolf *w, int inset)
{
    w->scroll_inset_y = inset < 0 ? 0 : inset;
}

/* scroll_world only lets WORLDTLX (16:16) stay in [0x12F, 0x648]. */
static void apply_scroll_inset(wolf *w)
{
    enum { LIM_L = 0x12F, LIM_R = 0x648 };
    if ((w->scroll_inset <= 0 && w->scroll_inset_y <= 0) || !w->worldtlx_addr)
        return;
    /* The ending (INPARTY, GAME.EQU: the fireworks and the congratulation text) pans the camera by script over the
     * whole level. The clamp below kept it from reaching its targets in a wide view, so the segment never ended and the
     * text was never shown (or cut short): the clamp is for the scrolling screens of the game, not for this. */
    if (w->gamstate_addr && get_bits(w->ram, w->gamstate_addr - RAM_BASE, 16) == 6)
        return;
    int32_t x = (int32_t)get_bits(w->ram, w->worldtlx_addr - RAM_BASE, 32);
    if (w->scroll_inset_y > 0 && w->worldtly_addr &&
        x >= (int32_t)((uint32_t)LIM_L << 16) && x <= (int32_t)((uint32_t)LIM_R << 16)) {
        enum { BG_TOP = -142, CAM_BOTTOM = 0x97 };
        int32_t y = (int32_t)get_bits(w->ram, w->worldtly_addr - RAM_BASE, 32);
        int32_t lo = (int32_t)((uint32_t)(BG_TOP + w->scroll_inset_y) << 16);
        int32_t hi = (int32_t)((uint32_t)(CAM_BOTTOM - w->scroll_inset_y) << 16);
        if (lo > hi)            /* the view is taller than the level: keep it centered */
            lo = hi = (int32_t)(((int64_t)lo + hi) / 2);
        if (y < lo || y > hi)
            put_bits(w->ram, w->worldtly_addr - RAM_BASE, 32, (uint32_t)(y < lo ? lo : hi));
    }
    if (w->scroll_inset <= 0)
        return;
    int32_t lo = (int32_t)((uint32_t)(LIM_L + w->scroll_inset) << 16);
    int32_t hi = (int32_t)((uint32_t)(LIM_R - w->scroll_inset) << 16);
    if (lo > hi)                /* the view is wider than the level: keep it centered */
        lo = hi = (int32_t)(((int64_t)lo + hi) / 2);
    if (x < (int32_t)((uint32_t)LIM_L << 16) || x > (int32_t)((uint32_t)LIM_R << 16))
        return;             /* not a scrolling level screen */
    if (x < lo)
        x = lo;
    else if (x > hi)
        x = hi;
    else
        return;
    put_bits(w->ram, w->worldtlx_addr - RAM_BASE, 32, (uint32_t)x);
}

/* SCRNTL/SCRNLR are [Y,X] longs; the game sets them once at start-up and
 * culls backgrounds and objects against them. */
static void apply_draw_margin(wolf *w)
{
    /* keep_onscreen's limit for a wrestler outside the ring follows the wide view */
    if (w->view_extra_addr)
        put_bits(w->ram, w->view_extra_addr - RAM_BASE, 16, w->draw_margin > 0 ? (uint32_t)w->draw_margin : 0);
    if ((w->draw_margin < 0 && w->draw_margin_y < 0) || !w->scrntl_addr || !w->scrnlr_addr)
        return;
    uint32_t br = get_bits(w->ram, w->scrnlr_addr - RAM_BASE, 32);
    if (!br)
        return;             /* not initialized yet */
    if (w->draw_margin >= 0) {
        put_bits(w->ram, w->scrntl_addr - RAM_BASE, 16, (uint32_t)(-w->draw_margin) & 0xFFFF);
        put_bits(w->ram, w->scrnlr_addr - RAM_BASE, 16, (uint32_t)(VIDEO_W + w->draw_margin));
    }
    if (w->draw_margin_y >= 0) {
        put_bits(w->ram, w->scrntl_addr - RAM_BASE + 16, 16, (uint32_t)(-w->draw_margin_y) & 0xFFFF);
        put_bits(w->ram, w->scrnlr_addr - RAM_BASE + 16, 16, (uint32_t)(VIDEO_H + w->draw_margin_y));
    }
}

/* With WWF_TRACE in the environment, wolf_init says how far it got (through the warning callback). */
#define TRACE(msg) do { if (warn && getenv("WWF_TRACE")) warn("trace: " msg, NULL); } while (0)

int wolf_init(wolf *w, const char *gen_dir, const char *img_dir, const char *override_dir,
              int scale, const char *cmos_path, catalog_warn_fn warn)
{
    memset(w, 0, sizeof *w);
    w->draw_margin = -1;
    w->draw_margin_y = -1;
    w->cpu.hw = w;
    snprintf(w->gen_dir, sizeof w->gen_dir, "%s", gen_dir);
    TRACE("reading rom.bin and ram.bin");
    w->rom = load_image(gen_dir, "rom.bin", ROM_BYTES);
    w->ram = load_image(gen_dir, "ram.bin", (RAM_END - RAM_BASE) / 8);
    w->cmos = calloc(1, (CMOS_END - CMOS_BASE) / 8 + 16);
    if (!w->rom || !w->ram || !w->cmos)
        return 0;
    gsp_ram = w->ram;
    if (cmos_path) {
        snprintf(w->cmos_path, sizeof w->cmos_path, "%s", cmos_path);
        size_t size;
        uint8_t *data = fs_read_file(cmos_path, &size);
        if (data) {
            size_t n = (CMOS_END - CMOS_BASE) / 8;
            memcpy(w->cmos, data, size < n ? size : n);
            free(data);
        }
    }
    {   /* the default LODs, then those the mods added (gen/extra_lods.txt) */
        static char extra[16][1024];
        const char *lods[64];
        int n = 0;
        for (; catalog_default_lods[n]; n++)
            lods[n] = catalog_default_lods[n];
        char path[1024];
        fs_join(path, sizeof path, gen_dir, "extra_lods.txt");
        FILE *ef = fopen(path, "r");
        for (int k = 0; ef && k < 16 && n < 63 && fgets(extra[k], sizeof extra[k], ef); ) {
            extra[k][strcspn(extra[k], "\r\n")] = 0;
            if (extra[k][0]) {
                if (strchr(extra[k], '/')) {    /* a LOD the mod carries: in the gen dir */
                    char rel[1024];
                    snprintf(rel, sizeof rel, "%.1023s", extra[k]);
                    fs_join(extra[k], sizeof extra[k], gen_dir, rel);
                }
                lods[n++] = extra[k++];
            }
        }
        if (ef)
            fclose(ef);
        lods[n] = NULL;
        TRACE("opening the image catalog (and looking through the art folder)");
        if (!catalog_open(&w->cat, img_dir, override_dir, lods, warn, NULL))
            return 0;
    }
    TRACE("catalog open; image cache");
    if (!gfx_cache_init(&w->gc, &w->cat, warn, NULL))
        return 0;
    if (override_dir)
        snprintf(w->override_dir, sizeof w->override_dir, "%s", override_dir);
    TRACE("reading the image ROM map");
    if (!load_imgrom(w, gen_dir, img_dir))
        return 0;
    TRACE("making the bitmap");
    w->scale = scale;
    if (!video_init_bitmap(&w->v, scale, 512, 1024))
        return 0;
    /* Serial PIC data decoding to game number 430 (WrestleMania) and serial
     * number 123456 with DIAG.ASM's _read_pic_data (X = Y = 0):
     *   bytes 0-2  (130 * 581 + 15732)       -> digits 1, 3
     *   bytes 3-6  (464 * 4223 + 7463513)    -> digits 4 (game), 0, 6, 4
     *   bytes 7-9  (352 * 7117 + 127984)     -> digits 3 (game), 5, 2      */
    {
        uint32_t b359 = 130u * 581u + 15732u, a086 = 464u * 4223u + 7463513u,
                 x174 = 352u * 7117u + 127984u;
        for (int i = 0; i < 3; i++)
            w->pic_serial[i] = (uint8_t)(b359 >> (8 * i));
        for (int i = 0; i < 4; i++)
            w->pic_serial[3 + i] = (uint8_t)(a086 >> (8 * i));
        for (int i = 0; i < 3; i++)
            w->pic_serial[7 + i] = (uint8_t)(x174 >> (8 * i));
    }
    TRACE("looking up symbols");
    w->v.view_x = XPAD;
    w->gamstate_addr = find_symbol_addr(gen_dir, "GAMSTATE");
    w->leave_ring_addr = find_symbol_addr(gen_dir, "leave_ring");
    w->index1_addr = find_symbol_addr(gen_dir, "index1");
    w->index2_addr = find_symbol_addr(gen_dir, "index2");
    w->lineup_addr = find_symbol_addr(gen_dir, "FINAL_BATTLE_LINEUP");
    w->intro_lo = find_symbol_addr(gen_dir, "CTMP_WAIT");
    w->intro_hi = find_symbol_addr(gen_dir, "CTMP2");
    w->errwait_lo = find_symbol_addr(gen_dir, "CTMP1");
    w->errwait_hi = find_symbol_addr(gen_dir, "CTMP0");
    w->swset1_addr = find_symbol_addr(gen_dir, "SWSET1");
    if (w->intro_lo && w->intro_hi > w->intro_lo && w->errwait_hi > w->errwait_lo && w->swset1_addr && !w->cpu.trace) {
        w->cpu.trace = intro_skip;
        w->cpu.trace_user = w;
    }
    w->no_select_timer_addr = find_symbol_addr(gen_dir, "no_select_timer");
    w->no_match_timer_addr = find_symbol_addr(gen_dir, "no_match_timer");
    w->no_flash_white_addr = find_symbol_addr(gen_dir, "no_flash_white");
    w->no_flash_red_addr = find_symbol_addr(gen_dir, "no_flash_red");
    w->view_extra_addr = find_symbol_addr(gen_dir, "view_extra");
    w->scrntl_addr = find_symbol_addr(gen_dir, "SCRNTL");
    w->scrnlr_addr = find_symbol_addr(gen_dir, "SCRNLR");
    w->worldtlx_addr = find_symbol_addr(gen_dir, "WORLDTLX");
    w->worldtly_addr = find_symbol_addr(gen_dir, "WORLDTLY");
    w->reduce_bog_addr = find_symbol_addr(gen_dir, "reduce_bog");
    w->crowd_process_addr = find_symbol_addr(gen_dir, "crowd_process");
    w->pu1_addr = find_symbol_addr(gen_dir, "p1powerup_request");
    w->pu2_addr = find_symbol_addr(gen_dir, "p2powerup_request");
    w->procptrs_addr = find_symbol_addr(gen_dir, "process_ptrs");
    w->frame_insns = 110000;
    w->dip = 0xFFFF;
    gsp_reset(&w->cpu);
    return 1;
}

void wolf_free(wolf *w)
{
    mods_shutdown(w);
    if (w->cmos_path[0] && w->cmos) {
        FILE *f = fopen(w->cmos_path, "wb");
        if (f) {
            fwrite(w->cmos, 1, (CMOS_END - CMOS_BASE) / 8, f);
            fclose(f);
        }
    }
    video_free(&w->v);
    gfx_cache_free(&w->gc);
    catalog_close(&w->cat);
    for (int i = 0; i < w->nimgs; i++)
        if (!w->imgs[i].own.entry && w->imgs[i].own.hi)
            gfx_image_free_override(&w->imgs[i].own);
    for (int i = 0; i < w->nbdd; i++)
        img_lib_free(&w->bdd[i]);
    wolf_trace_draws(w, 0);
    for (int i = 0; i < w->nbddpal; i++)
        free(w->bddpal[i].pal.colors);
    free(w->bddpal);
    free(w->imgs);
    free(w->rom);
    free(w->ram);
    free(w->cmos);
    gsp_ram = NULL;
}

int wolf_trace_draws(wolf *w, int on)
{
    for (int p = 0; p < 2; p++) {
        if (on && !w->draws[p]) {
            w->draws[p] = calloc(WOLF_MAX_DRAWS, sizeof *w->draws[p]);
            if (!w->draws[p]) {
                wolf_trace_draws(w, 0);
                return 0;
            }
        } else if (!on) {
            free(w->draws[p]);
            w->draws[p] = NULL;
        }
        w->ndraws[p] = 0;
        w->draws_frame[p] = 0;
    }
    return 1;
}

const wolf_draw *wolf_shown_draws(const wolf *w, int *n)
{
    int page = w->display_row >= 256 ? 1 : 0;
    *n = w->draws[page] ? w->ndraws[page] : 0;
    return w->draws[page];
}

int wolf_frame(wolf *w)
{
    gsp_t *c = &w->cpu;
    int di_line = c->io[0x0A];
    if (di_line <= 0 || di_line >= LINES_PER_FRAME)
        di_line = 254;
    int32_t first = (int32_t)((int64_t)w->frame_insns * di_line / LINES_PER_FRAME);

    apply_draw_margin(w);
    apply_game_options(w);
    mods_frame_begin(w);
    gsp_run(c, first);
    if (c->stop)
        return 0;
    apply_fixes(w);
    apply_shadows(w);
    apply_scroll_inset(w);
    w->hud_level = level_screen(w);
    w->gamstate = w->gamstate_addr ? (int)get_bits(w->ram, w->gamstate_addr - RAM_BASE, 16) : -1;
    w->in_match = w->gamstate == 4;   /* GAME.EQU INGAME */
    c->io[0x12] |= GSP_INT_DI;
    /* VCOUNT continues from the display interrupt line */
    gsp_run(c, w->frame_insns - first);
    if (c->stop)
        return 0;

    uint16_t start = c->io[0x09];    /* DPYSTRT */
    w->display_row = (int)((uint16_t)~start >> 4) & 511;
    w->v.view_y = ymap(w, w->display_row);
    w->frames++;
    if (c->trace == intro_skip && w->frames > 1500)   /* booted: the hook is not needed any more */
        c->trace = NULL;
    mods_frame_end(w);
    return 1;
}

/* ---- prefetch of the high-resolution art ------------------------------------------------------------------ */

/* The LOD script of each wrestler number (the tables of WRESTLE2.ASM: 0 Bret Hart, 1 Razor Ramon, 2 Undertaker,
 * 3 Yokozuna, 4 Shawn Michaels, 5 Bam Bam, 6 Doink, 7 Lex Luger). */
static const char *const wrestler_lod[8] = {"BRET.LOD", "RAZOR.LOD", "TAKER.LOD", "YOKO.LOD",
                                            "SHAWN.LOD", "BAM.LOD", "DOINK.LOD", "LEX.LOD"};

static int lib_has(const char *file, const char *part)
{
    return strstr(file, part) != NULL;
}

/* Queues a wrestler's frames: the standing and walking libraries first (what the match opens with), then the rest. */
static void prefetch_wrestler(wolf *w, int n)
{
    if (n < 0 || n > 7 || (w->prefetch_done >> n & 1u))
        return;
    w->prefetch_done |= 1u << n;
    const catalog *cat = &w->cat;
    int lod = -1;
    for (int i = 0; i < cat->nlods && lod < 0; i++) {
        const char *base = fs_basename(cat->lods[i].path);
        if (strlen(base) == strlen(wrestler_lod[n]) && str_iendswith(base, wrestler_lod[n]))
            lod = i;
    }
    if (lod < 0)
        return;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < cat->nimages; i++) {
            const cat_image *ci = &cat->images[i];
            const char *file = cat->libs[ci->lib].file;
            int first = lib_has(file, "WLK") || lib_has(file, "STAND");
            if (ci->lod == lod && ci->has_override && first == (pass == 0))
                gfx_prefetch_add(&w->gc, ci);
        }
}

void wolf_prefetch_tick(wolf *w)
{
    if (!w->gc.submit || !w->gamstate_addr)
        return;
    unsigned st = get_bits(w->ram, w->gamstate_addr - RAM_BASE, 16);
    if (st == 3 || st == 4 || st == 5 || st == 9) {   /* GAME.EQU INPREGAME, INGAME, INWAITCONT, INPREGAME2 */
        if (w->index1_addr && w->index2_addr) {
            prefetch_wrestler(w, (int)get_bits(w->ram, w->index1_addr - RAM_BASE, 16));
            prefetch_wrestler(w, (int)get_bits(w->ram, w->index2_addr - RAM_BASE, 16));
        }
        if (w->lineup_addr) {
            uint8_t line[9];
            unsigned seen = 0;
            int valid = 1;
            for (int i = 0; i < 9; i++)
                line[i] = (uint8_t)get_bits(w->ram, w->lineup_addr - RAM_BASE + 8u * (unsigned)i, 8);
            for (int i = 0; i < 8; i++) {
                if (line[i] > 7 || (seen >> line[i] & 1u))
                    valid = 0;
                else
                    seen |= 1u << line[i];
            }
            if (valid && line[8] == 0xFF && memcmp(line, w->prefetch_lineup, sizeof line) != 0) {
                memcpy(w->prefetch_lineup, line, sizeof line);
                for (int i = 0; i < 8; i++)
                    prefetch_wrestler(w, line[i]);   /* in the order they fight */
            }
        }
    }
}

/* ---- symbols for diagnostics ----------------------------------------------------------- */

static struct {
    uint32_t *addr;
    char **name;
    int n;
} syms;

const char *wolf_symbol(const char *gen_dir, uint32_t addr, uint32_t *offset)
{
    if (!syms.n) {
        char path[1024];
        fs_join(path, sizeof path, gen_dir, "symbols.txt");
        FILE *f = fopen(path, "r");
        if (!f)
            return NULL;
        int cap = 0;
        char line[256];
        while (fgets(line, sizeof line, f)) {
            unsigned a;
            char name[200];
            if (sscanf(line, "%x %199s", &a, name) != 2)
                continue;
            if (syms.n == cap) {
                cap = cap ? cap * 2 : 8192;
                syms.addr = realloc(syms.addr, (size_t)cap * sizeof *syms.addr);
                syms.name = realloc(syms.name, (size_t)cap * sizeof *syms.name);
            }
            syms.addr[syms.n] = a;
            syms.name[syms.n] = malloc(strlen(name) + 1);
            strcpy(syms.name[syms.n], name);
            syms.n++;
        }
        fclose(f);
    }
    int lo = 0, hi = syms.n - 1, best = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (syms.addr[mid] <= addr) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    if (best < 0)
        return NULL;
    if (offset)
        *offset = addr - syms.addr[best];
    return syms.name[best];
}

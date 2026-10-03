/*
 * DCS board model (see dcs.h). The latch, boot and autobuffer behavior
 * follows MAME's dcs.cpp, which is the reference for these boards.
 */
#include "dcs.h"

#include <stdlib.h>
#include <string.h>

enum {
    S1_AUTOBUF = 0x0F,
    S1_SCLKDIV = 0x11,
    S1_CONTROL = 0x12,
    TSCALE = 0x1B,
    TCOUNT = 0x1C,
    TPERIOD = 0x1D,
    SYSCONTROL = 0x1F,
};

#define LATCH_INPUT_EMPTY 0x0800
#define LATCH_OUTPUT_EMPTY 0x0400

static uint16_t rom_word(const dcs *d, uint32_t w)
{
    if (w < (uint32_t)d->nroms * DCS_ROM_BYTES)
        return (uint16_t)(0xFF00 | d->rom[w]);
    return 0xFFFF;
}

static void boot(dcs *d)
{
    uint8_t page[0x1000];
    uint32_t base = (uint32_t)d->bank * 0x1000;
    for (int i = 0; i < 0x1000; i++)
        page[i] = (uint8_t)rom_word(d, base + (uint32_t)i);
    adsp_boot(&d->cpu, page);
}

static void autobuffer_stop(dcs *d) { d->ab_on = 0; }

/*
 * A write to TX1 starts the autobuffered transmit (MAME: sound_tx_callback
 * and recompute_sample_rate). The buffer is I[ireg] (rounded down to 16
 * words, as MAME does) with length L[ireg] and step M[mreg].
 */
static void tx(void *ctx, int port, uint16_t value)
{
    dcs *d = ctx;
    (void)value;
    if (port != 1)
        return;
    if (!(d->ctrl[SYSCONTROL] & 0x0800) || !(d->ctrl[S1_AUTOBUF] & 0x0002)) {
        autobuffer_stop(d);
        return;
    }
    adsp2105 *a = &d->cpu;
    int ireg = (d->ctrl[S1_AUTOBUF] >> 9) & 7;
    int mreg = ((d->ctrl[S1_AUTOBUF] >> 7) & 3) | (ireg & 4);
    d->ab_ireg = ireg;
    d->ab_incs = a->m[mreg];
    d->ab_size = a->l[ireg];
    a->i[ireg] &= (uint16_t)~0xF;
    d->ab_base = a->i[ireg];
    /* cycles per sample: 16 bits of SCLK, or the external 31.25 kHz clock */
    int64_t per_sample = (d->ctrl[S1_CONTROL] & 0x4000)
                             ? 2 * ((int64_t)d->ctrl[S1_SCLKDIV] + 1) * 16
                             : DCS_CLOCK / 31250;
    d->sample_rate = (uint32_t)(DCS_CLOCK / per_sample);
    if (d->ab_incs > 0 && d->ab_size > 0) {
        d->ab_period = per_sample * d->ab_size / (2 * d->ab_incs);
        d->ab_next = d->now + d->ab_period;
        d->ab_on = 1;
    } else {
        d->ab_on = 0;
    }
}

static void emit(dcs *d, int16_t s)
{
    if (d->nout == d->cap) {
        size_t n = d->cap ? d->cap * 2 : 65536;
        int16_t *p = realloc(d->out, n * sizeof *p);
        if (!p)
            return;
        d->out = p;
        d->cap = n;
    }
    d->out[d->nout++] = s;
}

static uint16_t dm_read(void *ctx, uint16_t addr);

/* Half the buffer has gone out to the DAC; the transmit interrupt comes
 * when the whole buffer has (MAME: dcs_irq). */
static void autobuffer_event(dcs *d)
{
    adsp2105 *a = &d->cpu;
    int count = d->ab_size / (2 * d->ab_incs);
    uint32_t reg = a->i[d->ab_ireg];
    for (int k = 0; k < count; k++) {
        emit(d, (int16_t)dm_read(d, (uint16_t)(reg & 0x3FFF)));
        reg += (uint32_t)d->ab_incs;
    }
    uint32_t l = d->ab_size, p = 1;
    while (p < l)
        p <<= 1;
    uint32_t base = a->i[d->ab_ireg] & ~(p - 1);
    if (reg >= base + l) {
        reg = base;
        adsp_pulse_irq(a, ADSP_INT_SPORT1_TX);
    }
    a->i[d->ab_ireg] = (uint16_t)(reg & 0x3FFF);
}

static uint16_t dm_read(void *ctx, uint16_t addr)
{
    dcs *d = ctx;
    if (addr < 0x0800)
        return d->dm_lo[addr];
    if (addr < 0x2000)
        return (uint16_t)(d->cpu.pm[addr] >> 8);
    if (addr < 0x3000)
        return rom_word(d, (uint32_t)(d->bank & 0x7FF) * 0x1000 + (addr & 0x0FFF));
    if (addr < 0x3400)
        return 0xFFFF;
    if (addr < 0x3800) {
        /* reading the command latch acknowledges it */
        d->latch |= LATCH_INPUT_EMPTY;
        adsp_set_irq(&d->cpu, ADSP_INT_IRQ2, 0);
        return d->input;
    }
    if (addr >= 0x3FE0) {
        int r = addr - 0x3FE0;
        if (r == TCOUNT)
            return d->cpu.tcount;
        return d->ctrl[r];
    }
    return d->dm_hi[addr - 0x3800];
}

static void dm_write(void *ctx, uint16_t addr, uint16_t v)
{
    dcs *d = ctx;
    if (addr < 0x0800) {
        d->dm_lo[addr] = v;
    } else if (addr < 0x2000) {
        d->cpu.pm[addr] = ((uint32_t)v << 8) | (d->cpu.pm[addr] & 0xFF);
    } else if (addr < 0x3000) {
        /* ROM window */
    } else if (addr < 0x3400) {
        d->bank = v & 0x7FF;
    } else if (addr < 0x3800) {
        d->output = v;
        d->latch &= (uint16_t)~LATCH_OUTPUT_EMPTY;
    } else if (addr >= 0x3FE0) {
        int r = addr - 0x3FE0;
        d->ctrl[r] = v;
        switch (r) {
        case SYSCONTROL:
            if (v & 0x0200) {   /* boot force */
                adsp_reset(&d->cpu);
                boot(d);
                d->ctrl[SYSCONTROL] = 0;
            }
            if (!(v & 0x0800))
                autobuffer_stop(d);
            break;
        case S1_AUTOBUF:
            if (!(v & 0x0002))
                autobuffer_stop(d);
            break;
        case TSCALE:
            d->cpu.tscale = v & 0xFF;
            break;
        case TCOUNT:
            d->cpu.tcount = v;
            break;
        case TPERIOD:
            d->cpu.tperiod = v;
            break;
        default:
            break;
        }
    } else {
        d->dm_hi[addr - 0x3800] = v;
    }
}

static void full_reset(dcs *d)
{
    d->bank = 0;
    memset(d->ctrl, 0, sizeof d->ctrl);
    d->latch = LATCH_INPUT_EMPTY | LATCH_OUTPUT_EMPTY;
    d->ab_on = 0;
    adsp_reset(&d->cpu);
    boot(d);
}

int dcs_init(dcs *d, const uint8_t *const roms[], int nroms)
{
    memset(d, 0, sizeof *d);
    if (nroms < 1 || nroms > DCS_MAX_ROMS)
        return 0;
    d->rom = malloc((size_t)nroms * DCS_ROM_BYTES);
    if (!d->rom)
        return 0;
    for (int i = 0; i < nroms; i++)
        memcpy(d->rom + (size_t)i * DCS_ROM_BYTES, roms[i], DCS_ROM_BYTES);
    d->nroms = nroms;
    d->cpu.bus.ctx = d;
    d->cpu.bus.dm_read = dm_read;
    d->cpu.bus.dm_write = dm_write;
    d->cpu.bus.tx = tx;
    full_reset(d);
    return 1;
}

void dcs_free(dcs *d)
{
    free(d->rom);
    free(d->out);
    d->rom = NULL;
    d->out = NULL;
}

void dcs_reset_line(dcs *d, int state)
{
    if (state) {
        full_reset(d);
        d->halted = 1;
    } else {
        d->halted = 0;
    }
}

void dcs_write(dcs *d, uint8_t byte)
{
    d->input = byte;
    d->latch &= (uint16_t)~LATCH_INPUT_EMPTY;
    adsp_set_irq(&d->cpu, ADSP_INT_IRQ2, 1);
}

uint16_t dcs_read(dcs *d)
{
    d->latch |= LATCH_OUTPUT_EMPTY;
    return d->output;
}

uint16_t dcs_status(const dcs *d) { return d->latch; }

void dcs_run(dcs *d, int64_t cycles)
{
    int64_t end = d->now + cycles;
    while (d->now < end) {
        int64_t stop = end;
        if (d->ab_on && d->ab_next < stop)
            stop = d->ab_next;
        if (stop > d->now) {
            if (!d->halted)
                adsp_run(&d->cpu, stop - d->now);
            d->now = stop;
        }
        if (d->ab_on && d->now >= d->ab_next) {
            autobuffer_event(d);
            d->ab_next += d->ab_period;
        }
    }
}

const int16_t *dcs_take_samples(dcs *d, size_t *n)
{
    *n = d->nout;
    d->nout = 0;
    return d->out;
}

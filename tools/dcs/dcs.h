/*
 * Williams/Midway DCS sound board, first revision (ADSP-2105, 8K words of
 * external program RAM), as used by the Wolf Unit.
 *
 *   data memory 0x0000-0x07FF  RAM
 *               0x0800-0x1FFF  upper 16 bits of program RAM 0x0800-0x1FFF
 *               0x2000-0x2FFF  4K-word window into the sound ROMs
 *               0x3000-0x33FF  (write) ROM bank select
 *               0x3400-0x37FF  host latches: read = command byte, write = reply
 *               0x3800-0x39FF  internal RAM
 *               0x3FE0-0x3FFF  ADSP-2105 control registers
 *
 * The ROMs are byte wide on a 16-bit bus (MAME loads them with
 * ROM_LOAD16_BYTE): ROM word n is 0xFF00 | byte n. On reset the DSP boots
 * from bank 0. Sound comes out of SPORT1 in autobuffer mode.
 */
#ifndef WWF_DCS_H
#define WWF_DCS_H

#include <stddef.h>
#include <stdint.h>

#include "adsp2105.h"

#define DCS_CLOCK 10000000
#define DCS_ROM_BYTES 0x100000
#define DCS_MAX_ROMS 8

typedef struct {
    adsp2105 cpu;
    uint8_t *rom;             /* nroms * 1 MB */
    int nroms;
    uint16_t dm_lo[0x800];
    uint16_t dm_hi[0x800];    /* 0x3800-0x3FFF */
    uint16_t bank;
    uint16_t ctrl[32];        /* 0x3FE0-0x3FFF */
    int halted;

    /* host interface: bit 11 = command latch empty, bit 10 = reply latch empty */
    uint16_t input, output, latch;

    /* SPORT1 transmit autobuffer */
    int ab_on, ab_ireg, ab_incs, ab_size;
    uint16_t ab_base;
    int64_t ab_period, ab_next;
    uint32_t sample_rate;

    /* output samples since the last dcs_take_samples */
    int16_t *out;
    size_t nout, cap;

    int64_t now;              /* cycles since power-up */
} dcs;

/* Loads the ROMs (u2, u3, ... in order) from memory. */
int dcs_init(dcs *d, const uint8_t *const roms[], int nroms);
void dcs_free(dcs *d);

/* Reset line from the host: 1 holds the board in reset, 0 lets it boot. */
void dcs_reset_line(dcs *d, int state);

/* Host side of the latches. */
void dcs_write(dcs *d, uint8_t byte);
uint16_t dcs_read(dcs *d);
uint16_t dcs_status(const dcs *d);

/* Runs the board for the given number of cycles. */
void dcs_run(dcs *d, int64_t cycles);

/* Returns the samples produced since the last call (mono, 16 bit). */
const int16_t *dcs_take_samples(dcs *d, size_t *n);

#endif

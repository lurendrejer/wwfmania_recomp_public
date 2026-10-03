/*
 * Analog Devices ADSP-2105 (ADSP-2100 family) interpreter.
 *
 * Used only by the DCS sound tools (tools/dcs): the game itself plays the
 * extracted sounds and does not emulate the sound board.
 *
 * Program memory is 16K x 24 bits inside the core; data memory goes through
 * the bus callbacks so the board can map ROM banks, latches and control
 * registers. One instruction takes one cycle.
 */
#ifndef WWF_ADSP2105_H
#define WWF_ADSP2105_H

#include <stdint.h>

/* Interrupts, numbered by their IMASK bit (IRQ2 has the highest priority). */
enum {
    ADSP_INT_TIMER = 0,
    ADSP_INT_SPORT1_RX = 1,   /* or IRQ0 */
    ADSP_INT_SPORT1_TX = 2,   /* or IRQ1 */
    ADSP_INT_IRQ2 = 5,
};

typedef struct {
    void *ctx;
    uint16_t (*dm_read)(void *ctx, uint16_t addr);
    void (*dm_write)(void *ctx, uint16_t addr, uint16_t value);
    void (*tx)(void *ctx, int port, uint16_t value);   /* write to TX0/TX1 */
} adsp_bus;

typedef struct {
    /* data registers in DREG order (AX0 AX1 MX0 MX1 AY0 AY1 MY0 MY1 SI SE
     * AR MR0 MR1 MR2 SR0 SR1) plus AF and MF; alt is the other bank */
    uint16_t d[18], alt[18];
    uint16_t i[8], l[8];
    int16_t m[8];
    uint16_t astat, mstat, imask, icntl, cntr, px, ifc;
    uint16_t rx[2], tx[2];
    int16_t sb;

    uint16_t pc;
    uint16_t pc_stack[16];
    int pc_sp;
    uint32_t loop_stack[4];   /* end address << 4 | termination condition */
    int loop_sp;
    uint16_t cntr_stack[4];
    int cntr_sp;
    uint16_t stat_stack[4][3];
    int stat_sp;
    uint32_t loop_end;        /* current loop end, or ADSP_NO_LOOP */
    int loop_cond;
    int idle;

    uint8_t irq_line[6], irq_latch[6];
    int flag_in, flag_out, fl[3];

    /* timer (memory mapped on the chip; the board forwards the registers) */
    uint16_t tscale, tcount, tperiod, tprescale;

    uint32_t pm[0x4000];
    uint64_t cycles;
    uint32_t bad_ops;         /* unknown opcodes executed (as NOPs) */
    adsp_bus bus;
} adsp2105;

#define ADSP_NO_LOOP 0xFFFFFFFFu

/* Resets the registers and stacks (program memory is kept); PC = 0. */
void adsp_reset(adsp2105 *a);

/* Sets an interrupt line (IRQ2) or pulses an internal one (SPORT, timer). */
void adsp_set_irq(adsp2105 *a, int which, int state);
void adsp_pulse_irq(adsp2105 *a, int which);

/* Runs for the given number of cycles. */
void adsp_run(adsp2105 *a, int64_t cycles);

/*
 * The ADSP-2101/2105 boot format: 4 bytes per program word (high, middle,
 * low, unused); byte 3 gives the length as (n + 1) * 8 words. Loads into
 * program memory from address 0. Returns the number of words.
 */
int adsp_boot(adsp2105 *a, const uint8_t *page);

#endif

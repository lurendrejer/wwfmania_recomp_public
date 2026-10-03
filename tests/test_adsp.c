/*
 * ADSP-2105 and DCS board tests.
 *
 *   test_adsp [romdir]
 *
 * Small hand-assembled programs check the instruction set. With the sound
 * ROMs (a directory or zip, as for dcsrip) the board boots, passes its own
 * ROM test and plays a sound whose output is pinned.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adsp2105.h"
#include "dcs.h"
#include "romset.h"

static int failures;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                                 \
        }                                                                               \
    } while (0)

/* ---- a bare CPU with 16K words of data RAM ---- */

static uint16_t dm[0x4000];
static int tx_count;
static uint16_t dm_read(void *ctx, uint16_t a) { (void)ctx; return dm[a]; }
static void dm_write(void *ctx, uint16_t a, uint16_t v) { (void)ctx; dm[a] = v; }
static void tx(void *ctx, int port, uint16_t v) { (void)ctx; (void)port; (void)v; tx_count++; }

static adsp2105 cpu;
static int pc;

static void setup(void)
{
    memset(&cpu, 0, sizeof cpu);
    memset(dm, 0, sizeof dm);
    cpu.bus.dm_read = dm_read;
    cpu.bus.dm_write = dm_write;
    cpu.bus.tx = tx;
    adsp_reset(&cpu);
    pc = 0x20;   /* leave room for the interrupt vectors */
    cpu.pc = 0x20;
}

static void emit(uint32_t op) { cpu.pm[pc++] = op; }

/* register numbers (DREG) */
enum { AX0, AX1, MX0, MX1, AY0, AY1, MY0, MY1, SI, SE, AR, MR0, MR1, MR2, SR0, SR1 };
#define COND_TRUE 15

static void ld(int reg, uint16_t v) { emit(0x400000u | (uint32_t)v << 4 | (uint32_t)reg); }
static void ld_reg(int group, int reg, uint16_t v) { emit(0x300000u | (uint32_t)group << 18 | (uint32_t)(v & 0x3FFF) << 4 | (uint32_t)reg); }
/* ALU/MAC, unconditional: amf 5 bits, yop 0-3, xop 0-7, z: result to AF/MF */
static void alu(int amf, int xop, int yop, int z) { emit(0x200000u | (uint32_t)z << 18 | (uint32_t)amf << 13 | (uint32_t)yop << 11 | (uint32_t)xop << 8 | COND_TRUE); }
static void shift_sf(int sf, int xop) { emit(0x0E0000u | (uint32_t)sf << 11 | (uint32_t)xop << 8 | COND_TRUE); }
static void idle(void) { emit(0x028000u); }

static void run_until_idle(void)
{
    for (int i = 0; i < 100000 && !cpu.idle; i++)
        adsp_run(&cpu, 1);
}

static void test_alu(void)
{
    /* flags of AR = AX0 + AY0 with overflow: AV and AN, no AC */
    setup();
    ld(AX0, 0x7FFF);
    ld(AY0, 0x0001);
    alu(0x13, 0, 0, 0);
    idle();
    run_until_idle();
    CHECK(cpu.d[AR] == 0x8000);
    CHECK((cpu.astat & 0x0F) == (0x02 | 0x04));       /* AN, AV */

    /* AR = AX0 - AY0 with a borrow: AC clear, AN set */
    setup();
    ld(AX0, 5);
    ld(AY0, 7);
    alu(0x17, 0, 0, 0);
    idle();
    run_until_idle();
    CHECK(cpu.d[AR] == 0xFFFE);
    CHECK((cpu.astat & 0x0F) == 0x02);

    /* ABS */
    setup();
    ld(AX0, 0x8001);
    alu(0x1F, 0, 0, 0);          /* AR = ABS AX0 */
    idle();
    run_until_idle();
    CHECK(cpu.d[AR] == 0x7FFF);
    CHECK(cpu.astat & 0x10);     /* AS: input was negative */
}

static void test_mac(void)
{
    /* fractional mode: 0.5 * 0.5 = 0.25 -> MR1 = 0x2000 */
    setup();
    ld(MX0, 0x4000);
    ld(MY0, 0x4000);
    alu(0x04, 0, 0, 0);          /* MR = MX0 * MY0 (SS) */
    idle();
    run_until_idle();
    CHECK(cpu.d[MR1] == 0x2000 && cpu.d[MR0] == 0 && cpu.d[MR2] == 0);

    /* MR = MR + X * Y (SS), with -1 * 0.5 */
    setup();
    ld(MX0, 0x8000);
    ld(MY0, 0x4000);
    alu(0x04, 0, 0, 0);
    alu(0x08, 0, 0, 0);          /* MR = MR + MX0 * MY0 */
    idle();
    run_until_idle();
    CHECK(cpu.d[MR1] == 0x8000 && cpu.d[MR0] == 0 && cpu.d[MR2] == 0xFFFF);   /* -1.0 */

    /* (RND): 0x0001 * 0x4000 -> MR0 = 0x8000 rounds to even */
    setup();
    ld(MX0, 0x0001);
    ld(MY0, 0x4000);
    alu(0x01, 0, 0, 0);
    idle();
    run_until_idle();
    CHECK(cpu.d[MR1] == 0x0000);
}

static void test_shifter(void)
{
    setup();
    ld(SI, 0x1234);
    ld(SE, 4);
    shift_sf(0x0, 0);            /* SR = LSHIFT SI (HI) */
    idle();
    run_until_idle();
    CHECK(cpu.d[SR1] == 0x2340 && cpu.d[SR0] == 0);

    setup();
    ld(SI, 0x8000);
    ld(SE, (uint16_t)-4);
    shift_sf(0x4, 0);            /* SR = ASHIFT SI (HI) by -4 */
    idle();
    run_until_idle();
    CHECK(cpu.d[SR1] == 0xF800 && cpu.d[SR0] == 0);

    setup();
    ld(SI, 0x0100);
    shift_sf(0xC, 0);            /* SE = EXP SI (HI) */
    idle();
    run_until_idle();
    CHECK((int16_t)cpu.d[SE] == -6);
}

static void test_loop_and_dag(void)
{
    /* CNTR = 5; DO L UNTIL CE; L: AR = AR + AY0 */
    setup();
    ld(AY0, 1);
    ld_reg(3, 5, 5);                                   /* CNTR = 5 */
    emit(0x140000u | (uint32_t)(pc + 1) << 4 | 14);    /* DO next UNTIL CE */
    emit(0x200000u | 0x13u << 13 | 0u << 11 | 2u << 8 | COND_TRUE);   /* AR = AR + AY0 */
    idle();
    run_until_idle();
    CHECK(cpu.d[AR] == 5);
    CHECK(cpu.loop_sp == 0 && cpu.pc_sp == 0 && cpu.cntr_sp == 0);

    /* circular buffer: I0 = 0x100, L0 = 3, M0 = 1; four writes wrap */
    setup();
    ld_reg(1, 0, 0x100);         /* I0 */
    ld_reg(1, 8, 3);             /* L0 */
    ld_reg(1, 4, 1);             /* M0 */
    for (int k = 1; k <= 4; k++)
        emit(0xA00000u | (uint32_t)k << 4);   /* DM(I0, M0) = k */
    idle();
    run_until_idle();
    CHECK(dm[0x100] == 4 && dm[0x101] == 2 && dm[0x102] == 3);
    CHECK(cpu.i[0] == 0x101);
}

static void test_interrupt(void)
{
    setup();
    /* vector 0x10 (SPORT1 transmit): DM(0x200) = 0x55; RTI */
    cpu.pm[0x10] = 0xA00000u | 0x55u << 4;             /* DM(I0, M0) = 0x55 */
    cpu.pm[0x11] = 0x0A0000u | 0x10u | COND_TRUE;      /* RTI */
    ld_reg(1, 0, 0x200);
    ld_reg(3, 3, 0x04);                                /* IMASK: SPORT1 TX */
    idle();
    emit(0x400000u | 0x77u << 4 | AX0);                /* after the wake-up */
    idle();
    run_until_idle();
    CHECK(cpu.idle);
    adsp_pulse_irq(&cpu, ADSP_INT_SPORT1_TX);
    adsp_run(&cpu, 1);
    CHECK(!cpu.idle);
    run_until_idle();
    CHECK(dm[0x200] == 0x55);
    CHECK(cpu.d[AX0] == 0x77);
    CHECK(cpu.imask == 0x04 && cpu.stat_sp == 0 && cpu.pc_sp == 0);
}

/* ---- the real board ---- */

static void test_board(const char *romdir)
{
    dcs_romset rs;
    char err[512];
    if (!dcs_romset_load(&rs, romdir, err, sizeof err)) {
        fprintf(stderr, "%s\n", err);
        failures++;
        return;
    }
    CHECK(rs.known);
    static dcs d;
    CHECK(dcs_init(&d, (const uint8_t *const *)rs.data, DCS_NROMS));
    dcs_romset_free(&rs);
    dcs_reset_line(&d, 1);
    dcs_reset_line(&d, 0);
    int replies[4], n = 0;
    size_t count;
    for (int ms = 0; ms < 4000; ms++) {
        dcs_run(&d, DCS_CLOCK / 1000);
        dcs_take_samples(&d, &count);
        if (!(dcs_status(&d) & 0x0400) && n < 4)
            replies[n++] = dcs_read(&d);
    }
    /* the board's own ROM checksum test passed: 0x79, then 1 */
    CHECK(n == 2 && replies[0] == 0x79 && replies[1] == 0x01);
    CHECK(d.sample_rate == 31250);
    CHECK(d.cpu.bad_ops == 0);

    /* "can anybody stop him!?" (code 3844): pin the decoded audio */
    const int code = 3844;
    dcs_write(&d, code >> 8);
    for (int i = 0; i < 100 && !(dcs_status(&d) & 0x0800); i++)
        dcs_run(&d, 100);
    dcs_write(&d, code & 0xFF);
    uint32_t crc = 0;
    size_t total = 0;
    static uint8_t bytes[31250 * 2 * 2];
    for (int ms = 0; ms < 1500; ms++) {
        dcs_run(&d, DCS_CLOCK / 1000);
        const int16_t *s = dcs_take_samples(&d, &count);
        for (size_t k = 0; k < count && total < sizeof bytes / 2; k++, total++) {
            bytes[total * 2] = (uint8_t)(s[k] & 0xFF);
            bytes[total * 2 + 1] = (uint8_t)((uint16_t)s[k] >> 8);
        }
    }
    crc = dcs_crc32(bytes, total * 2);
    printf("speech: %zu samples, crc %08X\n", total, crc);
    CHECK(total == 47040);
    CHECK(crc == 0x11CDDFFCu);   /* regression pin for this emulator, not a hardware capture */
    dcs_free(&d);
}

int main(int argc, char **argv)
{
    test_alu();
    test_mac();
    test_shifter();
    test_loop_and_dag();
    test_interrupt();
    if (argc > 1)
        test_board(argv[1]);
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("all ADSP tests passed\n");
    return 0;
}

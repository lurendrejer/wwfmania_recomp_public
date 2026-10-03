/*
 * ADSP-2105 interpreter. Instruction encodings and flag rules follow the
 * ADSP-2100 Family User's Manual; the few board-visible details that the
 * manual leaves open (boot format, interrupt latching) follow MAME's
 * adsp2100 core, which runs the DCS sound boards.
 */
#include "adsp2105.h"

#include <string.h>

enum { AX0, AX1, MX0, MX1, AY0, AY1, MY0, MY1, SI, SE, AR, MR0, MR1, MR2, SR0, SR1, AF, MF };

/* ASTAT */
#define AZ 0x01
#define AN 0x02
#define AV 0x04
#define AC 0x08
#define AS 0x10
#define AQ 0x20
#define MV 0x40
#define SS 0x80

/* MSTAT */
#define M_BANK 0x01
#define M_REVERSE 0x02
#define M_STICKYV 0x04
#define M_SATURATE 0x08
#define M_INTEGER 0x10
#define M_TIMER 0x20
#define M_GOMODE 0x40

static uint16_t sx8(unsigned v) { return (uint16_t)((v & 0x80) ? (v | 0xFF00u) : (v & 0xFFu)); }
static int32_t s16(uint16_t v) { return (v & 0x8000) ? (int32_t)v - 0x10000 : (int32_t)v; }

static int64_t sext40(int64_t v)
{
    uint64_t u = (uint64_t)v & 0xFFFFFFFFFFull;
    return (u & 0x8000000000ull) ? (int64_t)(u | 0xFFFFFF0000000000ull) : (int64_t)u;
}

/* ---- stacks --------------------------------------------------------------------- */

static void pc_push(adsp2105 *a, uint16_t v)
{
    if (a->pc_sp < 16)
        a->pc_stack[a->pc_sp++] = v;
}

static uint16_t pc_pop(adsp2105 *a)
{
    if (a->pc_sp > 0)
        a->pc_sp--;
    return a->pc_stack[a->pc_sp];
}

static uint16_t pc_top(const adsp2105 *a) { return a->pc_stack[a->pc_sp > 0 ? a->pc_sp - 1 : 0]; }

static void loop_set_top(adsp2105 *a)
{
    if (a->loop_sp > 0) {
        a->loop_end = a->loop_stack[a->loop_sp - 1] >> 4;
        a->loop_cond = (int)(a->loop_stack[a->loop_sp - 1] & 15);
    } else {
        a->loop_end = ADSP_NO_LOOP;
        a->loop_cond = 0;
    }
}

static void loop_push(adsp2105 *a, uint32_t v)
{
    if (a->loop_sp < 4)
        a->loop_stack[a->loop_sp++] = v;
    loop_set_top(a);
}

static void loop_pop(adsp2105 *a)
{
    if (a->loop_sp > 0)
        a->loop_sp--;
    loop_set_top(a);
}

static void cntr_push(adsp2105 *a)
{
    if (a->cntr_sp < 4)
        a->cntr_stack[a->cntr_sp++] = a->cntr;
}

static void cntr_pop(adsp2105 *a)
{
    if (a->cntr_sp > 0)
        a->cntr = a->cntr_stack[--a->cntr_sp];
}

static void set_mstat(adsp2105 *a, uint16_t v)
{
    v &= 0x7F;
    if ((v ^ a->mstat) & M_BANK) {
        uint16_t t[18];
        memcpy(t, a->d, sizeof t);
        memcpy(a->d, a->alt, sizeof t);
        memcpy(a->alt, t, sizeof t);
    }
    a->mstat = v;
}

static void stat_push(adsp2105 *a)
{
    if (a->stat_sp < 4) {
        a->stat_stack[a->stat_sp][0] = a->astat;
        a->stat_stack[a->stat_sp][1] = a->mstat;
        a->stat_stack[a->stat_sp][2] = a->imask;
        a->stat_sp++;
    }
}

static void stat_pop(adsp2105 *a)
{
    if (a->stat_sp > 0) {
        a->stat_sp--;
        a->astat = a->stat_stack[a->stat_sp][0];
        set_mstat(a, a->stat_stack[a->stat_sp][1]);
        a->imask = a->stat_stack[a->stat_sp][2];
    }
}

static uint16_t sstat(const adsp2105 *a)
{
    uint16_t s = 0;
    if (a->pc_sp == 0) s |= 0x01;
    if (a->pc_sp >= 16) s |= 0x02;
    if (a->cntr_sp == 0) s |= 0x04;
    if (a->cntr_sp >= 4) s |= 0x08;
    if (a->stat_sp == 0) s |= 0x10;
    if (a->stat_sp >= 4) s |= 0x20;
    if (a->loop_sp == 0) s |= 0x40;
    if (a->loop_sp >= 4) s |= 0x80;
    return s;
}

/* ---- interrupts ------------------------------------------------------------------ */

static unsigned pending_irqs(const adsp2105 *a)
{
    unsigned p = 0;
    for (int b = 0; b < 6; b++)
        if (a->irq_latch[b])
            p |= 1u << b;
    /* IRQ2 is level sensitive unless ICNTL bit 2 selects edge */
    if (!(a->icntl & 4) && a->irq_line[ADSP_INT_IRQ2])
        p |= 1u << ADSP_INT_IRQ2;
    return p & a->imask;
}

static void take_irq(adsp2105 *a)
{
    unsigned p = pending_irqs(a);
    if (!p)
        return;
    int b = 5;
    while (!(p & (1u << b)))
        b--;
    a->irq_latch[b] = 0;
    pc_push(a, a->pc);
    stat_push(a);
    a->pc = (uint16_t)(0x04 + (5 - b) * 4);
    a->idle = 0;
    if (a->icntl & 0x10)
        a->imask &= (uint16_t)~((2u << b) - 1);   /* nesting: mask this and lower */
    else
        a->imask = 0;
}

void adsp_set_irq(adsp2105 *a, int which, int state)
{
    if (state && !a->irq_line[which])
        a->irq_latch[which] = 1;
    a->irq_line[which] = (uint8_t)(state != 0);
}

void adsp_pulse_irq(adsp2105 *a, int which) { a->irq_latch[which] = 1; }

/* ---- registers ------------------------------------------------------------------- */

/* IFC: bits 0-5 clear and bits 6-11 force the interrupts in IMASK order. */
static void write_ifc(adsp2105 *a, uint16_t v)
{
    a->ifc = v;
    for (int k = 0; k < 6; k++) {
        if (v & (1u << k))
            a->irq_latch[k] = 0;
        if (v & (0x40u << k))
            a->irq_latch[k] = 1;
    }
}

static uint16_t rd_reg(adsp2105 *a, int g, int r)
{
    switch (g) {
    case 0:
        return a->d[r];
    case 1:
    case 2: {
        int k = (g - 1) * 4 + (r & 3);
        switch (r >> 2) {
        case 0: return a->i[k];
        case 1: return (uint16_t)a->m[k];
        case 2: return a->l[k];
        default: return 0;
        }
    }
    default:
        switch (r) {
        case 0: return a->astat;
        case 1: return a->mstat;
        case 2: return sstat(a);
        case 3: return a->imask;
        case 4: return a->icntl;
        case 5: return a->cntr;
        case 6: return (uint16_t)a->sb;
        case 7: return a->px;
        case 8: return a->rx[0];
        case 9: return a->tx[0];
        case 10: return a->rx[1];
        case 11: return a->tx[1];
        case 12: return a->ifc;
        case 13: return a->cntr;
        case 15: return pc_pop(a);
        default: return 0;
        }
    }
}

static void wr_reg(adsp2105 *a, int g, int r, uint16_t v)
{
    switch (g) {
    case 0:
        if (r == SE || r == MR2) {
            a->d[r] = sx8(v);
        } else if (r == MR1) {
            a->d[MR1] = v;
            a->d[MR2] = (v & 0x8000) ? 0xFFFF : 0;
        } else {
            a->d[r] = v;
        }
        return;
    case 1:
    case 2: {
        int k = (g - 1) * 4 + (r & 3);
        switch (r >> 2) {
        case 0: a->i[k] = v & 0x3FFF; break;
        case 1: a->m[k] = (int16_t)((v & 0x2000) ? (int32_t)(v & 0x3FFF) - 0x4000 : (int32_t)(v & 0x3FFF)); break;
        case 2: a->l[k] = v & 0x3FFF; break;
        default: break;
        }
        return;
    }
    default:
        switch (r) {
        case 0: a->astat = v & 0xFF; break;
        case 1: set_mstat(a, v); break;
        case 3: a->imask = v & 0x3F; break;
        case 4: a->icntl = v & 0x1F; break;
        case 5: cntr_push(a); a->cntr = v & 0x3FFF; break;
        case 6: a->sb = (int16_t)((v & 0x10) ? (int32_t)(v & 0x1F) - 0x20 : (int32_t)(v & 0x1F)); break;
        case 7: a->px = v & 0xFF; break;
        case 8: a->rx[0] = v; break;
        case 9: a->tx[0] = v; if (a->bus.tx) a->bus.tx(a->bus.ctx, 0, v); break;
        case 10: a->rx[1] = v; break;
        case 11: a->tx[1] = v; if (a->bus.tx) a->bus.tx(a->bus.ctx, 1, v); break;
        case 12: write_ifc(a, v); break;
        case 13: a->cntr = v & 0x3FFF; break;   /* OWRCNTR: no push */
        case 15: pc_push(a, v & 0x3FFF); break;
        default: break;
        }
        return;
    }
}

/* ---- conditions ------------------------------------------------------------------ */

static int cond(adsp2105 *a, int c)
{
    unsigned s = a->astat;
    int r;
    switch (c >> 1) {
    case 0: r = (s & AZ) != 0; break;                                    /* EQ */
    case 1: r = !((((s & AN) != 0) ^ ((s & AV) != 0)) || (s & AZ)); break; /* GT */
    case 2: r = ((s & AN) != 0) ^ ((s & AV) != 0); break;                 /* LT */
    case 3: r = (s & AV) != 0; break;
    case 4: r = (s & AC) != 0; break;
    case 5: r = (s & AS) != 0; break;                                    /* NEG */
    case 6: r = (s & MV) != 0; break;
    default:
        if (c == 15)
            return 1;
        /* NOT CE: decrements the counter; on expiry the count stack pops */
        a->cntr = (uint16_t)((a->cntr - 1) & 0x3FFF);
        if (a->cntr != 0 && a->cntr != 0x3FFF)
            return 1;
        cntr_pop(a);
        return 0;
    }
    return (c & 1) ? !r : r;
}

/* ---- address generators --------------------------------------------------------- */

static void modify(adsp2105 *a, int ir, int mr)
{
    int32_t i = a->i[ir];
    uint32_t l = a->l[ir];
    int32_t ni = (i + a->m[mr]) & 0x3FFF;
    if (l) {
        uint32_t p = 1;
        while (p < l)
            p <<= 1;
        int32_t base = (int32_t)((uint32_t)i & ~(p - 1));
        if (ni < base)
            ni += (int32_t)l;
        else if (ni >= base + (int32_t)l)
            ni -= (int32_t)l;
    }
    a->i[ir] = (uint16_t)(ni & 0x3FFF);
}

static uint16_t dag(adsp2105 *a, int ir, int mr)
{
    uint16_t addr = a->i[ir];
    if (ir < 4 && (a->mstat & M_REVERSE)) {
        uint16_t r = 0;
        for (int b = 0; b < 14; b++)
            if (addr & (1u << b))
                r |= (uint16_t)(1u << (13 - b));
        addr = r;
    }
    modify(a, ir, mr);
    return addr;
}

static uint16_t dm_rd(adsp2105 *a, uint16_t addr) { return a->bus.dm_read(a->bus.ctx, addr & 0x3FFF); }
static void dm_wr(adsp2105 *a, uint16_t addr, uint16_t v) { a->bus.dm_write(a->bus.ctx, addr & 0x3FFF, v); }

static uint16_t pm_rd(adsp2105 *a, uint16_t addr)
{
    uint32_t w = a->pm[addr & 0x3FFF];
    a->px = (uint16_t)(w & 0xFF);
    return (uint16_t)(w >> 8);
}

static void pm_wr(adsp2105 *a, uint16_t addr, uint16_t v)
{
    a->pm[addr & 0x3FFF] = ((uint32_t)v << 8) | (a->px & 0xFF);
}

/* ---- ALU ------------------------------------------------------------------------- */

static const uint8_t alu_xreg[8] = {AX0, AX1, AR, MR0, MR1, MR2, SR0, SR1};
static const uint8_t mac_xreg[8] = {MX0, MX1, AR, MR0, MR1, MR2, SR0, SR1};
static const uint8_t shift_xreg[8] = {SI, SI, AR, MR0, MR1, MR2, SR0, SR1};

static uint16_t alu_y(const adsp2105 *a, int y)
{
    static const uint8_t reg[3] = {AY0, AY1, AF};
    return y == 3 ? 0 : a->d[reg[y]];
}

static uint16_t mac_y(const adsp2105 *a, int y)
{
    static const uint8_t reg[3] = {MY0, MY1, MF};
    return y == 3 ? 0 : a->d[reg[y]];
}

static void alu_op(adsp2105 *a, uint32_t op, int z)
{
    uint32_t x = a->d[alu_xreg[(op >> 8) & 7]], y = alu_y(a, (int)(op >> 11) & 3);
    uint32_t c = (a->astat & AC) ? 1 : 0, res = 0, ox = 0, oy = 0, cin = 0;
    int arith = 0;
    unsigned flags = a->astat & (AS | AQ | MV | SS);
    switch ((op >> 13) & 15) {
    case 0x0: res = y; break;
    case 0x1: ox = y; oy = 0; cin = 1; arith = 1; break;
    case 0x2: ox = x; oy = y; cin = c; arith = 1; break;
    case 0x3: ox = x; oy = y; cin = 0; arith = 1; break;
    case 0x4: res = ~y & 0xFFFF; break;
    case 0x5: ox = 0; oy = ~y & 0xFFFF; cin = 1; arith = 1; break;
    case 0x6: ox = x; oy = ~y & 0xFFFF; cin = c; arith = 1; break;
    case 0x7: ox = x; oy = ~y & 0xFFFF; cin = 1; arith = 1; break;
    case 0x8: /* Y - 1: AC only when Y = 0, as in MAME */
        res = (y - 1) & 0xFFFF;
        if (y == 0x8000) flags |= AV;
        if (y == 0) flags |= AC;
        break;
    case 0x9: ox = y; oy = ~x & 0xFFFF; cin = 1; arith = 1; break;
    case 0xA: ox = y; oy = ~x & 0xFFFF; cin = c; arith = 1; break;
    case 0xB: res = ~x & 0xFFFF; break;
    case 0xC: res = x & y; break;
    case 0xD: res = x | y; break;
    case 0xE: res = x ^ y; break;
    default: /* ABS X */
        res = (x & 0x8000) ? ((0x10000 - x) & 0xFFFF) : x;
        flags &= ~(unsigned)AS;
        if (x & 0x8000)
            flags |= AS;
        if (x == 0x8000)
            flags |= AV;
        break;
    }
    if (arith) {
        uint32_t s = ox + oy + cin;
        res = s & 0xFFFF;
        if (s & 0x10000)
            flags |= AC;
        if (~(ox ^ oy) & (ox ^ res) & 0x8000)
            flags |= AV;
    }
    if (res == 0)
        flags |= AZ;
    if (res & 0x8000)
        flags |= AN;
    if ((a->mstat & M_STICKYV) && (a->astat & AV))
        flags |= AV;
    int overflow = (flags & AV) != 0;
    a->astat = (uint16_t)flags;
    if (z) {
        a->d[AF] = (uint16_t)res;
    } else {
        if ((a->mstat & M_SATURATE) && overflow)
            res = (flags & AC) ? 0x8000 : 0x7FFF;
        a->d[AR] = (uint16_t)res;
    }
}

/* ---- MAC ------------------------------------------------------------------------- */

static int64_t get_mr(const adsp2105 *a)
{
    return sext40(((int64_t)(a->d[MR2] & 0xFF) << 32) | ((int64_t)a->d[MR1] << 16) | a->d[MR0]);
}

static void set_mr(adsp2105 *a, int64_t v)
{
    uint64_t u = (uint64_t)v;
    a->d[MR0] = (uint16_t)(u & 0xFFFF);
    a->d[MR1] = (uint16_t)((u >> 16) & 0xFFFF);
    a->d[MR2] = sx8((unsigned)((u >> 32) & 0xFF));
}

static void mac_op(adsp2105 *a, uint32_t op, int z)
{
    int f = (int)(op >> 13) & 15;
    if (f == 0)
        return;
    uint16_t x = a->d[mac_xreg[(op >> 8) & 7]], y = mac_y(a, (int)(op >> 11) & 3);
    int64_t xs = s16(x), ys = s16(y), xu = x, yu = y;
    int64_t k = (a->mstat & M_INTEGER) ? 1 : 2;
    int64_t p, res, mr = get_mr(a);
    int rnd = 0;
    switch (f & 3) {
    case 0: p = xs * ys; break;
    case 1: p = xs * yu; break;
    case 2: p = xu * ys; break;
    default: p = xu * yu; break;
    }
    if (f < 4)
        p = xs * ys;   /* the RND forms are signed x signed */
    p *= k;
    switch (f) {
    case 1: res = p; rnd = 1; break;
    case 2: res = mr + p; rnd = 1; break;
    case 3: res = mr - p; rnd = 1; break;
    default:
        if (f < 8) res = p;
        else if (f < 12) res = mr + p;
        else res = mr - p;
        break;
    }
    if (rnd) {
        int64_t low = res & 0xFFFF;
        res += 0x8000;
        if (low == 0x8000)
            res &= ~(int64_t)0x10000;
    }
    res = sext40(res);
    if (z) {
        a->d[MF] = (uint16_t)(((uint64_t)res >> 16) & 0xFFFF);
    } else {
        set_mr(a, res);
        a->astat &= (uint16_t)~MV;
        if (res < -(int64_t)0x80000000 || res > (int64_t)0x7FFFFFFF)
            a->astat |= MV;
    }
}

static void alumac(adsp2105 *a, uint32_t op, int z)
{
    if (op & 0x20000)
        alu_op(a, op, z);
    else
        mac_op(a, op, z);
}

/* ---- shifter ----------------------------------------------------------------------- */

static uint32_t lsh(uint32_t v, int sc)
{
    if (sc >= 32 || sc <= -32)
        return 0;
    return sc >= 0 ? v << sc : v >> -sc;
}

static uint32_t ash(uint32_t v, int sc)
{
    int neg = (v & 0x80000000u) != 0;
    if (sc >= 32)
        return 0;
    if (sc >= 0)
        return v << sc;
    if (sc <= -32)
        return neg ? 0xFFFFFFFFu : 0;
    uint32_t r = v >> -sc;
    if (neg)
        r |= ~(0xFFFFFFFFu >> -sc);
    return r;
}

/* Number of leading bits equal to the sign bit, minus one (0..15). */
static int redundant_bits(uint16_t x)
{
    int n = 0;
    unsigned s = x >> 15;
    for (int b = 14; b >= 0 && ((x >> b) & 1) == s; b--)
        n++;
    return n;
}

static void set_sr(adsp2105 *a, uint32_t r, int or_mode)
{
    if (or_mode)
        r |= (uint32_t)a->d[SR1] << 16 | a->d[SR0];
    a->d[SR0] = (uint16_t)r;
    a->d[SR1] = (uint16_t)(r >> 16);
}

static void shift_op(adsp2105 *a, int sf, uint16_t x, int sc)
{
    int orm = sf & 1;
    switch (sf) {
    case 0x0: case 0x1: set_sr(a, lsh((uint32_t)x << 16, sc), orm); break;
    case 0x2: case 0x3: set_sr(a, lsh(x, sc), orm); break;
    case 0x4: case 0x5: set_sr(a, ash((uint32_t)x << 16, sc), orm); break;
    case 0x6: case 0x7: set_sr(a, ash((uint32_t)(int32_t)s16(x), sc), orm); break;
    case 0x8: case 0x9: {
        int n = -(int)(int16_t)a->d[SE];
        uint32_t r = lsh((uint32_t)x << 16, n);
        if (n < 0 && (a->astat & AC))   /* overflowed input: AC is the true sign */
            r |= n <= -32 ? 0xFFFFFFFFu : ~(0xFFFFFFFFu >> -n);
        set_sr(a, r, orm);
        break;
    }
    case 0xA: case 0xB: set_sr(a, lsh(x, -(int)(int16_t)a->d[SE]), orm); break;
    case 0xC: /* EXP (HI) */
    case 0xD: /* EXP (HIX) */
        if (sf == 0xD && (a->astat & AV)) {
            a->d[SE] = 1;
            a->astat = (uint16_t)((a->astat & ~SS) | ((x & 0x8000) ? 0 : SS));
        } else {
            a->d[SE] = (uint16_t)(-redundant_bits(x));
            a->astat = (uint16_t)((a->astat & ~SS) | ((x & 0x8000) ? SS : 0));
        }
        break;
    case 0xE: /* EXP (LO) */
        if ((int16_t)a->d[SE] == -15) {
            unsigned s = (a->astat & SS) ? 1 : 0;
            int n = 0;
            for (int b = 15; b >= 0 && ((x >> b) & 1u) == s; b--)
                n++;
            a->d[SE] = (uint16_t)(-15 - n);
        }
        break;
    default: { /* EXPADJ */
        int e = -redundant_bits(x);
        if (e > a->sb)
            a->sb = (int16_t)e;
        break;
    }
    }
}

static void shift_reg(adsp2105 *a, uint32_t op)
{
    shift_op(a, (int)(op >> 11) & 15, a->d[shift_xreg[(op >> 8) & 7]], (int)(int16_t)a->d[SE]);
}

/* ---- execution ---------------------------------------------------------------------- */

static void exec(adsp2105 *a, uint32_t op)
{
    uint16_t v;
    int ir, mr;
    switch (op >> 16) {
    case 0x00:
        break;
    case 0x02:
        if (op & 0x8000) {
            a->idle = 1;
        } else if (cond(a, (int)op & 15)) {
            if (op & 0x020) a->flag_out = 0;
            if (op & 0x010) a->flag_out ^= 1;
            if (op & 0x080) a->fl[0] = 0;
            if (op & 0x040) a->fl[0] ^= 1;
            if (op & 0x200) a->fl[1] = 0;
            if (op & 0x100) a->fl[1] ^= 1;
            if (op & 0x800) a->fl[2] = 0;
            if (op & 0x400) a->fl[2] ^= 1;
        }
        break;
    case 0x03:
        if ((op & 2) ? a->flag_in : !a->flag_in) {
            if (op & 1)
                pc_push(a, a->pc);
            a->pc = (uint16_t)(((op >> 4) & 0x0FFF) | ((op << 10) & 0x3000));
        }
        break;
    case 0x04:
        if (op & 0x10) pc_pop(a);
        if (op & 0x08) loop_pop(a);
        if (op & 0x04) cntr_pop(a);
        if (op & 0x02) {
            if (op & 0x01) stat_pop(a);
            else stat_push(a);
        }
        break;
    case 0x05: /* SAT MR */
        if (a->astat & MV)
            set_mr(a, (a->d[MR2] & 0x80) ? -(int64_t)0x80000000 : (int64_t)0x7FFFFFFF);
        break;
    case 0x06: { /* DIVS */
        uint16_t y = alu_y(a, (int)(op >> 11) & 3), x = a->d[alu_xreg[(op >> 8) & 7]];
        unsigned q = ((unsigned)(x ^ y) >> 15) & 1;
        a->astat = (uint16_t)((a->astat & ~AQ) | (q ? AQ : 0));
        a->d[AF] = (uint16_t)((y << 1) | (a->d[AY0] >> 15));
        a->d[AY0] = (uint16_t)((a->d[AY0] << 1) | q);
        break;
    }
    case 0x07: { /* DIVQ */
        uint16_t x = a->d[alu_xreg[(op >> 8) & 7]];
        uint16_t r = (a->astat & AQ) ? (uint16_t)(a->d[AF] + x) : (uint16_t)(a->d[AF] - x);
        unsigned q = ((unsigned)(r ^ x) >> 15) & 1;
        a->astat = (uint16_t)((a->astat & ~AQ) | (q ? AQ : 0));
        a->d[AF] = (uint16_t)((r << 1) | (a->d[AY0] >> 15));
        a->d[AY0] = (uint16_t)((a->d[AY0] << 1) | (q ^ 1));
        break;
    }
    case 0x09: { /* MODIFY (I, M) */
        int g = (op & 0x10) ? 4 : 0;
        modify(a, g + (int)((op >> 2) & 3), g + (int)(op & 3));
        break;
    }
    case 0x0A: /* RTS / RTI */
        if (cond(a, (int)op & 15)) {
            a->pc = pc_pop(a);
            if (op & 0x10)
                stat_pop(a);
        }
        break;
    case 0x0B: /* JUMP / CALL (I4..I7) */
        if (cond(a, (int)op & 15)) {
            if (op & 0x10)
                pc_push(a, a->pc);
            a->pc = a->i[4 + ((op >> 6) & 3)] & 0x3FFF;
        }
        break;
    case 0x0C: { /* ENA / DIS mode */
        uint16_t t = a->mstat;
        if (op & 0x000008) t = (uint16_t)((t & ~M_GOMODE) | ((op << 4) & M_GOMODE));
        if (op & 0x000020) t = (uint16_t)((t & ~M_BANK) | ((op >> 4) & M_BANK));
        if (op & 0x000080) t = (uint16_t)((t & ~M_REVERSE) | ((op >> 5) & M_REVERSE));
        if (op & 0x000200) t = (uint16_t)((t & ~M_STICKYV) | ((op >> 6) & M_STICKYV));
        if (op & 0x000800) t = (uint16_t)((t & ~M_SATURATE) | ((op >> 7) & M_SATURATE));
        if (op & 0x002000) t = (uint16_t)((t & ~M_INTEGER) | ((op >> 8) & M_INTEGER));
        if (op & 0x008000) t = (uint16_t)((t & ~M_TIMER) | ((op >> 9) & M_TIMER));
        set_mstat(a, t);
        break;
    }
    case 0x0D: /* register move */
        v = rd_reg(a, (int)(op >> 8) & 3, (int)op & 15);
        wr_reg(a, (int)(op >> 10) & 3, (int)(op >> 4) & 15, v);
        break;
    case 0x0E: /* conditional shift */
        if (cond(a, (int)op & 15))
            shift_reg(a, op);
        break;
    case 0x0F: /* shift immediate */
        shift_op(a, (int)(op >> 11) & 15, a->d[shift_xreg[(op >> 8) & 7]], (int)(int8_t)(op & 0xFF));
        break;
    case 0x10: /* shift + register move */
        v = a->d[op & 15];
        shift_reg(a, op);
        wr_reg(a, 0, (int)(op >> 4) & 15, v);
        break;
    case 0x11: /* shift + PM (I4..I7) */
        ir = 4 + (int)((op >> 2) & 3);
        mr = 4 + (int)(op & 3);
        if (op & 0x8000) {
            v = a->d[(op >> 4) & 15];
            shift_reg(a, op);
            pm_wr(a, dag(a, ir, mr), v);
        } else {
            v = pm_rd(a, dag(a, ir, mr));
            shift_reg(a, op);
            wr_reg(a, 0, (int)(op >> 4) & 15, v);
        }
        break;
    case 0x12:
    case 0x13: /* shift + DM */
        ir = ((op & 0x10000) ? 4 : 0) + (int)((op >> 2) & 3);
        mr = ((op & 0x10000) ? 4 : 0) + (int)(op & 3);
        if (op & 0x8000) {
            v = a->d[(op >> 4) & 15];
            shift_reg(a, op);
            dm_wr(a, dag(a, ir, mr), v);
        } else {
            v = dm_rd(a, dag(a, ir, mr));
            shift_reg(a, op);
            wr_reg(a, 0, (int)(op >> 4) & 15, v);
        }
        break;
    case 0x14: case 0x15: case 0x16: case 0x17: /* DO addr UNTIL cond */
        loop_push(a, op & 0x3FFFF);
        pc_push(a, a->pc);
        break;
    case 0x18: case 0x19: case 0x1A: case 0x1B: /* JUMP */
        if (cond(a, (int)op & 15))
            a->pc = (uint16_t)((op >> 4) & 0x3FFF);
        break;
    case 0x1C: case 0x1D: case 0x1E: case 0x1F: /* CALL */
        if (cond(a, (int)op & 15)) {
            pc_push(a, a->pc);
            a->pc = (uint16_t)((op >> 4) & 0x3FFF);
        }
        break;
    case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
        if (cond(a, (int)op & 15))
            alumac(a, op, (op >> 18) & 1);
        break;
    case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E: case 0x2F:
        v = a->d[op & 15];
        alumac(a, op, (op >> 18) & 1);
        wr_reg(a, 0, (int)(op >> 4) & 15, v);
        break;
    case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: case 0x36: case 0x37:
    case 0x38: case 0x39: case 0x3A: case 0x3B: case 0x3C: case 0x3D: case 0x3E: case 0x3F:
        v = (uint16_t)((op >> 4) & 0x3FFF);   /* sign-extended 14-bit value */
        wr_reg(a, (int)(op >> 18) & 3, (int)op & 15, (v & 0x2000) ? (uint16_t)(v | 0xC000) : v);
        break;
    case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
    case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F:
        wr_reg(a, 0, (int)op & 15, (uint16_t)((op >> 4) & 0xFFFF));
        break;
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
    case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
        /* ALU/MAC + PM read/write */
        ir = 4 + (int)((op >> 2) & 3);
        mr = 4 + (int)(op & 3);
        if (op & 0x80000) {
            v = a->d[(op >> 4) & 15];
            alumac(a, op, (op >> 18) & 1);
            pm_wr(a, dag(a, ir, mr), v);
        } else {
            v = pm_rd(a, dag(a, ir, mr));
            alumac(a, op, (op >> 18) & 1);
            wr_reg(a, 0, (int)(op >> 4) & 15, v);
        }
        break;
    case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x66: case 0x67:
    case 0x68: case 0x69: case 0x6A: case 0x6B: case 0x6C: case 0x6D: case 0x6E: case 0x6F:
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F:
        /* ALU/MAC + DM read/write */
        ir = ((op & 0x100000) ? 4 : 0) + (int)((op >> 2) & 3);
        mr = ((op & 0x100000) ? 4 : 0) + (int)(op & 3);
        if (op & 0x80000) {
            v = a->d[(op >> 4) & 15];
            alumac(a, op, (op >> 18) & 1);
            dm_wr(a, dag(a, ir, mr), v);
        } else {
            v = dm_rd(a, dag(a, ir, mr));
            alumac(a, op, (op >> 18) & 1);
            wr_reg(a, 0, (int)(op >> 4) & 15, v);
        }
        break;
    case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86: case 0x87:
    case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8C: case 0x8D: case 0x8E: case 0x8F:
        /* reg = DM(addr) */
        wr_reg(a, (int)(op >> 18) & 3, (int)op & 15, dm_rd(a, (uint16_t)((op >> 4) & 0x3FFF)));
        break;
    case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
    case 0x98: case 0x99: case 0x9A: case 0x9B: case 0x9C: case 0x9D: case 0x9E: case 0x9F:
        /* DM(addr) = reg */
        dm_wr(a, (uint16_t)((op >> 4) & 0x3FFF), rd_reg(a, (int)(op >> 18) & 3, (int)op & 15));
        break;
    case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: case 0xA5: case 0xA6: case 0xA7:
    case 0xA8: case 0xA9: case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF:
    case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
    case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
        /* DM(I, M) = data */
        ir = ((op & 0x100000) ? 4 : 0) + (int)((op >> 2) & 3);
        mr = ((op & 0x100000) ? 4 : 0) + (int)(op & 3);
        dm_wr(a, dag(a, ir, mr), (uint16_t)((op >> 4) & 0xFFFF));
        break;
    default:
        if (op >= 0xC00000) { /* ALU/MAC + DM and PM reads */
            uint16_t dv = dm_rd(a, dag(a, (int)(op >> 2) & 3, (int)op & 3));
            uint16_t pv = pm_rd(a, dag(a, 4 + (int)((op >> 6) & 3), 4 + (int)((op >> 4) & 3)));
            alumac(a, op, 0);
            wr_reg(a, 0, (int)(op >> 18) & 3, dv);
            wr_reg(a, 0, 4 + (int)((op >> 20) & 3), pv);
        } else {
            a->bad_ops++;
        }
        break;
    }
}

/* ---- timer and main loop ---------------------------------------------------------- */

static void timer_advance(adsp2105 *a, int64_t n)
{
    while (n > 0) {
        if ((int64_t)a->tprescale >= n) {
            a->tprescale = (uint16_t)(a->tprescale - n);
            return;
        }
        n -= (int64_t)a->tprescale + 1;
        a->tprescale = a->tscale;
        if (a->tcount == 0) {
            a->irq_latch[ADSP_INT_TIMER] = 1;
            a->tcount = a->tperiod;
        } else {
            a->tcount--;
        }
    }
}

static int64_t timer_cycles_to_fire(const adsp2105 *a)
{
    return (int64_t)a->tprescale + 1 + (int64_t)a->tcount * ((int64_t)a->tscale + 1);
}

void adsp_run(adsp2105 *a, int64_t cycles)
{
    int64_t done = 0;
    while (done < cycles) {
        if (pending_irqs(a))
            take_irq(a);
        if (a->idle) {
            int64_t n = cycles - done;
            if (a->mstat & M_TIMER) {
                int64_t t = timer_cycles_to_fire(a);
                if (t < n)
                    n = t;
                timer_advance(a, n);
            }
            done += n;
            a->cycles += (uint64_t)n;
            continue;
        }
        uint16_t pc = a->pc;
        uint32_t op = a->pm[pc];
        if (pc != a->loop_end) {
            a->pc = (uint16_t)((pc + 1) & 0x3FFF);
        } else if (cond(a, a->loop_cond)) {
            a->pc = pc_top(a);
        } else {
            loop_pop(a);
            pc_pop(a);
            a->pc = (uint16_t)((pc + 1) & 0x3FFF);
        }
        exec(a, op);
        done++;
        a->cycles++;
        if (a->mstat & M_TIMER)
            timer_advance(a, 1);
    }
}

void adsp_reset(adsp2105 *a)
{
    uint32_t pm_keep[0x4000];
    adsp_bus bus = a->bus;
    uint64_t cycles = a->cycles;
    memcpy(pm_keep, a->pm, sizeof pm_keep);
    memset(a, 0, sizeof *a);
    memcpy(a->pm, pm_keep, sizeof pm_keep);
    a->bus = bus;
    a->cycles = cycles;
    a->loop_end = ADSP_NO_LOOP;
    a->sb = -16;
}

int adsp_boot(adsp2105 *a, const uint8_t *page)
{
    int n = (page[3] + 1) * 8;
    for (int i = 0; i < n; i++)
        a->pm[i] = (uint32_t)page[i * 4] << 16 | (uint32_t)page[i * 4 + 1] << 8 | page[i * 4 + 2];
    return n;
}

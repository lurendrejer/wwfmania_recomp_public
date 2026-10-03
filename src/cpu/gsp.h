/*
 * TMS34010 (GSP) runtime for the recompiled game code.
 *
 * The translator (tools/gsp/gsp2c.py) turns every instruction into C that
 * operates on a gsp_t. Registers, status flags, field sizes and the stack
 * behave like the real CPU; memory is bit-addressed, as on the 34010.
 *
 * Control flow: each module becomes one function containing a switch on
 * c->pc with a case per entry point. Direct jumps inside a module are
 * gotos; everything else (calls to other modules, returns, indirect
 * jumps, interrupts) goes through gsp_run's dispatcher. Return addresses
 * live on the emulated stack, so the game's multitasker, which switches
 * stacks and rewrites return addresses, works unchanged.
 */
#ifndef WWF_GSP_H
#define WWF_GSP_H

#include <stdint.h>

/* Register indices: A0-A14 = 0-14, SP = 15 (shared by both files), B0-B14 = 16-30. */
#define GSP_SP 15
#define GSP_B(n) (16 + (n))

/* Interrupt bits in INTENB/INTPEND. */
#define GSP_INT_X1 (1u << 1)
#define GSP_INT_X2 (1u << 2)
#define GSP_INT_HI (1u << 9)
#define GSP_INT_DI (1u << 10)
#define GSP_INT_WV (1u << 11)

/* ST bits other than flags and field sizes. */
#define GSP_ST_IE (1u << 21)
#define GSP_ST_PBX (1u << 25)

struct gsp_hw;

typedef struct gsp {
    uint32_t r[32];
    uint32_t pc;
    uint8_t n, c, z, v;       /* status flags, 0 or 1 */
    uint8_t fs[2];            /* field sizes 1..32 */
    uint8_t fe[2];            /* field sign-extend flags */
    uint32_t st_rest;         /* IE, PBX and any other ST bits */

    int32_t budget;           /* instructions left in the current time slice */
    uint64_t executed;        /* total instructions (approximate) */
    int stop;                 /* nonzero: leave gsp_run */
    const char *fault;        /* set on an unrecoverable error */
    uint32_t fault_pc;

    uint16_t io[32];          /* GSP I/O registers at 0xC0000000 + i*16 */
    struct gsp_hw *hw;

    /* last dispatch targets, for fault reports */
    uint32_t hist[64];
    unsigned hist_pos;

    /* optional: called at every dispatch (debugging/profiling) */
    void (*trace)(struct gsp *c, void *user);
    void *trace_user;
} gsp_t;

/* ---- memory (implemented by the hardware layer, src/wolf/wolf.c) ---------- */

uint32_t gsp_read(gsp_t *c, uint32_t addr, int bits);            /* zero-extended */
void gsp_write(gsp_t *c, uint32_t addr, int bits, uint32_t value);

/* Patch point in the recompiled display code (tools/gsp/gsp2c.py PATCHES): an
 * object is about to be queued for DMA, A0 = object, A10 = destination Y:X,
 * A9 = HEIGHT:WIDTH. May change A10. */
void gsp_hud_shift(gsp_t *c);

/* Fast path for work RAM (0x01000000..0x013FFFFF, bit addresses). */
extern uint8_t *gsp_ram;
#define GSP_RAM_BASE 0x01000000u
#define GSP_RAM_BITS 0x00400000u

static inline uint32_t gsp_rd(gsp_t *c, uint32_t addr, int bits)
{
    uint32_t off = addr - GSP_RAM_BASE;
    if (off < GSP_RAM_BITS && (off & 7) == 0) {
        const uint8_t *p = gsp_ram + (off >> 3);
        if (bits == 16)
            return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
        if (bits == 32)
            return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
                   ((uint32_t)p[3] << 24);
        if (bits == 8)
            return p[0];
    }
    return gsp_read(c, addr, bits);
}

static inline void gsp_wr(gsp_t *c, uint32_t addr, int bits, uint32_t v)
{
    uint32_t off = addr - GSP_RAM_BASE;
    if (off < GSP_RAM_BITS && (off & 7) == 0) {
        uint8_t *p = gsp_ram + (off >> 3);
        if (bits == 16) {
            p[0] = (uint8_t)v;
            p[1] = (uint8_t)(v >> 8);
            return;
        }
        if (bits == 32) {
            p[0] = (uint8_t)v;
            p[1] = (uint8_t)(v >> 8);
            p[2] = (uint8_t)(v >> 16);
            p[3] = (uint8_t)(v >> 24);
            return;
        }
        if (bits == 8) {
            p[0] = (uint8_t)v;
            return;
        }
    }
    gsp_write(c, addr, bits, v);
}

/* ---- field helpers ----------------------------------------------------------- */

static inline uint32_t gsp_sext(uint32_t v, int bits)
{
    if (bits >= 32)
        return v;
    uint32_t m = 1u << (bits - 1);
    v &= (m << 1) - 1;
    return (v ^ m) - m;
}

static inline uint32_t gsp_zext(uint32_t v, int bits)
{
    return bits >= 32 ? v : v & ((1u << bits) - 1);
}

/* Load a field using field size/extension F. */
static inline uint32_t gsp_ld(gsp_t *c, uint32_t addr, int f)
{
    uint32_t v = gsp_rd(c, addr, c->fs[f]);
    return c->fe[f] ? gsp_sext(v, c->fs[f]) : v;
}

static inline void gsp_st(gsp_t *c, uint32_t addr, int f, uint32_t v)
{
    gsp_wr(c, addr, c->fs[f], gsp_zext(v, c->fs[f]));
}

/* ---- flags --------------------------------------------------------------------- */

#define GSP_NZ(c, x) ((c)->n = (uint8_t)((uint32_t)(x) >> 31), (c)->z = (uint8_t)((x) == 0))

static inline uint32_t gsp_add(gsp_t *c, uint32_t a, uint32_t b, uint32_t carry_in)
{
    uint64_t s = (uint64_t)a + b + carry_in;
    uint32_t r = (uint32_t)s;
    c->c = (uint8_t)(s >> 32);
    c->v = (uint8_t)((~(a ^ b) & (a ^ r)) >> 31);
    GSP_NZ(c, r);
    return r;
}

/* a - b - borrow_in; C is the borrow. */
static inline uint32_t gsp_sub(gsp_t *c, uint32_t a, uint32_t b, uint32_t borrow_in)
{
    uint32_t r = a - b - borrow_in;
    c->c = (uint8_t)((uint64_t)a < (uint64_t)b + borrow_in);
    c->v = (uint8_t)(((a ^ b) & (a ^ r)) >> 31);
    GSP_NZ(c, r);
    return r;
}

/* XY arithmetic: N = X zero, V = X sign, Z = Y zero, C = Y sign.
 * (DIAG's HVLINE loops on CMPXY + JRNN/JRNZ until both halves match.) */
static inline uint32_t gsp_xy_flags(gsp_t *c, uint32_t r)
{
    uint16_t x = (uint16_t)r, y = (uint16_t)(r >> 16);
    c->n = (uint8_t)(x == 0);
    c->v = (uint8_t)(x >> 15);
    c->z = (uint8_t)(y == 0);
    c->c = (uint8_t)(y >> 15);
    return r;
}

static inline uint32_t gsp_addxy(gsp_t *c, uint32_t a, uint32_t b)
{
    uint32_t x = (uint16_t)(a + b), y = (uint16_t)((a >> 16) + (b >> 16));
    return gsp_xy_flags(c, (y << 16) | x);
}

/* a - b per half */
static inline uint32_t gsp_subxy(gsp_t *c, uint32_t a, uint32_t b)
{
    uint32_t x = (uint16_t)(a - b), y = (uint16_t)((a >> 16) - (b >> 16));
    return gsp_xy_flags(c, (y << 16) | x);
}

/* ---- shifts (k = 0..31) --------------------------------------------------------- */

static inline uint32_t gsp_sll(gsp_t *c, uint32_t v, uint32_t k)
{
    c->c = (uint8_t)(k ? (v >> (32 - k)) & 1 : 0);
    v = k ? v << k : v;
    c->z = (uint8_t)(v == 0);
    return v;
}

static inline uint32_t gsp_sla(gsp_t *c, uint32_t v, uint32_t k)
{
    uint32_t r = k ? v << k : v;
    /* overflow if any bit shifted through the sign position differs */
    uint32_t top = k ? (uint32_t)((int32_t)v >> (31 - k)) : 0;
    c->v = (uint8_t)(k && top != 0 && top != 0xFFFFFFFFu);
    c->c = (uint8_t)(k ? (v >> (32 - k)) & 1 : 0);
    GSP_NZ(c, r);
    return r;
}

static inline uint32_t gsp_srl(gsp_t *c, uint32_t v, uint32_t k)
{
    c->c = (uint8_t)(k ? (v >> (k - 1)) & 1 : 0);
    v = k ? v >> k : v;
    c->z = (uint8_t)(v == 0);
    return v;
}

static inline uint32_t gsp_sra(gsp_t *c, uint32_t v, uint32_t k)
{
    c->c = (uint8_t)(k ? ((uint32_t)((int32_t)v >> (k - 1))) & 1 : 0);
    v = k ? (uint32_t)((int32_t)v >> k) : v;
    GSP_NZ(c, v);
    return v;
}

static inline uint32_t gsp_rl(gsp_t *c, uint32_t v, uint32_t k)
{
    uint32_t r = k ? (v << k) | (v >> (32 - k)) : v;
    c->c = (uint8_t)(k ? r & 1 : 0);
    c->z = (uint8_t)(r == 0);
    return r;
}

/* Multiply/divide (rs, rd are register indices; even rd uses the pair rd:rd+1). */
void gsp_mpys(gsp_t *c, int rs, int rd);
void gsp_mpyu(gsp_t *c, int rs, int rd);
void gsp_divs(gsp_t *c, int rs, int rd);
void gsp_divu(gsp_t *c, int rs, int rd);
void gsp_mods(gsp_t *c, int rs, int rd);
void gsp_modu(gsp_t *c, int rs, int rd);

/* ---- status register ---------------------------------------------------------- */

static inline uint32_t gsp_get_st(const gsp_t *c)
{
    uint32_t st = c->st_rest & ~0xF0000FFFu;
    st |= (uint32_t)c->n << 31 | (uint32_t)c->c << 30 | (uint32_t)c->z << 29 | (uint32_t)c->v << 28;
    st |= (uint32_t)(c->fs[0] & 31) | (uint32_t)c->fe[0] << 5;
    st |= (uint32_t)(c->fs[1] & 31) << 6 | (uint32_t)c->fe[1] << 11;
    return st;
}

static inline void gsp_set_st(gsp_t *c, uint32_t st)
{
    c->n = (uint8_t)(st >> 31 & 1);
    c->c = (uint8_t)(st >> 30 & 1);
    c->z = (uint8_t)(st >> 29 & 1);
    c->v = (uint8_t)(st >> 28 & 1);
    c->fs[0] = (uint8_t)(st & 31);
    c->fe[0] = (uint8_t)(st >> 5 & 1);
    c->fs[1] = (uint8_t)(st >> 6 & 31);
    c->fe[1] = (uint8_t)(st >> 11 & 1);
    if (!c->fs[0])
        c->fs[0] = 32;
    if (!c->fs[1])
        c->fs[1] = 32;
    c->st_rest = st & ~0xF0000FFFu;
}

/* ---- stack ----------------------------------------------------------------------- */

static inline void gsp_push(gsp_t *c, uint32_t v)
{
    c->r[GSP_SP] -= 32;
    gsp_wr(c, c->r[GSP_SP], 32, v);
}

static inline uint32_t gsp_pop(gsp_t *c)
{
    uint32_t v = gsp_rd(c, c->r[GSP_SP], 32);
    c->r[GSP_SP] += 32;
    return v;
}

/* ---- execution -------------------------------------------------------------------- */

/* A recompiled module: runs from c->pc until control leaves the module. */
typedef void (*gsp_module_fn)(gsp_t *c);

typedef struct {
    uint32_t start, end;      /* code address range [start, end) */
    gsp_module_fn fn;
    const char *name;
} gsp_module;

extern const gsp_module gsp_modules[];
extern const int gsp_nmodules;

void gsp_reset(gsp_t *c);

/* Runs until the budget is used up or c->stop is set. */
void gsp_run(gsp_t *c, int32_t budget);

/* Raises a trap/interrupt: pushes PC and ST, clears IE, jumps to vector n. */
void gsp_trap(gsp_t *c, int n, uint32_t return_pc);

/* Called by generated code when an instruction cannot be translated. */
void gsp_unimplemented(gsp_t *c, uint32_t pc, const char *what);

/* Graphics instructions the game uses rarely (DIAG, a few screens). */
void gsp_pixt_to_xy(gsp_t *c, uint32_t value, uint32_t xy);
void gsp_fill(gsp_t *c, int xy);
void gsp_pixblt_b_xy(gsp_t *c);
void gsp_line(gsp_t *c, int mode);
void gsp_drav(gsp_t *c, int rs, int rd);
uint32_t gsp_cvxyl(gsp_t *c, uint32_t xy);

/* Per-instruction tracing, compiled in only with -DGSP_TRACE. */
#ifdef GSP_TRACE
void gsp_trace_insn(gsp_t *c, uint32_t addr);
#define GSP_TRACE_INSN(c, addr) gsp_trace_insn(c, addr)
#else
#define GSP_TRACE_INSN(c, addr) ((void)0)
#endif

/* Entry check at the top of every block: leave to the dispatcher when the
 * time slice is used up or an interrupt is waiting. */
#define GSP_ENTER(c, addr, ninsn)                                                   \
    do {                                                                            \
        if ((c)->budget <= 0 || gsp_irq_ready(c)) {                                 \
            (c)->pc = (addr);                                                       \
            return;                                                                 \
        }                                                                           \
        (c)->budget -= (ninsn);                                                     \
    } while (0)

static inline int gsp_irq_ready(const gsp_t *c)
{
    return (c->st_rest & GSP_ST_IE) && (c->io[0x11] & c->io[0x12] & 0x0E06u);
}

#endif

#include "gsp.h"

#include <stdio.h>
#include <string.h>

#define VECTOR(n) (0xFFFFFFE0u - 32u * (uint32_t)(n))

void gsp_reset(gsp_t *c)
{
    struct gsp_hw *hw = c->hw;
    void (*trace)(struct gsp *, void *) = c->trace;
    void *trace_user = c->trace_user;
    memset(c, 0, sizeof *c);
    c->hw = hw;
    c->trace = trace;
    c->trace_user = trace_user;
    c->fs[0] = c->fs[1] = 32;
    c->pc = gsp_rd(c, VECTOR(0), 32);
}

void gsp_trap(gsp_t *c, int n, uint32_t return_pc)
{
    gsp_push(c, return_pc);
    gsp_push(c, gsp_get_st(c));
    c->st_rest &= ~(GSP_ST_IE | GSP_ST_PBX);
    c->pc = gsp_rd(c, VECTOR(n), 32);
}

void gsp_unimplemented(gsp_t *c, uint32_t pc, const char *what)
{
    c->fault = what;
    c->fault_pc = pc;
    c->stop = 1;
}

static const gsp_module *find_module(uint32_t pc)
{
    int lo = 0, hi = gsp_nmodules - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const gsp_module *m = &gsp_modules[mid];
        if (pc < m->start)
            hi = mid - 1;
        else if (pc >= m->end)
            lo = mid + 1;
        else
            return m;
    }
    return NULL;
}

/* Highest-priority pending, enabled interrupt as a trap number, or -1. */
static int pending_trap(const gsp_t *c)
{
    uint16_t p = c->io[0x11] & c->io[0x12];
    if (!(c->st_rest & GSP_ST_IE) || !p)
        return -1;
    if (p & GSP_INT_HI)
        return 9;
    if (p & GSP_INT_DI)
        return 10;
    if (p & GSP_INT_WV)
        return 11;
    if (p & GSP_INT_X1)
        return 1;
    if (p & GSP_INT_X2)
        return 2;
    return -1;
}

void gsp_run(gsp_t *c, int32_t budget)
{
    c->budget = budget;
    while (c->budget > 0 && !c->stop) {
        int t = pending_trap(c);
        if (t >= 0)
            gsp_trap(c, t, c->pc);
        c->hist[c->hist_pos++ & 63] = c->pc;
        if (c->trace)
            c->trace(c, c->trace_user);
        const gsp_module *m = find_module(c->pc);
        if (!m) {
            c->fault = "jump to an address outside the recompiled code";
            c->fault_pc = c->pc;
            c->stop = 1;
            break;
        }
        int32_t before = c->budget;
        m->fn(c);
        c->executed += (uint64_t)(before - c->budget);
    }
}

/* ---- multiply / divide ---------------------------------------------------------- */

static int is_even(int r) { return ((r & 15) & 1) == 0; }

void gsp_mpys(gsp_t *c, int rs, int rd)
{
    int64_t p = (int64_t)(int32_t)gsp_sext(c->r[rs], c->fs[1]) * (int64_t)(int32_t)c->r[rd];
    if (is_even(rd)) {
        c->r[rd] = (uint32_t)((uint64_t)p >> 32);
        c->r[rd + 1] = (uint32_t)p;
    } else {
        c->r[rd] = (uint32_t)p;
    }
    c->n = (uint8_t)(p < 0);
    c->z = (uint8_t)(p == 0);
}

void gsp_mpyu(gsp_t *c, int rs, int rd)
{
    uint64_t p = (uint64_t)gsp_zext(c->r[rs], c->fs[1]) * (uint64_t)c->r[rd];
    if (is_even(rd)) {
        c->r[rd] = (uint32_t)(p >> 32);
        c->r[rd + 1] = (uint32_t)p;
    } else {
        c->r[rd] = (uint32_t)p;
    }
    c->z = (uint8_t)(p == 0);
}

void gsp_divs(gsp_t *c, int rs, int rd)
{
    int32_t d = (int32_t)c->r[rs];
    if (is_even(rd)) {
        int64_t n = (int64_t)(((uint64_t)c->r[rd] << 32) | c->r[rd + 1]);
        if (d == 0) {
            c->v = 1;
            return;
        }
        int64_t q = n / d, r = n % d;
        if (q > INT32_MAX || q < INT32_MIN) {
            c->v = 1;
            return;
        }
        c->r[rd] = (uint32_t)q;
        c->r[rd + 1] = (uint32_t)r;
        c->v = 0;
        GSP_NZ(c, c->r[rd]);
    } else {
        int32_t n = (int32_t)c->r[rd];
        if (d == 0 || (n == INT32_MIN && d == -1)) {
            c->v = 1;
            return;
        }
        c->r[rd] = (uint32_t)(n / d);
        c->v = 0;
        GSP_NZ(c, c->r[rd]);
    }
}

void gsp_divu(gsp_t *c, int rs, int rd)
{
    uint32_t d = c->r[rs];
    if (d == 0) {
        c->v = 1;
        return;
    }
    if (is_even(rd)) {
        uint64_t n = ((uint64_t)c->r[rd] << 32) | c->r[rd + 1];
        uint64_t q = n / d;
        if (q > 0xFFFFFFFFu) {
            c->v = 1;
            return;
        }
        c->r[rd] = (uint32_t)q;
        c->r[rd + 1] = (uint32_t)(n % d);
    } else {
        c->r[rd] = c->r[rd] / d;
    }
    c->v = 0;
    c->z = (uint8_t)(c->r[rd] == 0);
}

void gsp_mods(gsp_t *c, int rs, int rd)
{
    int32_t d = (int32_t)c->r[rs], n = (int32_t)c->r[rd];
    if (d == 0 || (n == INT32_MIN && d == -1)) {
        c->v = 1;
        return;
    }
    c->r[rd] = (uint32_t)(n % d);
    c->v = 0;
    GSP_NZ(c, c->r[rd]);
}

void gsp_modu(gsp_t *c, int rs, int rd)
{
    if (c->r[rs] == 0) {
        c->v = 1;
        return;
    }
    c->r[rd] %= c->r[rs];
    c->v = 0;
    c->z = (uint8_t)(c->r[rd] == 0);
}

#ifdef GSP_TRACE
/* Ring buffer of executed instruction addresses (see gsp_trace_dump). */
uint32_t gsp_trace_ring[4096];
unsigned gsp_trace_pos;

void gsp_trace_insn(gsp_t *c, uint32_t addr)
{
    (void)c;
    gsp_trace_ring[gsp_trace_pos++ & 4095] = addr;
}
#endif

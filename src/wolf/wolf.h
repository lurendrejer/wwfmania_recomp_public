/*
 * Midway Wolf Unit hardware as WWF WrestleMania uses it: memory map, I/O
 * (with the VMUX address remapping), DMA blitter, color RAM, display pages
 * and interrupts, around the recompiled TMS34010 code.
 */
#ifndef WWF_WOLF_H
#define WWF_WOLF_H

#include <stdint.h>

#include "assets/catalog.h"
#include "cpu/gsp.h"
#include "mods/mods.h"
#include "sound/sound.h"
#include "video/gfx.h"
#include "video/video.h"

/* Player inputs, active high (the hardware is active low). */
enum {
    WOLF_UP = 1 << 0, WOLF_DOWN = 1 << 1, WOLF_LEFT = 1 << 2, WOLF_RIGHT = 1 << 3,
    WOLF_B1 = 1 << 4, WOLF_B2 = 1 << 5, WOLF_B3 = 1 << 6, WOLF_B4 = 1 << 7,
};

/* Players 3 and 4 (wolf.extra_player, for mods): the stick is WOLF_UP..RIGHT, then these. */
enum {
    WOLF_X_PUNCH = 1 << 4, WOLF_X_BLOCK = 1 << 5, WOLF_X_SPUNCH = 1 << 6,
    WOLF_X_KICK = 1 << 7, WOLF_X_SKICK = 1 << 8, WOLF_X_START = 1 << 9,
};

/* COINS register bits, active high (orig/LINK.EQU START_BITS, TEST.ASM). */
enum {
    WOLF_COIN1 = 1 << 0, WOLF_COIN2 = 1 << 1, WOLF_START1 = 1 << 2, WOLF_TILT = 1 << 3,
    WOLF_TEST = 1 << 4, WOLF_START2 = 1 << 5, WOLF_SERVICE = 1 << 6,
};

/* One image the DMA drew (wolf_trace_draws), for the image inspector (src/platform/inspect.c). */
typedef struct {
    char name[16];         /* the image's label (e.g. B4AM4A07; NEWRINGB_12 for a background piece); "" for a fill */
    int x, y, w, h;        /* where, in bitmap pixels: x as the DMA's (with the extra columns), y with the page */
    uint16_t ctrl, pal, color;
    uint8_t hd;            /* 1: drawn from a high-resolution override */
    uint8_t background;    /* 1: a background piece (.BDD) */
} wolf_draw;

#define WOLF_MAX_DRAWS 1024

typedef struct {
    uint32_t sag;          /* image ROM address of pixel 0 */
    gfx_image *img;        /* drawable (with override when available) */
    gfx_image own;         /* used when the image has no catalog entry */
    const cat_image *ci;   /* catalog entry whose override loads on first draw */
    char name[16];         /* label, as in imgrom.txt */
    uint8_t background;    /* from a .BDD file */
    uint8_t layer;         /* art_layer of the image (src/assets/layers.h) */
} wolf_romimg;

typedef struct gsp_hw {
    gsp_t cpu;
    video v;
    catalog cat;
    gfx_cache gc;

    wolf_romimg *imgs;     /* sorted by sag */
    int nimgs;
    unsigned art_off;      /* art layers whose overrides are switched off (bit per art_layer), see wolf_set_art_layers */
    int art_off_set;       /* art_off has been applied at least once */
    img_lib bdd[64];       /* background data files (.BDD) */
    char bdd_name[64][32];
    int nbdd;
    struct wolf_bddpal *bddpal; /* palettes of the background images (gen/bddpal.txt), for true-colour overrides */
    int nbddpal;
    char override_dir[512];
    char gen_dir[512];     /* where symbols.txt and the generated tables are */

    uint8_t *rom;          /* 0xFF000000..0xFFFFFFFF (the mods' ROM below 0xFF800000) */
    uint8_t *ram;          /* 0x01000000..0x013FFFFF */
    uint8_t *cmos;         /* 0x01400000..0x014FFFFF */
    char cmos_path[512];

    uint16_t dma[16];      /* DMA registers 0x1A00000 + i*16 */
    uint16_t win_l, win_r, win_t, win_b;
    uint16_t cmapsel, sysctrl, vmux;

    /* security PIC (serial number protocol, see wolf.c) */
    int pic_running, pic_bit12, pic_last_cmd, pic_index;
    uint8_t pic_serial[16], pic_out;

    /* sound board: the file player when sounds were extracted (not owned),
     * otherwise a stub that only answers the handshakes */
    snd *snd;
    int snd_have_high, snd_just_reset, snd_reset_line;
    uint16_t snd_high;
    uint16_t snd_queue[8];
    int snd_count;

    /* inputs */
    /* player[0], player[1]: the low and high byte of SWITCH (stick WOLF_UP..RIGHT,
     * then the buttons WOLF_B1..B3 = punch, block, super punch; B4 is not used by
     * the game). player[2], player[3]: the low and high byte of SWITCH2, where
     * bit 0/1 is player 1's kick/super kick and bit 4/5 player 2's. */
    uint8_t player[4];
    /* Two more sticks for mods (mods/fourplayer); the original game has no input for
     * them. Bits 0-3 as player[0] (WOLF_UP..RIGHT), then bit 4 punch, 5 block,
     * 6 super punch, 7 kick, 8 super kick, 9 start. Nothing reads them unless a mod does. */
    uint16_t extra_player[2];
    uint16_t coin_bits;    /* active high: coins, starts, test, service */
    uint16_t dip;          /* raw DIP switch value (active low as read) */

    /* timing */
    int32_t frame_insns;   /* instructions per video frame */
    uint64_t frames;
    int display_row;       /* first bitmap row of the displayed page */

    /* Draw margin: how far past the 400 pixel screen the game draws (its
     * SCRNTL/SCRNLR, 32 in the original). -1 leaves the game's own value. */
    int scale;             /* framebuffer pixels per original pixel */
    int extra;             /* extra bitmap width on each side, see wolf_set_extra_size */
    int extra_y;           /* extra rows above and below each display page */
    int draw_margin;       /* x, see wolf_set_draw_margin */
    int draw_margin_y;     /* y, -1 = the game's own */
    uint32_t worldtlx_addr;            /* bit address of WORLDTLX (16:16 world x of the screen's left edge) */
    uint32_t worldtly_addr;            /* WORLDTLY (16:16 world y of the screen's top edge) */
    /* game options (wolf_set_options) */
    int skip_selftest;
    int free_play;          /* the coinage dipswitches read as free play */
    unsigned buddy_sides;              /* bit 0 / 1: a computer buddy for player 1 / 2 asked for (wolf_set_options) */
    unsigned powerup_flags;            /* GAME.EQU BLOCKING_OFF... BUDDY_MODE bits */
    int no_ringout_timer;
    int all_shadows;
    int wrestler7;          /* set by a mod that brings wrestler number 7 (mods/adambomb): the crash fix
                               that turns a 7 into Doink's 6 (apply_fixes) is then left out */
    uint32_t reduce_bog_addr, crowd_process_addr;
    uint32_t pu1_addr, pu2_addr, procptrs_addr;
    int hud_dx, hud_dy;                /* see wolf_set_hud_spread */
    struct {                           /* the lines of text on the screen this frame (gsp_hud_shift): */
        uint64_t frame;                /* frames + 1 when they were found, 0 = never */
        int n;
        int g[32][4];                  /* x0, y0, x1, y1 */
    } hud_text;
    uint32_t gamstate_addr;            /* GAMSTATE, 0 if unknown */
    uint32_t index1_addr, index2_addr; /* the wrestlers chosen (SELECT.ASM), 0 if unknown */
    uint32_t lineup_addr;              /* FINAL_BATTLE_LINEUP: 8 wrestler numbers, then 0xFF (PROGRESS.ASM) */
    unsigned prefetch_done;            /* wrestlers whose art was queued (wolf_prefetch_tick), bit per number */
    uint8_t prefetch_lineup[9];        /* the lineup it was queued for */
    uint32_t trace_ring[256];          /* the last dispatch addresses, newest at trace_n-1 (only while the hook is on) */
    unsigned trace_n;
    int gamstate;                      /* GAMSTATE as of the last frame, -1 if unknown */
    int in_match;                      /* 1 while a match is on (GAMSTATE INGAME, set each frame) */
    int hud_level;                     /* 1 while a level screen is shown (set each frame) */
    int scroll_inset_y;                /* see wolf_set_scroll_inset_y */
    int scroll_inset;                  /* see wolf_set_scroll_inset */
    int no_flash_white, no_flash_red;    /* see wolf_set_flashes */
    uint32_t no_flash_white_addr, no_flash_red_addr;   /* core.gen.txt variables, 0 if unknown */
    int hide_ropes;                      /* for mods (mods/sansring): the ropes are not drawn; 0 unless a mod sets it */
    int no_select_timer;                 /* see wolf_set_select_timer */
    int no_match_timer;                  /* see wolf_set_match_timer */
    uint32_t no_match_timer_addr;        /* no_match_timer (core.gen.txt), 0 if unknown */
    uint32_t no_select_timer_addr;       /* no_select_timer (core.gen.txt), 0 if unknown */
    uint32_t intro_lo, intro_hi;         /* the power-up info screen's wait loop (DIAG.ASM CTMP_WAIT..CTMP2) */
    uint32_t errwait_lo, errwait_hi, swset1_addr; /* its 'ERRORS DETECTED -- ANY BUTTON TO CONTINUE' loop (CTMP1..CTMP0) and the buttons it saved (SWSET1) */
    uint32_t leave_ring_addr;            /* leave_ring (core.gen.txt), 0 if unknown */
    uint32_t view_extra_addr;            /* view_extra (core.gen.txt), 0 if unknown */
    uint32_t scrntl_addr, scrnlr_addr;   /* bit addresses of those variables, 0 if unknown */

    /* mods switched on for this run (src/mods, docs/MODS.md) */
    const wwf_mod *mods[WWF_MAX_MODS];
    void *mod_state[WWF_MAX_MODS];
    int mod_arg[WWF_MAX_MODS];
    int nmods;

    /* diagnostics */
    int log_unmapped;
    uint32_t unmapped_count;

    /* draw recording (wolf_trace_draws): what was drawn on each of the two display pages, by the last
     * frame that drew on it */
    wolf_draw *draws[2];
    int ndraws[2];
    uint64_t draws_frame[2];
} wolf;

/*
 * gen_dir holds rom.bin, ram.bin and imgrom.txt (tools/gsp/gsp2c.py and
 * genimg.py); img_dir is orig/IMG; override_dir may be NULL.
 */
int wolf_init(wolf *w, const char *gen_dir, const char *img_dir, const char *override_dir,
              int scale, const char *cmos_path, catalog_warn_fn warn);
void wolf_free(wolf *w);
/* Switches the overrides of art layers (src/assets/layers.h) off: bit n set = layer n is drawn from the original pixels and
 * its override is not read. Takes effect at once and can be changed at any time; what is loaded stays in memory. */
void wolf_set_art_layers(wolf *w, unsigned off);

/* Test: makes the bitmap 512 + 2 * extra pixels wide. The game keeps using x
 * in 0..511, shifted by extra, and its clip window means the whole width.
 * Call right after wolf_init, then set the view (video.view_pad, at most
 * 56 + extra) and the draw margin (wolf_set_draw_margin, the same). */
int wolf_set_extra_width(wolf *w, int extra);

/* The same, and `extra_y` more rows above and below each display page (pages
 * are 256 + 2 * extra_y rows apart; the game's y is mapped page by page, and
 * its clip window means the whole page). Call before the game runs. */
int wolf_set_extra_size(wolf *w, int extra, int extra_y);

/* Like wolf_set_draw_margin for the rows: how far above and below the 254 row
 * screen the game draws (its own: 0). */
void wolf_set_draw_margin_y(wolf *w, int margin);

/* Wide view: keeps the camera `inset` pixels away from each end of the
 * level's scroll range (the game keeps WORLDTLX in 0x12F..0x648, WRESTLE2.ASM
 * scroll_world), so a wider view does not run off the background. Applied
 * to WORLDTLX just before each display, only while it lies inside the
 * original range (title and menu screens are left alone). 0 = off. */
void wolf_set_scroll_inset(wolf *w, int inset);

/* The same for the rows: the level's background reaches from world y -142 to
 * 405 (measured, one level), the camera normally from -142 to 151 (the game's
 * front fence limit, WRESTLE2.ASM). With `inset` extra rows shown above and
 * below, keeps WORLDTLY in -142 + inset .. 151 - inset. Like the x version it
 * only acts on a level screen (WORLDTLX inside the level range). 0 = off. */
void wolf_set_scroll_inset_y(wolf *w, int inset);

/* Wide view: moves the screen-relative objects (the HUD: life bars, names,
 * recover bars) outward by dx columns (left ones left, right ones right) and
 * dy rows (top ones up, bottom ones down), so they stay at the edges of a view
 * wider than the original screen. Applied while a level screen is shown. 0/0 =
 * off. */
void wolf_set_hud_spread(wolf *w, int dx, int dy);

/* Options the game itself has, or that only skip parts of it:
 *  skip_selftest: the power-up test (ROM check) is bypassed by the board's own
 *   bypass switch (DIP switch bit read by READ_DIP; WRESTLE.ASM skip_powerst).
 *   Call before the first frame.
 *  powerup_flags: the game's secret "powerups" (GAME.EQU): 1 blocking off, 2
 *   instant combos, 4 ring outs, 8 no ring, 16 move names, 32 drone meters,
 *   64 hyper speed, 128 buddy mode (bit 7 a buddy for player 1, bit 8 one for player 2; both is the game's own buddy mode). They are set in p1powerup_request and
 *   p2powerup_request every frame, which is what the MAME cheats do.
 *  no_ringout_timer: keeps every wrestler's RING_TIME positive, so the count
 *   that hurts a wrestler who stays outside the ring never starts.
 *  all_shadows: keeps `reduce_bog` (the number of wrestlers above two, which
 *   turns off shadows and some other work in a crowded ring) at 0. */
/* Free play is a dipswitch setting (DIP.EQU DPCOINAGE all on); the game reads the
 * switches when it starts, so this takes effect at the next start. */
void wolf_set_free_play(wolf *w, int on);
/* The spread of the wrestlers in a match, in world pixels: the smallest and largest x (OBJ_XPOSINT) of the
 * wrestler processes 0 to 5 (the referee is left out). Returns how many there are (0 = none, or unknown). */
int wolf_wrestler_extent(const wolf *w, int *min_x, int *max_x);
void wolf_set_options(wolf *w, int skip_selftest, unsigned powerup_flags, int no_ringout_timer, int all_shadows);
/* The full screen white / red flashes of hard hits and of the finish (flash_white, flash_red). */
void wolf_set_flashes(wolf *w, int no_white, int no_red);
/* off = 1: the clock of the character select screen never runs out. */
void wolf_set_select_timer(wolf *w, int off);
/* off: the round's clock does not count down (the option "no match timer") */
void wolf_set_match_timer(wolf *w, int off);

/* Sets the draw margin (0..56 + extra, the bitmap's padding; -1 = the game's own 32).
 * The view (video.view_pad) should not be wider than this. */
void wolf_set_draw_margin(wolf *w, int margin);

/* Runs one video frame (including the display interrupt). Returns 0 on a fault. */
int wolf_frame(wolf *w);

/* Records every image the DMA draws (off by default; for the image inspector). Returns 0 when out of memory. */
int wolf_trace_draws(wolf *w, int on);
/* What the shown page holds, as the last frame that drew on it left it (empty unless recording). */
const wolf_draw *wolf_shown_draws(const wolf *w, int *n);

/* Save states (src/wolf/state.c): the machine between two frames, to a file and back. Only good for
 * the same build and view size. Return 0 on failure (the machine is untouched when a load fails
 * before the data was read completely). */
int wolf_state_save(wolf *w, const char *path);
int wolf_state_load(wolf *w, const char *path);

/* Bit address of a symbol of the recompiled game (0 if unknown), from symbols.txt. */
uint32_t wolf_symbol_addr(const wolf *w, const char *name);

/* Asks the image cache to load the high-resolution art of the wrestlers in the match, then the opponents in line
 * (FINAL_BATTLE_LINEUP), ahead of the game's drawing it (gfx_prefetch_*). Call once a frame; it does nothing unless
 * the platform turned async loading on (gfx_cache_set_async), so headless runs and tests are unchanged. */
void wolf_prefetch_tick(wolf *w);

/* The address of the dispatch 'back' steps before the latest one (0 = the latest), 0 if there is none. */
uint32_t wolf_trace_pc(const wolf *w, unsigned back);

/* Symbol lookup for fault messages (loads symbols.txt lazily). */
const char *wolf_symbol(const char *gen_dir, uint32_t addr, uint32_t *offset);

#endif

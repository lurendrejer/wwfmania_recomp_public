/*
 * wwfrun - run the recompiled game headless (tests, debugging).
 *
 *   wwfrun <gen_dir> <img_dir> <frames> [--shots DIR] [--every N] [--scale N]
 *          [--log-unmapped] [--start FRAME] [--cmos FILE] [--expect-gamestate N]
 *          [--art DIR]        high-resolution art overrides (folder or zip, docs/ASSET_OVERRIDES.md)
 *          [--draws FRAME]    list the images on the shown page at that frame (the image inspector's data)
 *          [--input FRAME,FRAMES,WHAT,BITS]...   WHAT: p1..p4 or coins
 *          e.g. --input 1500,5,p1,16 presses P1 button 1 at frame 1500
 *          --input FRAME,FRAMES,WHAT,BITS,PERIOD  presses it for the first half of every PERIOD frames
 *          (button mashing) from FRAME for FRAMES frames
 *          [--sound DIR] [--wav FILE] [--expect-sounds N]
 *          plays the extracted sounds (dcsrip) and can record them
 *
 * Prints a fault (with the nearest label) if the game stops, and can dump
 * the displayed screen as PNGs.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "png_write.h"
#include "wolf/replay.h"
#include "wolf/wolf.h"

static void quiet_warn(const char *msg, void *user)
{
    (void)user;
    if (strncmp(msg, "duplicate image ", 16) != 0)
        fprintf(stderr, "warning: %s\n", msg);
}

/* ---- profiling: which labels the dispatcher enters ---- */
#define PROF_SLOTS 4096
static struct {
    const char *gen;
    const char *name[PROF_SLOTS];
    unsigned long count[PROF_SLOTS];
    int n;
} prof;

static const char *watch_names;   /* WWF_WATCH=a,b: print when a dispatch enters one of these labels */
static long watch_from, watch_to;
static long watch_frame;

static void prof_hook(gsp_t *c, void *user)
{
    (void)user;
    if (watch_names && watch_frame >= watch_from && watch_frame <= watch_to) {
        uint32_t o;
        const char *n = wolf_symbol(prof.gen, c->pc, &o);
        if (n && !o && (watch_names[0] == '*' || strstr(watch_names, n)))
            printf("frame %ld: %s (a0=%X a3=%X a4=%X)\n", watch_frame, n, c->r[0], c->r[3], c->r[4]);
        return;
    }
    uint32_t off;
    const char *s = wolf_symbol(prof.gen, c->pc, &off);
    if (!s)
        s = "?";
    for (int i = 0; i < prof.n; i++)
        if (prof.name[i] == s) {
            prof.count[i]++;
            return;
        }
    if (prof.n < PROF_SLOTS) {
        prof.name[prof.n] = s;
        prof.count[prof.n++] = 1;
    }
}

static void prof_dump(long frame)
{
    printf("-- frame %ld: top dispatch targets\n", frame);
    for (int k = 0; k < 8; k++) {
        int best = -1;
        for (int i = 0; i < prof.n; i++)
            if (prof.count[i] && (best < 0 || prof.count[i] > prof.count[best]))
                best = i;
        if (best < 0)
            break;
        printf("   %8lu %s\n", prof.count[best], prof.name[best]);
        prof.count[best] = 0;
    }
    prof.n = 0;
}

static uint32_t sym_addr(const char *gen, const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/symbols.txt", gen);
    FILE *f = fopen(path, "r");
    char line[256];
    unsigned a;
    char n[200];
    while (f && fgets(line, sizeof line, f))
        if (sscanf(line, "%x %199s", &a, n) == 2 && !strcmp(n, name)) {
            fclose(f);
            return a;
        }
    if (f)
        fclose(f);
    return 0;
}

static int shot(wolf *w, const char *dir, unsigned long frame)
{
    int ow = video_view_width(&w->v), oh = video_view_height(&w->v);
    uint32_t *argb = malloc((size_t)ow * oh * 4);
    uint8_t *rgba = malloc((size_t)ow * oh * 4);
    if (!argb || !rgba)
        return 0;
    video_to_argb(&w->v, argb);
    for (size_t i = 0; i < (size_t)ow * oh; i++) {
        rgba[i * 4] = (uint8_t)(argb[i] >> 16);
        rgba[i * 4 + 1] = (uint8_t)(argb[i] >> 8);
        rgba[i * 4 + 2] = (uint8_t)argb[i];
        rgba[i * 4 + 3] = 0xFF;
    }
    char path[1024];
    snprintf(path, sizeof path, "%s/frame_%05lu.png", dir, frame);
    int ok = png_write_rgba(path, ow, oh, rgba);
    free(argb);
    free(rgba);
    return ok;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: wwfrun <gen_dir> <img_dir> <frames> [--shots DIR] [--every N] "
                        "[--scale N] [--extra N] [--inset N] [--hud DX DY] [--skip-selftest] [--powerups N] [--no-ringout-timer] [--all-shadows] [--no-select-timer] [--no-white-flash] [--no-red-flash] [--view-pad N] [--mod NAME[=N]] [--mod-on NAME[=N]@FRAME] [--mod-off NAME@FRAME] [--poke NAME=VALUE[@FRAME]] [--log-unmapped] [--start FRAME] [--art DIR] [--draws FRAME]\n");
        return 2;
    }
    const char *gen = argv[1], *img = argv[2], *shots = NULL, *cmos = NULL, *art = NULL;
    long frames = atol(argv[3]), every = 60, start = 0, draws_at = -1;
    int scale = 1, log_unmapped = 0, view_pad = 0, margin = -1, extra = 0, inset = 0, extra_y = 0, view_pad_y = 0, margin_y = -1, inset_y = 0, hud_dx = 0, hud_dy = 0, skip_selftest = 0, no_ringout = 0, all_shadows = 0, free_play = 0, no_select_timer = 0, no_match_timer = 0, no_flash_white = 0, no_flash_red = 0;
    unsigned powerups = 0;
    struct { const char *spec; long at; int on; } modsw[8];   /* --mod-on NAME[=N]@FRAME, --mod-off NAME@FRAME */
    int nmodsw = 0;
    uint32_t peek[8];
    int npeek = 0;
    const char *mods[WWF_MAX_MODS];
    int nmods = 0;
    struct { uint32_t addr; unsigned val; long from; } pokes[8];   /* --poke NAME=VALUE[@FRAME] */
    int npokes = 0;
    struct { uint32_t addr; unsigned val; long at, to; } pokeat[16];   /* --pokeat HEXADDR,VALUE,FRAME: once, 16 bits */
    int npokeat = 0;
    uint32_t track_proc = 0;                       /* --track PROCESS,FROM,TO: print a wrestler's X, Y, Z and mode each frame */
    long track_from = 0, track_to = -1;
    struct { long frame, len, period; int what; unsigned bits; } inputs[256];
    long save_at = -1, load_at = -1;                                 /* --save-state / --load-state FRAME PATH */
    const char *save_path = NULL, *load_path = NULL;
    long rec_from = -1, rec_to = -1, play_at = -1;         /* --record FROM TO STATE REC, --replay FRAME STATE REC */
    const char *rec_state = NULL, *rec_path = NULL, *play_state = NULL, *play_path = NULL;
    replay rp;
    memset(&rp, 0, sizeof rp);
    int ninputs = 0;
    long profile = 0;
    int expect = -9999;
    const char *sound_dir = NULL, *wav_path = NULL;
    long expect_sounds = -1;
    for (int i = 4; i < argc; i++) {
        if (!strcmp(argv[i], "--shots") && i + 1 < argc) shots = argv[++i];
        else if (!strcmp(argv[i], "--every") && i + 1 < argc) every = atol(argv[++i]);
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mod") && i + 1 < argc && nmods < WWF_MAX_MODS) mods[nmods++] = argv[++i];
        else if ((!strcmp(argv[i], "--mod-on") || !strcmp(argv[i], "--mod-off")) && i + 1 < argc && nmodsw < 8) {
            const int on = argv[i][6] == 'o' && argv[i][7] == 'n';
            char *spec = argv[++i], *at = strrchr(spec, '@');
            if (!at) { fprintf(stderr, "%s needs NAME@FRAME\n", argv[i - 1]); return 2; }
            *at = 0;
            modsw[nmodsw].spec = spec; modsw[nmodsw].at = atol(at + 1); modsw[nmodsw].on = on; nmodsw++;
        }
        else if (!strcmp(argv[i], "--track") && i + 1 < argc) {
            unsigned a = 0;
            if (sscanf(argv[++i], "%x,%ld,%ld", &a, &track_from, &track_to) == 3)
                track_proc = a;
        }
        else if (!strcmp(argv[i], "--pokeat") && i + 1 < argc && npokeat < 16) {
            unsigned a = 0, v = 0;
            long at = 0, to = -1;
            if (sscanf(argv[++i], "%x,%u,%ld-%ld", &a, &v, &at, &to) >= 3) {
                pokeat[npokeat].addr = a;
                pokeat[npokeat].val = v;
                pokeat[npokeat].to = to < at ? at : to;
                pokeat[npokeat++].at = at;
            }
        }
        else if (!strcmp(argv[i], "--poke") && i + 1 < argc && npokes < 8) {
            char name[64];
            unsigned v = 0;
            long from = 0;
            if (sscanf(argv[++i], "%63[^=]=%u@%ld", name, &v, &from) >= 2) {
                pokes[npokes].addr = sym_addr(gen, name);
                pokes[npokes].val = v;
                pokes[npokes].from = from;
                if (pokes[npokes].addr)
                    npokes++;
                else
                    fprintf(stderr, "--poke: no symbol %s\n", name);
            }
        }
        else if (!strcmp(argv[i], "--extra") && i + 1 < argc) extra = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--inset") && i + 1 < argc) inset = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--extra-y") && i + 1 < argc) extra_y = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--view-pad-y") && i + 1 < argc) view_pad_y = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--margin-y") && i + 1 < argc) margin_y = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--inset-y") && i + 1 < argc) inset_y = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--hud") && i + 2 < argc) { hud_dx = atoi(argv[i + 1]); hud_dy = atoi(argv[i + 2]); i += 2; }
        else if (!strcmp(argv[i], "--peek") && i + 1 < argc && npeek < 8) peek[npeek++] = (uint32_t)strtoul(argv[++i], NULL, 16);
        else if (!strcmp(argv[i], "--save-state") && i + 2 < argc) { save_at = atol(argv[i + 1]); save_path = argv[i + 2]; i += 2; }
        else if (!strcmp(argv[i], "--load-state") && i + 2 < argc) { load_at = atol(argv[i + 1]); load_path = argv[i + 2]; i += 2; }
        else if (!strcmp(argv[i], "--record") && i + 4 < argc) { rec_from = atol(argv[i + 1]); rec_to = atol(argv[i + 2]); rec_state = argv[i + 3]; rec_path = argv[i + 4]; i += 4; }
        else if (!strcmp(argv[i], "--replay") && i + 3 < argc) { play_at = atol(argv[i + 1]); play_state = argv[i + 2]; play_path = argv[i + 3]; i += 3; }
        else if (!strcmp(argv[i], "--skip-selftest")) skip_selftest = 1;
        else if (!strcmp(argv[i], "--powerups") && i + 1 < argc) powerups = (unsigned)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--no-ringout-timer")) no_ringout = 1;
        else if (!strcmp(argv[i], "--free-play")) free_play = 1;
        else if (!strcmp(argv[i], "--all-shadows")) all_shadows = 1;
        else if (!strcmp(argv[i], "--no-select-timer")) no_select_timer = 1;
        else if (!strcmp(argv[i], "--no-match-timer")) no_match_timer = 1;
        else if (!strcmp(argv[i], "--no-white-flash")) no_flash_white = 1;
        else if (!strcmp(argv[i], "--no-red-flash")) no_flash_red = 1;
        else if (!strcmp(argv[i], "--margin") && i + 1 < argc) margin = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--view-pad") && i + 1 < argc) view_pad = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--start") && i + 1 < argc) start = atol(argv[++i]);
        else if (!strcmp(argv[i], "--log-unmapped")) log_unmapped = 1;
        else if (!strcmp(argv[i], "--cmos") && i + 1 < argc) cmos = argv[++i];
        else if (!strcmp(argv[i], "--expect-gamestate") && i + 1 < argc) expect = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--input") && i + 1 < argc && ninputs < 256) {
            char what[16] = "";
            long fr = 0, len = 0;
            unsigned bits = 0;
            long period = 0;
            if (sscanf(argv[++i], "%ld,%ld,%15[^,],%u,%ld", &fr, &len, what, &bits, &period) >= 4) {
                inputs[ninputs].frame = fr;
                inputs[ninputs].period = period;
                inputs[ninputs].len = len;
                inputs[ninputs].what = !strcmp(what, "coins") ? 4 : what[0] == 'p' ? what[1] - '1' : what[0] == 'x' ? 3 + (what[1] - '0') : 0;
                inputs[ninputs].bits = bits;
                ninputs++;
            }
        }
        else if (!strcmp(argv[i], "--profile") && i + 1 < argc) profile = atol(argv[++i]);
        else if (!strcmp(argv[i], "--art") && i + 1 < argc) art = argv[++i];
        else if (!strcmp(argv[i], "--draws") && i + 1 < argc) draws_at = atol(argv[++i]);
        else if (!strcmp(argv[i], "--sound") && i + 1 < argc) sound_dir = argv[++i];
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav_path = argv[++i];
        else if (!strcmp(argv[i], "--expect-sounds") && i + 1 < argc) expect_sounds = atol(argv[++i]);
    }
    static wolf w;
    if (!wolf_init(&w, gen, img, art, scale, cmos, quiet_warn)) {
        fprintf(stderr, "init failed\n");
        return 1;
    }
    if (draws_at >= 0)
        wolf_trace_draws(&w, 1);
    w.log_unmapped = log_unmapped;
    if ((extra > 0 || extra_y > 0) && !wolf_set_extra_size(&w, extra, extra_y)) {
        fprintf(stderr, "cannot widen the bitmap\n");
        return 1;
    }
    w.v.view_pad = view_pad;
    w.v.view_pad_y = view_pad_y;
    if (margin_y >= 0)
        wolf_set_draw_margin_y(&w, margin_y);
    wolf_set_scroll_inset(&w, inset);
    wolf_set_scroll_inset_y(&w, inset_y);
    wolf_set_hud_spread(&w, hud_dx, hud_dy);
    wolf_set_options(&w, skip_selftest, powerups, no_ringout, all_shadows);
    wolf_set_select_timer(&w, no_select_timer);
    wolf_set_match_timer(&w, no_match_timer);
    wolf_set_flashes(&w, no_flash_white, no_flash_red);
    wolf_set_free_play(&w, free_play);
    if (margin >= 0)
        wolf_set_draw_margin(&w, margin);
    for (int i = 0; i < nmods; i++) {
        char err[256];
        if (!mods_enable(&w, mods[i], err, sizeof err)) {
            fprintf(stderr, "mod %s: %s\n", mods[i], err);
            return 1;
        }
    }
    enum { RATE = 44100 };
    FILE *wav = NULL;
    unsigned long wav_frames = 0;
    double mix_acc = 0;
    if (sound_dir) {
        char err[512];
        w.snd = snd_open(sound_dir, NULL, RATE, err, sizeof err);
        if (!w.snd) {
            fprintf(stderr, "sound: %s\n", err);
            return 1;
        }
        if (wav_path) {
            wav = fopen(wav_path, "wb");
            if (!wav) {
                fprintf(stderr, "cannot write %s\n", wav_path);
                return 1;
            }
            static const uint8_t hdr[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E',
                                            'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0,
                                            0x44, 0xAC, 0, 0, 0x10, 0xB1, 2, 0, 4, 0, 16, 0,
                                            'd', 'a', 't', 'a', 0, 0, 0, 0};
            fwrite(hdr, 1, sizeof hdr, wav);
        }
    }
    prof.gen = gen;
    if (getenv("WWF_WATCH")) {
        watch_names = getenv("WWF_WATCH");
        watch_from = getenv("WWF_FROM") ? atol(getenv("WWF_FROM")) : 0;
        watch_to = getenv("WWF_TO") ? atol(getenv("WWF_TO")) : 1L << 40;
        w.cpu.trace = prof_hook;
    } else if (profile)
        w.cpu.trace = prof_hook;
    uint32_t gamstate = sym_addr(gen, "GAMSTATE");
    int rc = 0;
    for (long f = 0; f < frames; f++) {
        watch_frame = f;
        if (f == save_at && save_path && !wolf_state_save(&w, save_path))
            fprintf(stderr, "--save-state failed\n");
        if (f == rec_from) {
            if (!wolf_state_save(&w, rec_state))
                fprintf(stderr, "--record: cannot write the state\n");
            replay_start_record(&rp);
        }
        if (f == play_at && (!wolf_state_load(&w, play_state) || !replay_start_play(&rp, play_path)))
            fprintf(stderr, "--replay failed\n");
        if (f == load_at && load_path && !wolf_state_load(&w, load_path))
            fprintf(stderr, "--load-state failed\n");
        memset(w.player, 0, sizeof w.player);
        memset(w.extra_player, 0, sizeof w.extra_player);
        w.coin_bits = 0;
        for (int k = 0; k < ninputs; k++)
            if (f >= inputs[k].frame && f < inputs[k].frame + inputs[k].len &&
                (inputs[k].period <= 1 || (f - inputs[k].frame) % inputs[k].period < (inputs[k].period + 1) / 2)) {
                if (inputs[k].what == 4)
                    w.coin_bits |= (uint16_t)inputs[k].bits;
                else if (inputs[k].what >= 0 && inputs[k].what < 4)
                    w.player[inputs[k].what] |= (uint8_t)inputs[k].bits;
                else if (inputs[k].what == 6 || inputs[k].what == 7)   /* x3, x4: the mods' third and fourth */
                    w.extra_player[inputs[k].what - 6] |= (uint16_t)inputs[k].bits;
            }
        if (f == rec_to && rp.mode == REPLAY_RECORDING && !replay_stop_record(&rp, rec_path))
            fprintf(stderr, "--record failed\n");
        replay_frame_inputs(&rp, &w);
        if (track_proc && f >= track_from && f <= track_to)
            printf("track %ld: x %u y %u z %u mode %u inring %u\n", f, (unsigned)gsp_read(&w.cpu, track_proc + 0x110, 16),
                   (unsigned)gsp_read(&w.cpu, track_proc + 0x130, 16), (unsigned)gsp_read(&w.cpu, track_proc + 0x150, 16),
                   (unsigned)gsp_read(&w.cpu, track_proc + 0x5C0, 16), (unsigned)gsp_read(&w.cpu, track_proc + 0x560, 16));
        for (int k = 0; k < npokeat; k++)
            if (f >= pokeat[k].at && f <= pokeat[k].to)
                gsp_write(&w.cpu, pokeat[k].addr, 16, pokeat[k].val);
        for (int k = 0; k < npokes; k++)             /* a 16-bit game variable, every frame from FRAME on */
            if (f >= pokes[k].from)
                gsp_write(&w.cpu, pokes[k].addr, 16, pokes[k].val);
        for (int k = 0; k < nmodsw; k++)
            if (f == modsw[k].at) {
                char err[128] = "";
                if (modsw[k].on) {
                    if (!mods_enable(&w, modsw[k].spec, err, sizeof err))
                        fprintf(stderr, "--mod-on %s: %s\n", modsw[k].spec, err);
                } else if (mods_find(modsw[k].spec))
                    mods_remove(&w, mods_find(modsw[k].spec));
            }
        int ok_frame = wolf_frame(&w);
        {
            static int prev_gs = -1;
            if (getenv("WWF_RESETS") && w.gamstate == 0 && prev_gs > 0) {
                printf("frame %ld: GAMSTATE %d -> 0, last dispatches (newest first):", f, prev_gs);
                for (unsigned b = 0; b < 60; b++) {
                    uint32_t o = 0, pc = wolf_trace_pc(&w, b);
                    const char *n = pc ? wolf_symbol(gen, pc, &o) : NULL;
                    printf(" %s+%X", n ? n : "?", o);
                }
                printf("\n");
            }
            prev_gs = w.gamstate;
        }
        if (!ok_frame) {
            uint32_t off = 0;
            const char *sym = wolf_symbol(gen, w.cpu.fault_pc, &off);
            fprintf(stderr, "frame %ld: %s at %08X (%s+0x%X)\n", f,
                    w.cpu.fault ? w.cpu.fault : "stopped", w.cpu.fault_pc, sym ? sym : "?", off);
            for (int k = 16; k >= 1; k--) {
                uint32_t pc = w.cpu.hist[(w.cpu.hist_pos - (unsigned)k) & 63];
                uint32_t o = 0;
                const char *n = wolf_symbol(gen, pc, &o);
                fprintf(stderr, "   <- %08X %s+0x%X\n", pc, n ? n : "?", o);
            }
#ifdef GSP_TRACE
            {
                extern uint32_t gsp_trace_ring[4096];
                extern unsigned gsp_trace_pos;
                for (int k = 48; k >= 1; k--) {
                    uint32_t pc = gsp_trace_ring[(gsp_trace_pos - (unsigned)k) & 4095];
                    uint32_t o = 0;
                    const char *n = wolf_symbol(gen, pc, &o);
                    fprintf(stderr, "   insn %08X %s+0x%X\n", pc, n ? n : "?", o);
                }
            }
#endif
            fprintf(stderr, "   regs:");
            for (int r = 0; r < 16; r++)
                fprintf(stderr, " a%d=%08X", r, w.cpu.r[r]);
            fprintf(stderr, "\n");
            rc = 1;
            break;
        }
        if (w.snd) {
            /* the Wolf Unit runs at about 54.7 frames per second */
            static int16_t buf[2 * RATE / 50];
            mix_acc += RATE / 54.7;
            int n = (int)mix_acc;
            mix_acc -= n;
            snd_mix(w.snd, buf, n);
            if (wav)
                fwrite(buf, 4, (size_t)n, wav);
            wav_frames += (unsigned long)n;
        }
        if (f == draws_at) {      /* --draws FRAME: what the shown page holds */
            int n;
            const wolf_draw *dl = wolf_shown_draws(&w, &n);
            printf("-- frame %ld: %d images on the shown page (name x y w h, + = high-res override)\n", f, n);
            for (int k = 0; k < n; k++)
                printf("   %-12s %5d %5d %4d %4d%s%s\n", dl[k].name[0] ? dl[k].name : "(fill)", dl[k].x, dl[k].y,
                       dl[k].w, dl[k].h, dl[k].hd ? " +" : "", dl[k].background ? " bg" : "");
        }
        if (shots && f >= start && every > 0 && (f - start) % every == 0)
            shot(&w, shots, (unsigned long)f);
        if (profile && f % profile == 0) {
            printf("GAMSTATE=%d display_row=%d\n", (int16_t)gsp_read(&w.cpu, gamstate, 16), w.display_row);
            prof_dump(f);
        }
    }
    int gs = (int16_t)gsp_read(&w.cpu, gamstate, 16);
    if (w.snd) {
        snd_stats st = snd_get_stats(w.snd);
        printf("sound: %lu commands, %lu sounds started, %lu unknown codes\n", st.commands,
               st.started, st.unknown);
        if (expect_sounds >= 0 && (long)st.started < expect_sounds) {
            fprintf(stderr, "only %lu sounds started, expected at least %ld\n", st.started,
                    expect_sounds);
            rc = 1;
        }
        if (wav) {
            uint32_t data = (uint32_t)(wav_frames * 4), riff = data + 36;
            uint8_t b[4];
            fseek(wav, 4, SEEK_SET);
            for (int k = 0; k < 4; k++) b[k] = (uint8_t)(riff >> (8 * k));
            fwrite(b, 1, 4, wav);
            fseek(wav, 40, SEEK_SET);
            for (int k = 0; k < 4; k++) b[k] = (uint8_t)(data >> (8 * k));
            fwrite(b, 1, 4, wav);
            fclose(wav);
        }
        snd_close(w.snd);
        w.snd = NULL;
    }
    for (int k = 0; k < npeek; k++)
        printf("peek %08X = %08X\n", peek[k], gsp_read(&w.cpu, peek[k], 32));
    if (expect != -9999 && rc == 0 && gs != expect) {
        fprintf(stderr, "GAMSTATE is %d, expected %d\n", gs, expect);
        rc = 1;
    }
    uint32_t off = 0;
    const char *sym = wolf_symbol(gen, w.cpu.pc, &off);
    printf("GAMSTATE %d, frames %llu, instructions %llu, pc %08X (%s+0x%X), unmapped DMA %u\n",
           gs, (unsigned long long)w.frames, (unsigned long long)w.cpu.executed, w.cpu.pc,
           sym ? sym : "?", off, w.unmapped_count);
    wolf_free(&w);
    return rc;
}

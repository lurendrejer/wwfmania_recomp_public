/*
 * Mod layer: registry, hook order, error paths, and (given the generated
 * directory) that the symbol the referee mod switches on exists, and the generated background palettes.
 * Usage: test_mods [gen_dir]
 */
#include <stdio.h>
#include <string.h>

#include "wolf/replay.h"
#include "wolf/wolf.h"

static int failures;
#define CHECK(c) \
    do { \
        if (!(c)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            failures++; \
        } \
    } while (0)

static char trace[64];
static void note(char c)
{
    size_t n = strlen(trace);
    if (n + 1 < sizeof trace) {
        trace[n] = c;
        trace[n + 1] = 0;
    }
}

static int a_init(wolf *w, void **st, char *err, size_t n)
{
    (void)w; (void)err; (void)n;
    *st = (void *)"A";
    note('a');
    return 1;
}
static void a_begin(wolf *w, void *st) { (void)w; note(((const char *)st)[0] == 'A' ? 'b' : '?'); }
static void a_end(wolf *w, void *st) { (void)w; (void)st; note('e'); }
static void a_down(wolf *w, void *st) { (void)w; (void)st; note('d'); }
static int b_init(wolf *w, void **st, char *err, size_t n)
{
    (void)w; (void)st;
    snprintf(err, n, "no");
    return 0;
}
static void c_down(wolf *w, void *st) { (void)w; (void)st; note('c'); }

static const wwf_mod mod_a = {"a", "test", a_init, a_begin, a_end, a_down, 0, 0, 0, NULL, 0};
static const wwf_mod mod_b = {"b", "refuses", b_init, NULL, NULL, NULL, 0, 0, 0, NULL, 0};
static const wwf_mod mod_c = {"c", "only shutdown", NULL, NULL, NULL, c_down, 0, 0, 0, NULL, 0};

static wolf w; /* zero-initialized: the hooks never touch the CPU */

static void quiet(const char *msg, void *user)
{
    (void)msg;
    (void)user;
}

int main(int argc, char **argv)
{
    char err[256];

    /* registry: referee is built in (unless -DWWF_MODS=OFF) */
    const wwf_mod *const *list = mods_builtin();
    CHECK(list != NULL);
    int have_ref = mods_find("referee") != NULL;
    CHECK(mods_find("no-such-mod") == NULL);
    CHECK(!mods_enable(&w, "no-such-mod", err, sizeof err) && strstr(err, "unknown"));

    /* hook order, state pointer, reverse shutdown, no-hook mods */
    CHECK(mods_add(&w, &mod_a, err, sizeof err));
    CHECK(mods_add(&w, &mod_c, err, sizeof err));
    CHECK(!mods_add(&w, &mod_a, err, sizeof err) && strstr(err, "already"));
    CHECK(!mods_add(&w, &mod_b, err, sizeof err) && !strcmp(err, "no"));
    CHECK(w.nmods == 2);
    mods_frame_begin(&w);
    mods_frame_end(&w);
    mods_shutdown(&w);
    CHECK(!strcmp(trace, "abecd") && w.nmods == 0);

    /* switching off one mod in the middle: its shutdown runs, the rest stays */
    trace[0] = 0;
    CHECK(mods_add(&w, &mod_a, err, sizeof err) && mods_add(&w, &mod_c, err, sizeof err));
    CHECK(mods_is_enabled(&w, &mod_a) && mods_is_enabled(&w, &mod_c));
    mods_remove(&w, &mod_a);
    CHECK(!mods_is_enabled(&w, &mod_a) && mods_is_enabled(&w, &mod_c) && w.nmods == 1);
    CHECK(!strcmp(trace, "ad"));
    mods_shutdown(&w);
    trace[0] = 0;

    /* limit */
    for (int i = 0; i < WWF_MAX_MODS; i++)
        w.mods[w.nmods++] = &mod_c;
    CHECK(!mods_add(&w, &mod_a, err, sizeof err) && strstr(err, "too many"));
    w.nmods = 0;

    if (have_ref) {
        /* without the generated tables the referee refuses to start, cleanly */
        snprintf(w.gen_dir, sizeof w.gen_dir, "%s", "/nonexistent-gen-dir");
        CHECK(!mods_enable(&w, "referee", err, sizeof err) && err[0] && w.nmods == 0);
        if (argc > 1) {      /* with them, the symbol the mod switches on is there */
            snprintf(w.gen_dir, sizeof w.gen_dir, "%s", argv[1]);
            CHECK(wolf_symbol_addr(&w, "ref_enabled") != 0);
        }
    }

    /* a mod's number: clamped, per mod, kept while enabled */
    {
        static const wwf_mod mod_n = {"n", "has a number", NULL, NULL, NULL, NULL, 1, 4, 2, "N", 0};
        CHECK(mods_arg(&w, &mod_n) == 2);          /* not enabled: its default */
        CHECK(mods_add(&w, &mod_n, err, sizeof err));
        mods_set_arg(&w, &mod_n, 9);
        CHECK(mods_arg(&w, &mod_n) == 4);
        mods_set_arg(&w, &mod_n, 0);
        CHECK(mods_arg(&w, &mod_n) == 1);
        mods_remove(&w, &mod_n);
        CHECK(mods_arg(&w, &mod_n) == 2 && w.nmods == 0);
    }

    if (mods_find("disco")) {   /* colours turned only between frame_end and the next frame_begin */
        const wwf_mod *d = mods_find("disco");
        CHECK(mods_add(&w, d, err, sizeof err));
        mods_set_arg(&w, d, 20);
        w.v.colram[5] = 0x7C00;                          /* red */
        w.v.colram[6] = 0x4210;                          /* grey */
        mods_frame_end(&w);                              /* the wheel starts at 0 */
        mods_frame_begin(&w);
        mods_frame_end(&w);
        CHECK(w.v.colram[5] != 0x7C00 && w.v.colram[6] == 0x4210);
        mods_frame_begin(&w);
        CHECK(w.v.colram[5] == 0x7C00);
        mods_frame_end(&w);
        mods_remove(&w, d);
        CHECK(w.v.colram[5] == 0x7C00 && w.nmods == 0);
    }

    if (mods_find("bamfire")) {
        snprintf(w.gen_dir, sizeof w.gen_dir, "%s", "/nonexistent-gen-dir");
        CHECK(!mods_enable(&w, "bamfire", err, sizeof err) && err[0] && w.nmods == 0);
        if (argc > 1) {
            snprintf(w.gen_dir, sizeof w.gen_dir, "%s", argv[1]);
            CHECK(wolf_symbol_addr(&w, "bowl_enabled") != 0);
            CHECK(wolf_symbol_addr(&w, "BAMBOWL01") != 0);
        }
    }

    if (mods_find("easymoves")) {
        snprintf(w.gen_dir, sizeof w.gen_dir, "%s", "/nonexistent-gen-dir");
        CHECK(!mods_enable(&w, "easymoves", err, sizeof err) && err[0] && w.nmods == 0);
        if (argc > 1) {
            snprintf(w.gen_dir, sizeof w.gen_dir, "%s", argv[1]);
            CHECK(wolf_symbol_addr(&w, "easy_enabled") != 0);
            CHECK(wolf_symbol_addr(&w, "easy_sync_kick") != 0);
        }
    }

    if (mods_find("moredrones")) {
        snprintf(w.gen_dir, sizeof w.gen_dir, "%s", "/nonexistent-gen-dir");
        CHECK(!mods_enable(&w, "moredrones", err, sizeof err) && err[0] && w.nmods == 0);
        if (argc > 1) {
            snprintf(w.gen_dir, sizeof w.gen_dir, "%s", argv[1]);
            CHECK(wolf_symbol_addr(&w, "more_drones") != 0);
            CHECK(wolf_symbol_addr(&w, "more_orig") != 0);
        }
    }

    if (mods_find("morebuddies")) {
        snprintf(w.gen_dir, sizeof w.gen_dir, "%s", "/nonexistent-gen-dir");
        CHECK(!mods_enable(&w, "morebuddies", err, sizeof err) && err[0] && w.nmods == 0);
        if (argc > 1) {
            snprintf(w.gen_dir, sizeof w.gen_dir, "%s", argv[1]);
            CHECK(wolf_symbol_addr(&w, "more_buddies") != 0);
        }
    }

    if (mods_find("fourplayer")) {
        snprintf(w.gen_dir, sizeof w.gen_dir, "%s", "/nonexistent-gen-dir");
        CHECK(!mods_enable(&w, "fourplayer", err, sizeof err) && err[0] && w.nmods == 0);
        if (argc > 1) {
            snprintf(w.gen_dir, sizeof w.gen_dir, "%s", argv[1]);
            CHECK(wolf_symbol_addr(&w, "fp_on") != 0);
            CHECK(wolf_symbol_addr(&w, "fp_in") != 0);
            CHECK(wolf_symbol_addr(&w, "fp_input") != 0);
            CHECK(wolf_symbol_addr(&w, "fp_select") != 0);
            CHECK(wolf_symbol_addr(&w, "fp_pick3") != 0 && wolf_symbol_addr(&w, "fp_pick4") != 0);
            CHECK(wolf_symbol_addr(&w, "fp_wait") != 0);
            CHECK(wolf_symbol_addr(&w, "fp_showmug") != 0 && wolf_symbol_addr(&w, "fp_scale") != 0);
            CHECK(wolf_symbol_addr(&w, "fp_mug1") != 0 && wolf_symbol_addr(&w, "fp_name1") != 0);
            CHECK(wolf_symbol_addr(&w, "fp_mtr_init") != 0 && wolf_symbol_addr(&w, "fp_mtr_update") != 0);
            CHECK(wolf_symbol_addr(&w, "GAMSTATE") != 0 && wolf_symbol_addr(&w, "PSTATUS") != 0);
        }
    }

    if (mods_find("sansring")) {
        snprintf(w.gen_dir, sizeof w.gen_dir, "%s", "/nonexistent-gen-dir");
        CHECK(!mods_enable(&w, "sansring", err, sizeof err) && err[0] && w.nmods == 0);
        if (argc > 1) {
            snprintf(w.gen_dir, sizeof w.gen_dir, "%s", argv[1]);
            CHECK(wolf_symbol_addr(&w, "sans_on") != 0);
        }
    }

    if (mods_find("bottombuckles")) {
        snprintf(w.gen_dir, sizeof w.gen_dir, "%s", "/nonexistent-gen-dir");
        CHECK(!mods_enable(&w, "bottombuckles", err, sizeof err) && err[0] && w.nmods == 0);
        if (argc > 1) {
            snprintf(w.gen_dir, sizeof w.gen_dir, "%s", argv[1]);
            CHECK(wolf_symbol_addr(&w, "bb_on") != 0);
            CHECK(wolf_symbol_addr(&w, "bb_check") != 0);
        }
    }

    if (mods_find("outsidedive")) {
        snprintf(w.gen_dir, sizeof w.gen_dir, "%s", "/nonexistent-gen-dir");
        CHECK(!mods_enable(&w, "outsidedive", err, sizeof err) && err[0] && w.nmods == 0);
        if (argc > 1) {
            snprintf(w.gen_dir, sizeof w.gen_dir, "%s", argv[1]);
            CHECK(wolf_symbol_addr(&w, "dive_on") != 0);
            CHECK(wolf_symbol_addr(&w, "dive_tick") != 0 && wolf_symbol_addr(&w, "dive_hit") != 0);
        }
    }

    if (mods_find("doinkpie")) {
        snprintf(w.gen_dir, sizeof w.gen_dir, "%s", "/nonexistent-gen-dir");
        CHECK(!mods_enable(&w, "doinkpie", err, sizeof err) && err[0] && w.nmods == 0);
        if (argc > 1) {
            snprintf(w.gen_dir, sizeof w.gen_dir, "%s", argv[1]);
            CHECK(wolf_symbol_addr(&w, "pie_enabled") != 0);
            CHECK(wolf_symbol_addr(&w, "dnk_pie_flight") != 0);
        }
    }

    if (mods_find("training")) {
        snprintf(w.gen_dir, sizeof w.gen_dir, "%s", "/nonexistent-gen-dir");
        CHECK(!mods_enable(&w, "training", err, sizeof err) && err[0] && w.nmods == 0);
        if (argc > 1) {
            snprintf(w.gen_dir, sizeof w.gen_dir, "%s", argv[1]);
            CHECK(wolf_symbol_addr(&w, "life_data") != 0);
            CHECK(wolf_symbol_addr(&w, "match_time") != 0);
        }
    }

    {   /* small mods: (name, symbol the machine needs) */
        static const char *const small[][2] = {{"rounds", "rounds_needed"}, {"cpuskill", "cpu_skill_add"},
                                               {"damage", "speed_adjustment"}, {"matchtime", "match_time"},
                                               {"nodebris", "no_debris"},
                                               {"brokenrecord", "brec_line"}, {"wrongnames", "wn_name"},
                                               {"chatterbox", "chatter_on"}, {"moongravity", "mg_gravity"},
                                               {"chair", "chair_enabled"}, {"chair", "CHAIR_SWING"}, {"chair", "chair_control"}};
        for (size_t i = 0; i < sizeof small / sizeof small[0]; i++) {
            if (!mods_find(small[i][0]))
                continue;
            snprintf(w.gen_dir, sizeof w.gen_dir, "%s", "/nonexistent-gen-dir");
            CHECK(!mods_enable(&w, small[i][0], err, sizeof err) && err[0] && w.nmods == 0);
            if (argc > 1) {
                snprintf(w.gen_dir, sizeof w.gen_dir, "%s", argv[1]);
                CHECK(wolf_symbol_addr(&w, small[i][1]) != 0);
            }
        }
    }

    if (argc > 1) {   /* the generated background palettes (tools/gsp/genimg.py): one line per .BDD image */
        char path[1024], line[2048];
        snprintf(path, sizeof path, "%s/bddpal.txt", argv[1]);
        FILE *f = fopen(path, "r");
        int n = 0, bad = 0;
        CHECK(f != NULL);
        while (f && fgets(line, sizeof line, f)) {
            char file[32];
            int index, ncolors;
            if (line[0] == '#')
                continue;
            if (sscanf(line, "%31s %d %d", file, &index, &ncolors) != 3 || ncolors <= 0 || ncolors > 256)
                bad++;
            n++;
        }
        if (f)
            fclose(f);
        CHECK(n == 203 && bad == 0);   /* 95 + 42 + 15 + 9 + 40 + 2 images in the six .BDD files */
    }

    if (argc > 1) {   /* wrestler frames drawn with another palette than their own (genimg.py: gen/drawpal.txt) */
        char path[1024], line[256];
        snprintf(path, sizeof path, "%s/drawpal.txt", argv[1]);
        FILE *f = fopen(path, "r");
        int n = 0, bam = 0;
        CHECK(f != NULL);
        while (f && fgets(line, sizeof line, f)) {
            if (line[0] == '#' || line[0] == 'A')   /* A...: Adam Bomb's (mods/adambomb), not in the original */
                continue;
            n++;
            bam += strcmp(line, "B1TT5Z02 BAMBLU_P\n") == 0;   /* marked with the fire trail's BAMFRE_P */
        }
        if (f)
            fclose(f);
        CHECK(n == 12 && bam == 1);   /* one or two per walking library: player 2's colours, trails ... */
    }

    if (argc > 2) {   /* ... and the game maps a true-colour override of one onto the palette it is drawn with */
        char lod[1024];
        snprintf(lod, sizeof lod, "%s/BAM.LOD", argv[2]);
        FILE *f = fopen(lod, "r");
        if (f) {
            static wolf g;
            fclose(f);
            CHECK(wolf_init(&g, argv[1], argv[2], NULL, 1, NULL, quiet));
            int seen = 0;
            for (int i = 0; i < g.nimgs; i++) {
                const wolf_romimg *r = &g.imgs[i];
                if (strcmp(r->name, "B1TT5Z02") == 0 || strcmp(r->name, "B1TT5Z03") == 0) {
                    seen++;
                    CHECK(r->img && r->img->pal && strcmp(r->img->pal->name, "BAMBLU_P") == 0);
                }
            }
            CHECK(seen == 2);
            wolf_free(&g);
        }
    }

    {   /* input recording: record, write, read, play back */
        replay rp;
        const char *path = "test_replay.rec";

        memset(&rp, 0, sizeof rp);
        replay_start_record(&rp);
        for (int i = 0; i < 5000; i++) {                    /* past the first buffer growth */
            w.player[0] = (uint8_t)i;
            w.player[3] = (uint8_t)(i >> 3);
            w.coin_bits = (uint16_t)(i * 7);
            w.extra_player[1] = (uint16_t)(i * 3);
            replay_frame_inputs(&rp, &w);
        }
        CHECK(rp.count == 5000 && replay_stop_record(&rp, path) && rp.mode == REPLAY_IDLE);
        CHECK(replay_start_play(&rp, path) && rp.count == 5000 && rp.mode == REPLAY_PLAYING);
        for (int i = 0; i < 5000; i++) {
            memset(w.player, 0, sizeof w.player);
            w.coin_bits = 0;
            w.extra_player[1] = 0;
            replay_frame_inputs(&rp, &w);
            CHECK(w.player[0] == (uint8_t)i && w.player[3] == (uint8_t)(i >> 3) && w.coin_bits == (uint16_t)(i * 7) &&
                  w.extra_player[1] == (uint16_t)(i * 3));
            if (failures)
                break;
        }
        replay_frame_inputs(&rp, &w);                       /* one past the end */
        CHECK(rp.mode == REPLAY_IDLE);
        CHECK(!replay_start_play(&rp, "/nonexistent/x.rec"));
        replay_free(&rp);
        remove(path);
    }

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("mods: ok\n");
    return 0;
}

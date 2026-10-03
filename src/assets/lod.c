#include "lod.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../util/fsutil.h"

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s))
        s++;
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1]))
        s[--n] = 0;
    return s;
}

static void set_str(char *dst, size_t len, const char *src)
{
    snprintf(dst, len, "%s", src);
}

static lod_entry *add_entry(lod_script *lod, int *cap)
{
    if (lod->nentries == *cap) {
        int ncap = *cap ? *cap * 2 : 32;
        lod_entry *ne = realloc(lod->entries, (size_t)ncap * sizeof *ne);
        if (!ne)
            return NULL;
        lod->entries = ne;
        *cap = ncap;
    }
    lod_entry *e = &lod->entries[lod->nentries++];
    memset(e, 0, sizeof *e);
    return e;
}

static int add_name(lod_entry *e, const char *name)
{
    char **nn = realloc(e->names, (size_t)(e->nnames + 1) * sizeof *nn);
    if (!nn)
        return 0;
    e->names = nn;
    size_t len = strlen(name);
    char *copy = malloc(len + 1);
    if (!copy)
        return 0;
    str_upper_copy(copy, len + 1, name);
    e->names[e->nnames++] = copy;
    return 1;
}

static unsigned toggle_bit(char c)
{
    switch (toupper((unsigned char)c)) {
    case 'Z': return LOD_T_Z;
    case 'C': return LOD_T_C;
    case 'P': return LOD_T_P;
    case 'M': return LOD_T_M;
    case 'B': return LOD_T_B;
    default: return 0;
    }
}

int lod_load(lod_script *lod, const char *path, char *err, unsigned long err_len)
{
    memset(lod, 0, sizeof *lod);
    set_str(lod->path, sizeof lod->path, path);

    size_t size;
    uint8_t *data = fs_read_file(path, &size);
    if (!data) {
        if (err && err_len)
            snprintf(err, err_len, "%s: cannot read file", path);
        return 0;
    }

    int cap = 0, lineno = 0;
    unsigned toggles = 0;
    char asm_table[64] = "", ihdr[160] = "";
    int last_img = -1; /* index, entries may move on realloc */
    int ok = 1;

    char *text = (char *)data;
    char *end = text + size;
    while (text < end && ok) {
        char *nl = memchr(text, '\n', (size_t)(end - text));
        size_t len = nl ? (size_t)(nl - text) : (size_t)(end - text);
        char line[1024];
        if (len >= sizeof line)
            len = sizeof line - 1;
        memcpy(line, text, len);
        line[len] = 0;
        text = nl ? nl + 1 : end;
        lineno++;

        char *s = trim(line);
        if (!*s || *s == ';')
            continue;

        if (strncmp(s, "--->", 4) == 0) {
            if (last_img < 0)
                continue; /* list without a library: nothing to restrict */
            char *tok = s + 4;
            while (tok && ok) {
                char *comma = strchr(tok, ',');
                if (comma)
                    *comma = 0;
                char *name = trim(tok);
                if (*name)
                    ok = add_name(&lod->entries[last_img], name);
                tok = comma ? comma + 1 : NULL;
            }
            continue;
        }

        if (strlen(s) >= 4 && s[3] == '>') {
            char *arg = trim(s + 4);
            char dir[4] = {s[0], s[1], s[2], 0};
            if (str_ieq(dir, "ASM")) {
                set_str(asm_table, sizeof asm_table, arg);
            } else if (str_ieq(dir, "BBB") || str_ieq(dir, "FRM")) {
                lod_entry *e = add_entry(lod, &cap);
                if (!e) {
                    ok = 0;
                    break;
                }
                e->kind = str_ieq(dir, "BBB") ? LOD_BBB : LOD_FRM;
                set_str(e->file, sizeof e->file, arg);
                e->toggles = toggles;
                set_str(e->asm_table, sizeof e->asm_table, asm_table);
                set_str(e->ihdr, sizeof e->ihdr, ihdr);
                e->line = lineno;
            } else if (toupper((unsigned char)s[1]) == 'O' && toupper((unsigned char)s[2]) == 'N') {
                toggles |= toggle_bit(s[0]);
            } else if (toupper((unsigned char)s[1]) == 'O' && toupper((unsigned char)s[2]) == 'F') {
                toggles &= ~toggle_bit(s[0]);
            }
            /* ***>, GLO> and unknown directives carry nothing the port needs. */
            continue;
        }

        if (strncmp(s, "IHDR", 4) == 0 && isspace((unsigned char)s[4])) {
            set_str(ihdr, sizeof ihdr, trim(s + 4));
            continue;
        }

        if (str_iendswith(s, ".img")) {
            lod_entry *e = add_entry(lod, &cap);
            if (!e) {
                ok = 0;
                break;
            }
            e->kind = LOD_IMG;
            set_str(e->file, sizeof e->file, fs_basename(s));
            e->toggles = toggles;
            set_str(e->asm_table, sizeof e->asm_table, asm_table);
            set_str(e->ihdr, sizeof e->ihdr, ihdr);
            e->line = lineno;
            last_img = lod->nentries - 1;
            continue;
        }
        /* Anything else is ignored; the scripts contain no other forms. */
    }

    free(data);
    if (!ok) {
        lod_free(lod);
        if (err && err_len)
            snprintf(err, err_len, "%s: out of memory", path);
        return 0;
    }
    return 1;
}

void lod_free(lod_script *lod)
{
    for (int i = 0; i < lod->nentries; i++) {
        for (int n = 0; n < lod->entries[i].nnames; n++)
            free(lod->entries[i].names[n]);
        free(lod->entries[i].names);
    }
    free(lod->entries);
    lod->entries = NULL;
    lod->nentries = 0;
}

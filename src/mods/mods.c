#include "mods/mods.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wolf/wolf.h"

/* Defined by the registry CMake generates from mods/ (mods_builtin.c). */
extern const wwf_mod *const wwf_builtin_mods[];

const wwf_mod *const *mods_builtin(void)
{
    return wwf_builtin_mods;
}

const wwf_mod *mods_find(const char *name)
{
    for (const wwf_mod *const *m = wwf_builtin_mods; *m; m++)
        if (strcmp((*m)->name, name) == 0)
            return *m;
    return NULL;
}

int mods_add(wolf *w, const wwf_mod *m, char *err, size_t err_len)
{
    for (int i = 0; i < w->nmods; i++) {
        if (w->mods[i] == m) {
            snprintf(err, err_len, "mod '%s' is already enabled", m->name);
            return 0;
        }
    }
    if (w->nmods >= WWF_MAX_MODS) {
        snprintf(err, err_len, "too many mods (max %d)", WWF_MAX_MODS);
        return 0;
    }
    void *state = NULL;
    err[0] = 0;
    if (m->init && !m->init(w, &state, err, err_len)) {
        if (!err[0])
            snprintf(err, err_len, "mod '%s' failed to start", m->name);
        return 0;
    }
    w->mods[w->nmods] = m;
    w->mod_state[w->nmods] = state;
    w->mod_arg[w->nmods] = m->arg_default;
    w->nmods++;
    return 1;
}

int mods_arg(const wolf *w, const wwf_mod *m)
{
    for (int i = 0; i < w->nmods; i++)
        if (w->mods[i] == m)
            return w->mod_arg[i];
    return m->arg_default;
}

void mods_set_arg(wolf *w, const wwf_mod *m, int value)
{
    if (m->arg_max <= m->arg_min)
        return;
    if (value < m->arg_min)
        value = m->arg_min;
    if (value > m->arg_max)
        value = m->arg_max;
    for (int i = 0; i < w->nmods; i++)
        if (w->mods[i] == m)
            w->mod_arg[i] = value;
}

int mods_enable(wolf *w, const char *spec, char *err, size_t err_len)
{
    char name[64];
    snprintf(name, sizeof name, "%s", spec);
    char *eq = strchr(name, '=');
    if (eq)
        *eq = 0;
    const wwf_mod *m = mods_find(name);
    if (!m) {
        snprintf(err, err_len, "unknown mod '%s' (not built in; see --list-mods)", name);
        return 0;
    }
    if (!mods_add(w, m, err, err_len))
        return 0;
    if (eq)
        mods_set_arg(w, m, atoi(eq + 1));
    return 1;
}

int mods_is_enabled(const wolf *w, const wwf_mod *m)
{
    for (int i = 0; i < w->nmods; i++)
        if (w->mods[i] == m)
            return 1;
    return 0;
}

void mods_remove(wolf *w, const wwf_mod *m)
{
    for (int i = 0; i < w->nmods; i++) {
        if (w->mods[i] != m)
            continue;
        if (m->shutdown)
            m->shutdown(w, w->mod_state[i]);
        for (int j = i; j + 1 < w->nmods; j++) {
            w->mods[j] = w->mods[j + 1];
            w->mod_state[j] = w->mod_state[j + 1];
            w->mod_arg[j] = w->mod_arg[j + 1];
        }
        w->nmods--;
        return;
    }
}

void mods_frame_begin(wolf *w)
{
    for (int i = 0; i < w->nmods; i++)
        if (w->mods[i]->frame_begin)
            w->mods[i]->frame_begin(w, w->mod_state[i]);
}

void mods_frame_end(wolf *w)
{
    for (int i = 0; i < w->nmods; i++)
        if (w->mods[i]->frame_end)
            w->mods[i]->frame_end(w, w->mod_state[i]);
}

void mods_shutdown(wolf *w)
{
    for (int i = w->nmods - 1; i >= 0; i--)
        if (w->mods[i]->shutdown)
            w->mods[i]->shutdown(w, w->mod_state[i]);
    w->nmods = 0;
}

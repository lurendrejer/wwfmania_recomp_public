#include "util/dataextract.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir((p), 0777)
#endif

#include "util/fsutil.h"

/* Makes the directories above the file `path` (which uses '/'). */
static void make_parents(const char *path)
{
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            MKDIR(tmp);
            *p = '/';
        }
}

static int write_file(const char *path, const uint8_t *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return 0;
    int ok = n == 0 || fwrite(data, 1, n, f) == n;
    return fclose(f) == 0 && ok;
}

static void fail(char *err, size_t n, const char *what, const char *name)
{
    if (err && n)
        snprintf(err, n, "%s %.200s", what, name);
}

int data_extract(const data_source *src, const char *dest_dir, int *copied, char *err, size_t err_len)
{
    size_t nl = 0, ns = 0;
    char path[1024];
    uint8_t *list = src->read(src->user, "filelist.txt", &nl);
    if (copied)
        *copied = 0;
    if (!list)
        return 0;
    uint8_t *stamp = src->read(src->user, "data.stamp", &ns);
    fs_join(path, sizeof path, dest_dir, "data.stamp");
    size_t old_n = 0;
    uint8_t *old = fs_read_file(path, &old_n);
    if (stamp && old && old_n == ns && memcmp(stamp, old, ns) == 0) {
        free(list);
        free(stamp);
        free(old);
        return 1;
    }
    free(old);
    MKDIR(dest_dir);

    int n = 0, rc = 2, total = 0;
    char *text = malloc(nl + 1);
    if (!text) {
        free(list);
        free(stamp);
        fail(err, err_len, "out of memory", "");
        return -1;
    }
    memcpy(text, list, nl);
    text[nl] = 0;
    free(list);
    for (const char *q = text; *q; q++)                 /* the number of files, for the progress */
        if (*q == '\n')
            total++;
    if (text[0] && text[strlen(text) - 1] != '\n')
        total++;
    for (char *line = strtok(text, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        if (src->progress)
            src->progress(src->user, n, total);
        if (!*line || strstr(line, ".."))
            continue;
        size_t sz = 0;
        uint8_t *data = src->read(src->user, line, &sz);
        char out[1024];
        if (!data) {
            fail(err, err_len, "cannot read from the bundle:", line);
            rc = -1;
            break;
        }
        snprintf(out, sizeof out, "%s/%s", dest_dir, line);
        make_parents(out);
        int ok = write_file(out, data, sz);
        free(data);
        if (!ok) {
            fail(err, err_len, "cannot write", out);
            rc = -1;
            break;
        }
        n++;
    }
    free(text);
    if (rc == 2 && stamp) {
        fs_join(path, sizeof path, dest_dir, "data.stamp");
        if (!write_file(path, stamp, ns)) {
            fail(err, err_len, "cannot write", path);
            rc = -1;
        }
    }
    free(stamp);
    if (src->progress)
        src->progress(src->user, n, total);
    if (copied)
        *copied = n;
    return rc;
}

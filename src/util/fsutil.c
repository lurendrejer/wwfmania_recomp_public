#include "fsutil.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

uint8_t *fs_read_file(const char *path, size_t *size_out)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long len = ftell(f);
    if (len < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    uint8_t *buf = malloc(len ? (size_t)len : 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (len && fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *size_out = (size_t)len;
    return buf;
}

int fs_file_exists(const char *path)
{
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
#endif
}

int fs_join(char *out, size_t out_len, const char *dir, const char *name)
{
    size_t dl = strlen(dir);
    int need_sep = dl > 0 && dir[dl - 1] != '/' && dir[dl - 1] != '\\';
    int n = snprintf(out, out_len, "%s%s%s", dir, need_sep ? "/" : "", name);
    return n >= 0 && (size_t)n < out_len;
}

const char *fs_basename(const char *path)
{
    const char *base = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\')
            base = p + 1;
    return base;
}

int str_ieq(const char *a, const char *b)
{
    while (*a && *b) {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

int str_iendswith(const char *s, const char *suffix)
{
    size_t sl = strlen(s), xl = strlen(suffix);
    return sl >= xl && str_ieq(s + sl - xl, suffix);
}

void str_upper_copy(char *dst, size_t dst_len, const char *src)
{
    size_t i = 0;
    if (!dst_len)
        return;
    for (; src[i] && i + 1 < dst_len; i++)
        dst[i] = (char)toupper((unsigned char)src[i]);
    dst[i] = 0;
}

int fs_list_dir(const char *dir, fs_dir_cb cb, void *user)
{
    int count = 0;
#ifdef _WIN32
    char pattern[1024];
    if (!fs_join(pattern, sizeof pattern, dir, "*"))
        return -1;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return -1;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            cb(fd.cFileName, user);
            count++;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d)
        return -1;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.')
            continue;
        cb(e->d_name, user);
        count++;
    }
    closedir(d);
#endif
    return count;
}

struct ci_search {
    const char *want;
    char found[256];
};

static void ci_cb(const char *name, void *user)
{
    struct ci_search *s = user;
    if (!s->found[0] && str_ieq(name, s->want))
        snprintf(s->found, sizeof s->found, "%s", name);
}

int fs_resolve_ci(char *out, size_t out_len, const char *dir, const char *name)
{
    const char *base = fs_basename(name);
    if (fs_join(out, out_len, dir, base) && fs_file_exists(out))
        return 1;
    struct ci_search s;
    s.want = base;
    s.found[0] = 0;
    if (fs_list_dir(dir, ci_cb, &s) < 0 || !s.found[0])
        return 0;
    return fs_join(out, out_len, dir, s.found) && fs_file_exists(out);
}

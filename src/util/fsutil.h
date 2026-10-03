/* Small filesystem helpers shared by the asset loaders. */
#ifndef WWF_FSUTIL_H
#define WWF_FSUTIL_H

#include <stddef.h>
#include <stdint.h>

/* Reads a whole file into a malloc'd buffer. Returns NULL on failure. */
uint8_t *fs_read_file(const char *path, size_t *size_out);

/* Returns nonzero if a regular file exists at path. */
int fs_file_exists(const char *path);

/* Joins dir and name into out (out_len bytes). Returns 0 on overflow. */
int fs_join(char *out, size_t out_len, const char *dir, const char *name);

/*
 * Finds `name` inside `dir` ignoring case (the original DOS scripts use
 * lowercase names, the files on disk are uppercase). Any DOS/Unix directory
 * prefix in `name` is stripped first. Writes the full path to out and
 * returns 1 when found, 0 otherwise.
 */
int fs_resolve_ci(char *out, size_t out_len, const char *dir, const char *name);

/* Returns the part of path after the last '/' or '\\'. */
const char *fs_basename(const char *path);

/* Case-insensitive ASCII compare helpers (portable strcasecmp). */
int str_ieq(const char *a, const char *b);
int str_iendswith(const char *s, const char *suffix);

/* Copies src to dst (dst_len bytes, always terminated), uppercasing ASCII. */
void str_upper_copy(char *dst, size_t dst_len, const char *src);

/* Callback-based directory listing. Returns number of entries visited or -1. */
typedef void (*fs_dir_cb)(const char *name, void *user);
int fs_list_dir(const char *dir, fs_dir_cb cb, void *user);

#endif

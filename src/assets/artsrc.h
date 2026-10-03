/*
 * Where art overrides come from (docs/ASSET_OVERRIDES.md): a directory with
 * loose <NAME>.png files and/or .zip files holding them (at any folder depth
 * inside the zip; only the file name counts), or a single .zip file given
 * instead of the directory. Loose files win over zipped ones, and earlier
 * zips (in listing order) over later ones.
 *
 * Zip entries may be stored or deflated. Zip64 archives, encrypted entries
 * and archives of 2 GB or more are not read (split the set into several
 * zips instead).
 */
#ifndef WWF_ARTSRC_H
#define WWF_ARTSRC_H

#include <stddef.h>
#include <stdint.h>

#include "png_read.h"
#include "../util/fsutil.h"

/* Calls cb with the file name of every override candidate (loose files and
 * zip entries alike, without folders). Returns the number of names or -1 when
 * `src` is neither a directory nor a readable zip. Refreshes the zip index. */
int art_list(const char *src, fs_dir_cb cb, void *user);

/* Where one override file is, resolved (art_locate) so that reading it (art_read) touches no shared state: a
 * worker thread may call art_read while the main thread uses the rest of this interface. */
typedef struct {
    char path[1024];         /* the loose file, or the zip holding the entry */
    char name[96];           /* the entry inside the zip, for messages */
    int zipped;
    uint16_t method;         /* 0 stored, 8 deflate */
    uint32_t csize, usize, offset;
} art_ref;

/* Finds <file> in `src` like art_load_png, without reading it. Main thread only (it keeps the zip index). */
int art_locate(const char *src, const char *file, art_ref *ref, char *err, size_t err_len);

/* Reads and decodes a located file. Safe on any thread, any number at once. */
int art_read(const art_ref *ref, png_indexed *out, char *err, size_t err_len);

/* Loads and decodes <file> (e.g. "D4BK3A06.png", case-insensitive) from
 * `src`. Returns 0 with err set when it is not there or cannot be decoded. */
int art_load_png(const char *src, const char *file, png_indexed *out, char *err, size_t err_len);

/* Drops the cached zip index (the next call reads the zips again). */
void art_forget(void);

#endif

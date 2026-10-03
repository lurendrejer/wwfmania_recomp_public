/*
 * Copies data bundled in an app (Android assets, which are not files the game can fopen) to a directory
 * where they are. The bundle holds "filelist.txt" (relative paths, one per line, '/' separators), "data.stamp"
 * (any text: a version) and the files. Nothing is copied when the destination already holds the same stamp.
 */
#ifndef WWF_DATAEXTRACT_H
#define WWF_DATAEXTRACT_H

#include <stddef.h>
#include <stdint.h>

/* Reads one file of the bundle into a malloc'd buffer (NULL if it is not there). */
typedef struct {
    void *user;
    uint8_t *(*read)(void *user, const char *rel, size_t *size_out);
    /* optional: called before each file with how many are done and how many there are, and once at the end */
    void (*progress)(void *user, int done, int total);
} data_source;

/*
 * Returns -1 on an error (message in err), 0 when the bundle has no filelist.txt (nothing bundled),
 * 1 when the destination is up to date, 2 when files were copied (*copied = how many, if not NULL).
 * The stamp is written last, so an interrupted copy is done again next time.
 */
int data_extract(const data_source *src, const char *dest_dir, int *copied, char *err, size_t err_len);

#endif

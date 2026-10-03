/*
 * Finding the DCS sound ROMs: a directory or a MAME-style .zip holding
 * files that end in .u2, .u3, .u4 and .u5 (WWF WrestleMania names them
 * wwf_music-spch_l1.u2 ... .u5). Each must be 1 MB.
 */
#ifndef WWF_DCS_ROMSET_H
#define WWF_DCS_ROMSET_H

#include <stddef.h>
#include <stdint.h>

#define DCS_NROMS 4

typedef struct {
    uint8_t *data[DCS_NROMS];   /* 1 MB each, u2..u5 */
    uint32_t crc[DCS_NROMS];
    int known;                  /* 1 if every CRC matches the WrestleMania set */
} dcs_romset;

int dcs_romset_load(dcs_romset *rs, const char *path, char *err, size_t err_len);
void dcs_romset_free(dcs_romset *rs);

uint32_t dcs_crc32(const uint8_t *data, size_t len);

#endif

/*
 * Parser for the ".LOD" scripts that told the original ROM builder which
 * images from which IMG libraries go into which generated image table.
 *
 * Line forms (CRLF, case-insensitive):
 *   ; comment
 *   IHDR SIZX:W,SIZY:W,...      header layout for the following images
 *   ***> 5101774,0              ROM start address/bank (ignored by the port)
 *   ASM> adamimg.tbl            generated table the following images go to
 *   GLO> adamimg.glo            generated globals file
 *   ZON> ZOF> CON> COF> ...     builder toggles (Z = zero compression, ...)
 *   BBB> NAME                   background block (.BDB/.BDD)
 *   FRM> NAME                   frame reference
 *   file.img                    include images from this library
 *   ---> NAME,NAME,...          restrict the previous library to these images
 */
#ifndef WWF_LOD_H
#define WWF_LOD_H

typedef enum {
    LOD_IMG,  /* an IMG library, optionally restricted to `names` */
    LOD_BBB,  /* a background block */
    LOD_FRM,  /* a frame reference */
} lod_kind;

/* Builder toggles active when an entry was declared (bit set = ON). */
enum {
    LOD_T_Z = 1 << 0, /* ZON/ZOF: zero (transparent run) compression */
    LOD_T_C = 1 << 1, /* CON/COF */
    LOD_T_P = 1 << 2, /* PON/POF */
    LOD_T_M = 1 << 3, /* MON/MOF */
    LOD_T_B = 1 << 4, /* BON/BOF */
};

typedef struct {
    lod_kind kind;
    char file[128];     /* IMG basename, or the BBB/FRM name */
    char **names;       /* selected image names; NULL/0 = all images */
    int nnames;
    unsigned toggles;
    char asm_table[64]; /* ASM> target in effect ("" if none) */
    char ihdr[160];     /* IHDR spec in effect ("" if none) */
    int line;           /* source line, for diagnostics */
} lod_entry;

typedef struct {
    char path[512];
    lod_entry *entries;
    int nentries;
} lod_script;

/* Parses a LOD file. Returns 0 on I/O failure (err is filled). */
int lod_load(lod_script *lod, const char *path, char *err, unsigned long err_len);
void lod_free(lod_script *lod);

#endif

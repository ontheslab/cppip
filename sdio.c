/* sdio.c - FreHD SD card I/O extension for CPPIP/FPPIP
 *
 * Implements SD: prefix file access via the FreHD hard disk emulator.
 * The FreHD presents an SD card to the Z80 via five fixed I/O ports (0xC2-0xC5,
 * 0xCF) using a command/data protocol derived from FatFS.
 *
 * Port map:
 *   0xC2  DATA2    bidirectional data byte (one byte per IN/OUT)
 *   0xC3  SIZE2    write = payload length; read = response length / version
 *   0xC4  COMMAND2 write command code to trigger operation
 *   0xC5  ERROR2   FatFS error code (read after STATUS ERROR bit set)
 *   0xCF  STATUS   bit7=BUSY (poll), bit3=DRQ (data ready), bit0=ERROR
 *
 * Commands:
 *   0x00  GETVER    detect interface (SIZE2 == 6 means present)
 *   0x03  OPENFILE  open file; SIZE2 = len, data = [mode][name][NUL]
 *   0x04  READFILE  read block; pre-set SIZE2 = 0 (256 bytes)
 *   0x05  WRITEFILE write block; SIZE2 = byte count, then data
 *   0x06  CLOSEFILE close current file (no data)
 *   0x08  OPENDIR   open directory; SIZE2 = len before cmd, data = [path][NUL]
 *   0x09  READDIR   read next FILINFO entry (22 bytes via SIZE2 + DATA2)
 *
 * FILINFO layout (22 bytes):
 *   [0..3] fsize   [4..5] fdate  [6..7] ftime  [8] fattrib  [9..21] fname
 *   fattrib bits: 0x10=DIR  0x08=VOL  0x02=HID
 *   fname: NUL-terminated 8.3 uppercase string (e.g. "FILE.COM\0")
 */

#ifdef FREHD

#include "ppip.h"
#include "sdio.h"
#include "diskio.h"
#include "console.h"
#include "crc.h"

/* ---- Port declarations ---- */
/* SDCC __sfr __at(n): read -> IN A,(n) / write -> OUT (n),A */

__sfr __at(0xC2) s_sd_data;     /* DATA2   */
__sfr __at(0xC3) s_sd_size;     /* SIZE2   */
__sfr __at(0xC4) s_sd_cmd;      /* COMMAND2 */
__sfr __at(0xC5) s_sd_error;    /* ERROR2  */
__sfr __at(0xCF) s_sd_status;   /* STATUS  */

/* ---- Command codes ---- */
#define SD_CMD_GETVER    0x00
#define SD_CMD_OPENFILE  0x03
#define SD_CMD_READFILE  0x04
#define SD_CMD_WRITEFILE 0x05
#define SD_CMD_CLOSEFILE 0x06
#define SD_CMD_OPENDIR   0x08
#define SD_CMD_READDIR   0x09

/* ---- FatFS open mode flags ---- */
#define SD_FA_READ           0x01
#define SD_FA_WRITE          0x02
#define SD_FA_CREATE_ALWAYS  0x08

/* ---- STATUS register bit masks ---- */
#define SD_ST_BUSY   0x80   /* controller processing */
#define SD_ST_DRQ    0x08   /* data ready / accepted */
#define SD_ST_ERROR  0x01   /* error; read ERROR2 for FatFS code */
#define SD_ST_NONE   0xFF   /* no FreHD interface on bus */

/* ---- FILINFO offsets and size ---- */
#define FINFO_ATTR   8      /* fattrib byte */
#define FINFO_NAME   9      /* fname: NUL-terminated 8.3 string */
#define FINFO_SIZE  32     /* read buffer (actual FILINFO is 22 bytes) */

/* ---- FatFS attribute flags ---- */
#define FA_DIR   0x10
#define FA_VOL   0x08
#define FA_HID   0x02

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

/* Poll STATUS until BUSY clears.
 * Requires 4 consecutive 0xFF readings before declaring no-device;
 * a single transient 0xFF (e.g. after FA_CREATE_ALWAYS) is ignored. */
static void sd_wait(void) {
    uint8_t st;
    uint8_t none_cnt = 0;
    do {
        st = s_sd_status;
        if (st == SD_ST_NONE) {
            if (++none_cnt >= 4) { g_ferror = true; return; }
        } else {
            none_cnt = 0;
        }
    } while (st & SD_ST_BUSY);
}

/* Like sd_wait() but returns false on no-device instead of setting g_ferror. */
static bool sd_wait_silent(void) {
    uint8_t st;
    do {
        st = s_sd_status;
        if (st == SD_ST_NONE) return false;
    } while (st & SD_ST_BUSY);
    return true;
}

/* Iterative glob match (no recursion — safe on Z80 stack).
 * Both pat and str are NUL-terminated uppercase strings.
 * Supports '*' (any sequence) and '?' (any single character). */
static bool sd_fnmatch(const char *pat, const char *str) {
    const char *p    = pat;
    const char *s    = str;
    const char *pstar = NULL;   /* last '*' position in pattern */
    const char *sstar = NULL;   /* string position when '*' was tried */

    while (*s) {
        if (*p == '*') {
            pstar = p++;
            sstar = s;
        } else if (*p == '?' || *p == *s) {
            p++;
            s++;
        } else if (pstar) {
            p = pstar + 1;
            sstar++;
            s = sstar;
        } else {
            return false;
        }
    }
    while (*p == '*') p++;
    return (*p == '\0');
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

bool sd_is_frehd(void) {
    if (s_sd_status == SD_ST_NONE) return false;
    s_sd_cmd = SD_CMD_GETVER;
    if (!sd_wait_silent()) return false;
    return (s_sd_size == 6);
}

void sd_init(sd_t *sd, const char *name) {
    uint8_t i;
    char c;
    for (i = 0; i < SD_NAME_LEN - 1 && name[i]; i++) {
        c = name[i];
        if (c >= 'a' && c <= 'z') c = (char)((uint8_t)c - 'a' + 'A');
        sd->name[i] = c;
    }
    sd->name[i] = '\0';
    sd->namelen = i;
}

void sd_print_name(const sd_t *sd) {
    con_str("SD:");
    con_str(sd->name);
}

/* Return true if the named file exists on SD (silent — no error output). */
bool sd_exists(const sd_t *sd) {
    uint8_t i;
    s_sd_cmd = SD_CMD_OPENFILE;
    if (!sd_wait_silent()) return false;
    s_sd_size = (uint8_t)(sd->namelen + 2);    /* mode + name + NUL */
    s_sd_data = SD_FA_READ;
    for (i = 0; i < sd->namelen; i++) s_sd_data = (uint8_t)sd->name[i];
    s_sd_data = 0;
    if (!sd_wait_silent()) return false;
    if (s_sd_status & SD_ST_ERROR) return false;
    /* File exists and is now open — close it */
    s_sd_cmd = SD_CMD_CLOSEFILE;
    sd_wait_silent();
    return true;
}

/* Open the named SD file for reading.  Prints an error if not found. */
bool sd_open_rd(const sd_t *sd) {
    uint8_t i;
    s_sd_cmd = SD_CMD_OPENFILE;
    sd_wait();
    if (g_ferror) return false;
    s_sd_size = (uint8_t)(sd->namelen + 2);
    s_sd_data = SD_FA_READ;
    for (i = 0; i < sd->namelen; i++) s_sd_data = (uint8_t)sd->name[i];
    s_sd_data = 0;
    sd_wait();
    if (g_ferror) return false;
    if (s_sd_status & SD_ST_ERROR) {
        con_str("\r\nERROR: SD: file not found: ");
        con_str(sd->name);
        con_nl();
        return false;
    }
    return true;
}

/* Open the named SD file for writing.
 * Handles Exists!/Delete? prompt.  Creates or overwrites on success. */
bool sd_open_wr(const sd_t *sd) {
    uint8_t i;
    if (sd_exists(sd)) {
        con_str(" Exists!");
        if (!g_opts.wipe) {
            if (!g_opts.emend) {
                if (!ask_delete()) return false;
            }
        }
    }
    s_sd_cmd = SD_CMD_OPENFILE;
    sd_wait();
    if (g_ferror) return false;
    s_sd_size = (uint8_t)(sd->namelen + 2);
    s_sd_data = (uint8_t)(SD_FA_WRITE | SD_FA_CREATE_ALWAYS);
    for (i = 0; i < sd->namelen; i++) s_sd_data = (uint8_t)sd->name[i];
    s_sd_data = 0;
    sd_wait();
    if (g_ferror) return false;
    if (s_sd_status & SD_ST_ERROR) {
        con_str("\r\nERROR: SD: cannot create: ");
        con_str(sd->name);
        for (i = 0; i < sd->namelen; i++) {
            if (sd->name[i] == '/') {
                con_str(" - check directory exists");
                break;
            }
        }
        con_nl();
        return false;
    }
    return true;
}

/* Close the currently open SD file. */
void sd_close(void) {
    s_sd_cmd = SD_CMD_CLOSEFILE;
    sd_wait();
}

/* Return true if the directory path in sd->name exists on the SD card.
 * Trailing '/' is stripped before the OPENDIR call (e.g. "FILES/" -> "FILES").
 * Does not consume any READDIR entries; the FreHD resets on the next command. */
bool sd_dir_check(const sd_t *sd) {
    uint8_t path_len;
    uint8_t i;

    path_len = sd->namelen;
    while (path_len > 0 && sd->name[path_len - 1] == '/') path_len--;
    if (path_len == 0) return true;     /* root always exists */

    s_sd_size = (uint8_t)(path_len + 1);  /* path + NUL */
    s_sd_cmd  = SD_CMD_OPENDIR;
    sd_wait();
    if (g_ferror) return false;

    for (i = 0; i < path_len; i++) s_sd_data = (uint8_t)sd->name[i];
    s_sd_data = 0;
    sd_wait();
    if (g_ferror) return false;

    return !(s_sd_status & SD_ST_ERROR);
}

/* ------------------------------------------------------------------ */
/* Copy: SD -> CP/M                                                     */
/* Reads 256-byte blocks from FreHD (2 CP/M records per READFILE call) */
/* ------------------------------------------------------------------ */
bool sd_copy_sd_to_cpm(const sd_t *src, pfile_t *dst) {
    uint8_t st;
    uint16_t i;
    uint8_t *p;
    uint16_t blk_bytes;
    uint8_t blk_recs;

    g_ferror = false;   /* clear any stale error from a previous iteration */

    if (!sd_open_rd(src)) return false;
    if (!new_file(dst)) { sd_close(); return false; }

    g_crcval = 0;

    for (;;) {
        /* Request 256-byte block (SIZE2=0 => 256 bytes) */
        s_sd_size = 0;
        s_sd_cmd  = SD_CMD_READFILE;
        sd_wait();
        if (g_ferror) {
            f_delete(dst);          /* remove partial CP/M file */
            sd_close();
            return false;
        }

        st = s_sd_status;
        if (!(st & SD_ST_DRQ)) break;   /* DRQ clear: no more data */

        /* SIZE2 gives actual byte count (0 = 256) */
        blk_bytes = s_sd_size;
        if (blk_bytes == 0) blk_bytes = 256;
        blk_recs = (uint8_t)(blk_bytes / REC_SIZE);  /* 1 or 2 */

        /* Read blk_bytes into g_iobuf */
        p = g_iobuf;
        for (i = 0; i < blk_bytes; i++) *p++ = s_sd_data;

        /* Accumulate source CRC for valid records only */
        if (g_opts.verify) {
            crc_record(&g_iobuf[0]);
            if (blk_recs >= 2) crc_record(&g_iobuf[REC_SIZE]);
        }

        /* Write first CP/M record */
        set_dma(&g_iobuf[0]);
        if (!f_write(dst)) {
            f_delete(dst);
            set_dma((void*)0x0080);
            con_str("\r\nERROR: CP/M disk full. Partial copy deleted.\r\n");
            sd_close();
            return false;
        }

        /* Write second CP/M record only if present */
        if (blk_recs >= 2) {
            set_dma(&g_iobuf[REC_SIZE]);
            if (!f_write(dst)) {
                f_delete(dst);
                set_dma((void*)0x0080);
                con_str("\r\nERROR: CP/M disk full. Partial copy deleted.\r\n");
                sd_close();
                return false;
            }
        }
    }

    sd_close();
    f_close(dst);
    g_crcval2 = g_crcval;   /* save source CRC for verify comparison */
    return true;
}

/* ------------------------------------------------------------------ */
/* Copy: CP/M -> SD                                                     */
/* Reads CP/M records via blk_read() and writes 128 bytes per WRITEFILE */
/* ------------------------------------------------------------------ */
bool sd_copy_cpm_to_sd(pfile_t *src, const sd_t *dst) {
    uint16_t recs, n;
    bool eof;
    uint8_t *p;
    uint8_t j;

    g_ferror = false;   /* clear any stale error from a previous iteration */

    if (!f_open(src)) {
        con_str("\r\nERROR: can't open source\r\n");
        return false;
    }
    if (!sd_open_wr(dst)) { f_close(src); return false; }

    g_crcval = 0;
    eof = false;

    while (!eof) {
        /* Read a batch of CP/M records into g_iobuf.
         * blk_read() accumulates CRC in g_crcval when g_opts.verify is set. */
        recs = blk_read(src, g_iobuf, g_iobuf_recs, &eof);
        for (n = 0; n < recs; n++) {
            p = &g_iobuf[n * REC_SIZE];

            /* Write one 128-byte CP/M record to SD */
            s_sd_cmd = SD_CMD_WRITEFILE;
            sd_wait();
            if (g_ferror) { sd_close(); f_close(src); return false; }
            s_sd_size = (uint8_t)REC_SIZE;      /* 128 bytes */
            for (j = 0; j < (uint8_t)REC_SIZE; j++) s_sd_data = p[j];
            sd_wait();
            if (g_ferror) { sd_close(); f_close(src); return false; }
            if (s_sd_status & SD_ST_ERROR) {
                con_str("\r\nERROR: SD write error (FatFS ");
                con_out((char)('0' + s_sd_error));
                con_str(")\r\n");
                sd_close(); f_close(src); return false;
            }
        }
    }

    sd_close();
    f_close(src);
    g_crcval2 = g_crcval;   /* save source CRC for verify comparison */
    return true;
}

/* ------------------------------------------------------------------ */
/* CRC verify: re-read SD file and accumulate CRC into g_crcval        */
/* ------------------------------------------------------------------ */
bool sd_crc_file(const sd_t *sd) {
    uint8_t st;
    uint16_t i;
    uint8_t *p;
    uint16_t blk_bytes;
    uint8_t blk_recs;

    if (!sd_open_rd(sd)) return false;

    g_crcval = 0;

    for (;;) {
        s_sd_size = 0;
        s_sd_cmd  = SD_CMD_READFILE;
        sd_wait();
        if (g_ferror) { sd_close(); return false; }

        st = s_sd_status;
        if (!(st & SD_ST_DRQ)) break;

        /* SIZE2 gives actual byte count (0 = 256) */
        blk_bytes = s_sd_size;
        if (blk_bytes == 0) blk_bytes = 256;
        blk_recs = (uint8_t)(blk_bytes / REC_SIZE);  /* 1 or 2 */

        p = g_iobuf;
        for (i = 0; i < blk_bytes; i++) *p++ = s_sd_data;

        crc_record(&g_iobuf[0]);
        if (blk_recs >= 2) crc_record(&g_iobuf[REC_SIZE]);
    }

    sd_close();
    return true;
}

/* ------------------------------------------------------------------ */
/* Wildcard support                                                     */
/* ------------------------------------------------------------------ */

bool sd_has_wild(const sd_t *sd) {
    uint8_t i;
    for (i = 0; i < sd->namelen; i++)
        if (sd->name[i] == '*' || sd->name[i] == '?') return true;
    return false;
}

/* Enumerate SD files matching pattern->name.
 * Results are stored in g_nargbuf (FCB_FNAME_LEN bytes each) and g_nargc.
 * Returns match count.
 *
 * Pattern may include a path prefix: "DIR/*.COM" — the directory is opened
 * and each entry's filename (no path) is tested against the wildcard part.
 * Results store filenames only; sd_list_item() prepends the path on retrieval. */
uint16_t sd_list_wild(const sd_t *pattern) {
    static uint8_t finfo[FINFO_SIZE];   /* FILINFO read buffer */
    uint8_t i, j, fsize, attr;
    uint8_t path_len;                   /* chars up to and including last '/' */
    const char *wild;                   /* pointer to wildcard part in name */
    uint8_t dir_len;                    /* length of directory path string */
    uint8_t st;
    uint8_t *entry;
    const char *fn;
    uint8_t tbuf[13];                   /* temp: FCB->8.3 re-verify buffer */
    uint8_t ti;

    g_nargc = 0;

    /* Find path_len: length of prefix including the last separator */
    path_len = 0;
    for (i = 0; i < pattern->namelen; i++)
        if (pattern->name[i] == '/' || pattern->name[i] == '\\')
            path_len = (uint8_t)(i + 1);

    /* Wildcard portion is everything after the last separator */
    wild = &pattern->name[path_len];

    /* Directory path: prefix without trailing '/', or "/" for root */
    if (path_len > 1) {
        dir_len = (uint8_t)(path_len - 1);     /* strip trailing '/' */
    } else {
        dir_len = 1;                            /* root "/" */
    }

    /* OPENDIR: SIZE2 must be written before COMMAND2 is issued */
    s_sd_size = (uint8_t)(dir_len + 1);         /* path + NUL */
    s_sd_cmd  = SD_CMD_OPENDIR;
    sd_wait();
    if (g_ferror) return 0;

    /* Send directory path bytes */
    if (path_len > 1) {
        for (i = 0; i < dir_len; i++) s_sd_data = (uint8_t)pattern->name[i];
    } else {
        s_sd_data = '/';                        /* root */
    }
    s_sd_data = 0;                              /* NUL terminator */
    sd_wait();
    if (g_ferror) return 0;
    if (s_sd_status & SD_ST_ERROR) return 0;    /* directory not found */

    /* READDIR loop */
    while (g_nargc < MAX_NARG) {
        s_sd_cmd = SD_CMD_READDIR;
        sd_wait();
        if (g_ferror) break;

        st = s_sd_status;
        if (st & SD_ST_ERROR) break;            /* error or end of directory */
        if (!(st & SD_ST_DRQ)) break;           /* no more entries */

        /* Read FILINFO: SIZE2 gives byte count */
        fsize = s_sd_size;
        if (fsize > FINFO_SIZE) fsize = FINFO_SIZE;
        for (i = 0; i < fsize; i++) finfo[i] = s_sd_data;

        /* Skip directories, volume labels, and hidden entries */
        attr = finfo[FINFO_ATTR];
        if (attr & (FA_DIR | FA_VOL | FA_HID)) continue;

        /* Match filename against the wildcard pattern */
        fn = (const char *)&finfo[FINFO_NAME];
        if (!sd_fnmatch(wild, fn)) continue;

        /* Convert 8.3 "FILE.COM" to FCB format (8+3 space-padded, no dot) */
        entry = &g_nargbuf[g_nargc * FCB_FNAME_LEN];
        for (j = 0; j < FCB_FNAME_LEN; j++) entry[j] = ' ';

        i = 0; j = 0;
        /* Name part (up to 8 chars, stop at '.' or NUL) */
        while (j < 8 && fn[i] && fn[i] != '.') entry[j++] = (uint8_t)fn[i++];
        /* Skip the '.' separator */
        if (fn[i] == '.') {
            i++;
            j = 8;
            /* Extension part (up to 3 chars) */
            while (j < FCB_FNAME_LEN && fn[i]) entry[j++] = (uint8_t)fn[i++];
        }

        /* Defensive re-verify: reconstruct 8.3 from FCB and re-match.
         * Guards against SD filenames with >8-char name parts (e.g.
         * NIALLCONV.COM): the name loop fills all 8 bytes and exits with
         * fn[i]!='.' so the extension is never written to the FCB.
         * The truncated FCB "NIALLCON   " would fail the pattern here,
         * correctly excluding a file that cannot be stored on CP/M. */
        ti = 0;
        for (i = 0; i < 8 && entry[i] != ' '; i++) tbuf[ti++] = entry[i];
        if (entry[8] != ' ') {
            tbuf[ti++] = '.';
            for (i = 8; i < FCB_FNAME_LEN && entry[i] != ' '; i++)
                tbuf[ti++] = entry[i];
        }
        tbuf[ti] = '\0';
        if (!sd_fnmatch(wild, (const char*)tbuf)) {
            con_str("\r\n ");
            con_str(fn);
            con_str(": SD name too long for CP/M, skipped.\r\n"
                    "   To copy: " PROG_NAME " SD:");
            con_str(fn);
            con_str(" A:NEWNAME.COM\r\n");
            continue;
        }

        g_nargc++;
    }

    return g_nargc;
}

/* Retrieve the nth wildcard result as an sd_t.
 * base provides the path prefix (e.g. g_sd_src).  The stored FCB-format
 * name is converted back to "NAME.EXT" and the path prefix is prepended. */
void sd_list_item(uint16_t n, const sd_t *base, sd_t *out) {
    uint8_t *entry;
    uint8_t i, k;
    uint8_t path_len;
    uint8_t c;

    out->namelen = 0;
    out->name[0] = '\0';
    if (n >= g_nargc) return;

    k = 0;

    /* Prepend path prefix from base (everything up to and including last '/') */
    path_len = 0;
    for (i = 0; i < base->namelen; i++)
        if (base->name[i] == '/' || base->name[i] == '\\')
            path_len = (uint8_t)(i + 1);
    for (i = 0; i < path_len && k < SD_NAME_LEN - 1; i++)
        out->name[k++] = base->name[i];

    /* Reconstruct "NAME.EXT" from FCB format (8+3, space-padded) */
    entry = &g_nargbuf[n * FCB_FNAME_LEN];

    for (i = 0; i < 8; i++) {
        c = entry[i];
        if (c == ' ') break;
        if (k < SD_NAME_LEN - 1) out->name[k++] = c;
    }
    if (entry[8] != ' ') {
        if (k < SD_NAME_LEN - 1) out->name[k++] = '.';
        for (i = 8; i < FCB_FNAME_LEN; i++) {
            c = entry[i];
            if (c == ' ') break;
            if (k < SD_NAME_LEN - 1) out->name[k++] = c;
        }
    }
    out->name[k] = '\0';
    out->namelen = k;
}

#endif /* FREHD */

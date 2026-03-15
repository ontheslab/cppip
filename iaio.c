/*
 * iaio.c -- NABU IA RetroNET file store I/O for CPPIP (Phase 5)
 *
 * Compiled only when -DNABU_IA is passed to zcc.
 * Include path must contain ../NABULIB/ (RetroNET-FileStore.h and NABU-LIB.h).
 *
 * One copy of the RetroNET-FileStore.c implementation ends up here because
 * RetroNET-FileStore.h includes its own .c at the bottom (header-only style).
 * Do NOT include RetroNET-FileStore.h elsewhere - Learning.
 */

#ifdef NABU_IA

/* Must be defined before NABU-LIB.h */
#define BIN_TYPE BIN_CPM
#define DISABLE_VDP
#define DISABLE_KEYBOARD_INT

#include "../NABULIB/NABU-LIB.h"
#include "../NABULIB/RetroNET-FileStore.h"

#include "ppip.h"
#include "iaio.h"
#include "diskio.h"
#include "console.h"
#include "crc.h"

/* ------------------------------------------------------------------ */

bool ia_is_nabu(void) {
    return isCloudCPM();
}

void ia_init(ia_t *ia, const char *name) {
    uint8_t i;
    char c;
    for (i = 0; i < IA_NAME_LEN - 1 && name[i]; i++) {
        c = name[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        ia->name[i] = c;
    }
    ia->name[i] = '\0';
    ia->namelen = i;
    ia->handle  = 0xFF;
}

void ia_print_name(const ia_t *ia) {
    con_str("IA:");
    con_str(ia->name);
}

bool ia_exists(const ia_t *ia) {
    return rn_fileSize(ia->namelen, (uint8_t*)ia->name) >= 0;
}

bool ia_open_rd(ia_t *ia) {
    if (!ia_exists(ia)) {
        con_str("\r\nERROR: IA file not found: ");
        con_str(ia->name);
        con_nl();
        return false;
    }
    ia->handle = rn_fileOpen(ia->namelen, (uint8_t*)ia->name,
                             OPEN_FILE_FLAG_READONLY, 0xFF);
    return true;
}

/* Open for write.  Handles Exists!/Delete? prompt.
 * Creates file if absent; truncates if present and confirmed. */
bool ia_open_wr(ia_t *ia) {
    bool exists = ia_exists(ia);
    if (exists) {
        if (!g_opts.wipe && !g_opts.emend) {
            con_str(" Exists!");
            if (!ask_delete()) return false;
        }
    }
    ia->handle = rn_fileOpen(ia->namelen, (uint8_t*)ia->name,
                             OPEN_FILE_FLAG_READWRITE, 0xFF);
    if (exists) rn_fileHandleEmptyFile(ia->handle);
    return true;
}

void ia_close(ia_t *ia) {
    rn_fileHandleClose(ia->handle);
    ia->handle = 0xFF;
}

void ia_delete(ia_t *ia) {
    rn_fileDelete(ia->namelen, (uint8_t*)ia->name);
}

/* ------------------------------------------------------------------ */

/* Copy CP/M file -> IA.
 * src must have dr/user/FCB name set.
 * dst must already be open for write (ia_open_wr called by caller). */
bool ia_copy_cpm_to_ia(pfile_t *src, ia_t *dst) {
    bool     eof;
    uint16_t recs;

    g_crcval = 0;

    if (!f_open(src)) {
        con_str("\r\nERROR: can't open source\r\n");
        g_ferror = true;
        return false;
    }

    if (!ia_open_wr(dst)) {
        f_close(src);
        return false;
    }

    eof = false;
    while (!eof) {
        recs = blk_read(src, g_iobuf, g_iobuf_recs, &eof);
        if (recs > 0)
            rn_fileHandleAppend(dst->handle, 0,
                                (uint16_t)(recs * REC_SIZE), g_iobuf);
    }

    g_crcval2 = g_crcval;
    f_close(src);
    ia_close(dst);
    return true;
}

/* Copy IA -> CP/M file.
 * src is an IA descriptor (not yet open).
 * dst pfile_t must have dr/user/FCB name set; new_file() is called here. */
bool ia_copy_ia_to_cpm(ia_t *src, pfile_t *dst) {
    uint16_t bytes;
    uint16_t recs;
    uint16_t pad;
    uint16_t j;

    g_crcval = 0;

    if (!ia_open_rd(src)) return false;

    if (!new_file(dst)) {
        ia_close(src);
        return false;
    }

    while (1) {
        bytes = rn_fileHandleReadSeq(src->handle, g_iobuf, 0, IOBUF_SIZE);
        if (bytes == 0) break;

        /* Round up to whole 128-byte CP/M records */
        recs = (uint16_t)((bytes + REC_SIZE - 1) / REC_SIZE);

        /* Pad last partial record with CP/M EOF marker ^Z */
        pad = (uint16_t)(recs * REC_SIZE);
        for (j = bytes; j < pad; j++) g_iobuf[j] = 0x1A;

        /* Accumulate source CRC across all data */
        if (g_opts.verify) {
            for (j = 0; j < recs; j++)
                crc_record(&g_iobuf[(uint16_t)(j * REC_SIZE)]);
        }

        if (!blk_write(dst, g_iobuf, recs)) {
            ia_close(src);
            f_delete(dst);
            con_str("\r\nERROR: Disk full. Copy deleted.\r\n");
            g_ferror = true;
            return false;
        }
    }

    g_crcval2 = g_crcval;
    ia_close(src);

    if (!f_close(dst)) {
        con_str("\r\nERROR: can't close destination\r\n");
        g_ferror = true;
        return false;
    }

    return true;
}

bool ia_has_wild(const ia_t *ia) {
    uint8_t i;
    for (i = 0; i < ia->namelen; i++)
        if (ia->name[i] == '*' || ia->name[i] == '?') return true;
    return false;
}

uint16_t ia_list_wild(const ia_t *pattern) {
    return rn_fileList(1, (uint8_t*)"\\",
                       pattern->namelen, (uint8_t*)pattern->name,
                       FILE_LIST_FLAG_INCLUDE_FILES);
}

void ia_list_item(uint16_t n, ia_t *out) {
    FileDetailsStruct fds;
    uint8_t i, len;
    uint8_t c;
    rn_fileListItem(n, &fds);
    len = fds.FilenameLen;
    if (len >= IA_NAME_LEN) len = (uint8_t)(IA_NAME_LEN - 1);
    for (i = 0; i < len; i++) {
        c = fds.Filename[i];
        if (c >= 'a' && c <= 'z') c = (uint8_t)(c - 'a' + 'A');
        out->name[i] = c;
    }
    out->name[len] = '\0';
    out->namelen   = len;
    out->handle    = 0xFF;
}

/* Read IA file record by record and accumulate CRC into g_crcval.
 * Used for /V verify after a CPM->IA copy. */
bool ia_crc_file(ia_t *ia) {
    uint16_t bytes;
    uint16_t recs;
    uint16_t j;

    g_crcval = 0;

    if (!ia_open_rd(ia)) return false;

    while (1) {
        bytes = rn_fileHandleReadSeq(ia->handle, g_iobuf, 0, IOBUF_SIZE);
        if (bytes == 0) break;
        recs = (uint16_t)(bytes / REC_SIZE);
        for (j = 0; j < recs; j++)
            crc_record(&g_iobuf[(uint16_t)(j * REC_SIZE)]);
    }

    ia_close(ia);
    return true;
}

#endif /* NABU_IA */

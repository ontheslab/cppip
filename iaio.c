/*
 * iaio.c -- NABU IA RetroNET file store I/O for CPPIP (Phase 5)
 *
 * Compiled only when -DNABU_IA is passed to zcc.
 * Include path must contain ../NABULIB/ (RetroNET-FileStore.h and NABU-LIB.h).
 *
 * One copy of the RetroNET-FileStore.c implementation ends up here because
 * RetroNET-FileStore.h includes its own .c at the bottom (header-only style).
 * Do NOT include RetroNET-FileStore.h elsewhere - it pulls in its own .c
 * implementation and will cause duplicate symbol errors if included twice.
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

/* Returns true if ia->name has a drive-letter prefix (e.g. "Z:\...").
 * The NIA server auto-creates directory trees for these paths, so no
 * pre-flight directory check is needed and rn_fileOpen is safe to call
 * even when subdirectories don't yet exist. */
static bool ia_has_drive(const ia_t *ia) {
    return ia->namelen >= 2 && ia->name[1] == ':';
}

/* Forward declaration — defined after ia_open_rd. */
static bool ia_check_dirs(const ia_t *ia);

void ia_init(ia_t *ia, const char *name) {
    uint8_t i = 0;
    const char *src = name;
    char c;
    bool has_drive;

    /* /X/ or /X  →  drive-letter format  X:\...
     * e.g. /D/1/ARCOPY.ZIP → D:\1\ARCOPY.ZIP
     *      /Z/TEST/FILE    → Z:\TEST\FILE
     * Lets the NIA server's auto-create-directory code path handle
     * the path, same as the explicit "Z:/" syntax.
     * Any other leading slash is stripped to avoid crashing the server
     * (it maps a bare leading '/' to its Windows drive root). */
    if (src[0] == '/' &&
        ((src[1] >= 'a' && src[1] <= 'z') || (src[1] >= 'A' && src[1] <= 'Z')) &&
        (src[2] == '/' || src[2] == '\\' || src[2] == '\0')) {
        c = src[1];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        ia->name[i++] = c;
        ia->name[i++] = ':';
        src += 2;
        has_drive = true;
    } else {
        if (src[0] == '/') src++;   /* strip orphan leading slash */
        has_drive = (src[0] != '\0' && src[1] == ':');
    }

    while (i < IA_NAME_LEN - 1 && *src) {
        c = *src++;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (has_drive && c == '/') c = '\\';
        ia->name[i++] = c;
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
    /* For plain paths, verify directory components exist before calling
     * rn_fileOpen(). The NIA server throws DirectoryNotFoundException and
     * drops the TCP connection for read-only opens on missing directories —
     * the same crash as on the write path. ia_check_dirs() uses
     * rn_fileList() which handles missing directories safely (returns 0).
     * Skip for drive-letter paths — the server handles those safely.
     *
     * Do NOT use ia_exists()/rn_fileSize() for the file check: the server
     * does a case-sensitive comparison there, so uppercase queries fail for
     * lowercase files. rn_fileOpen(READONLY) uses the Windows OS directly
     * and is case-insensitive, so the handle check is sufficient. */
    if (!ia_has_drive(ia) && !ia_check_dirs(ia)) return false;
    ia->handle = rn_fileOpen(ia->namelen, (uint8_t*)ia->name,
                             OPEN_FILE_FLAG_READONLY, 0xFF);
    if (ia->handle == 0xFF) {
        con_str("\r\nERROR: IA file not found: ");
        con_str(ia->name);
        con_nl();
        return false;
    }
    return true;
}

/* Verify every directory component in ia->name exists before writing.
 * Walks level by level from root so each rn_fileList call uses a parent
 * path that was verified in the previous step — safe even if deeper dirs
 * don't exist (rn_fileList on a missing path returns 0; rn_fileOpen on a
 * missing path crashes the NIA server and disconnects the client).
 * Skipped for drive-letter paths (e.g. "Z:\DIR\FILE") — the NIA server
 * calls Directory.CreateDirectory() for those automatically.
 * Returns true if all directory components exist (or path has no dirs). */
static bool ia_check_dirs(const ia_t *ia) {
    uint8_t i, j;
    uint8_t seg_start;
    uint8_t seg_end;
    uint8_t parent_len;
    uint16_t cnt;

    if (ia_has_drive(ia)) return true;   /* server auto-creates */

    seg_start  = 0;
    parent_len = 0;

    for (i = 0; i < ia->namelen; i++) {
        if (ia->name[i] != '/' && ia->name[i] != '\\') continue;

        seg_end = i;
        if (seg_end == seg_start) { seg_start = i + 1; continue; }  /* skip // */

        /* ia->name[seg_start..seg_end-1] is a directory component */
        if (parent_len == 0) {
            cnt = rn_fileList(1, (uint8_t*)"\\",
                              (uint8_t)(seg_end - seg_start),
                              (uint8_t*)&ia->name[seg_start],
                              FILE_LIST_FLAG_INCLUDE_DIRECTORIES);
        } else {
            cnt = rn_fileList(parent_len, (uint8_t*)ia->name,
                              (uint8_t)(seg_end - seg_start),
                              (uint8_t*)&ia->name[seg_start],
                              FILE_LIST_FLAG_INCLUDE_DIRECTORIES);
        }

        if (cnt == 0) {
            con_str("\r\nERROR: IA: directory not found: ");
            for (j = 0; j < seg_end; j++) con_out(ia->name[j]);
            con_str("\r\n  Tip: use IA:/X/PATH/FILE or IA:X:/PATH/FILE\r\n");
            return false;
        }

        parent_len = i;   /* path up to (not including) this slash */
        seg_start  = i + 1;
    }
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
    if (!ia_check_dirs(ia)) return false;
    ia->handle = rn_fileOpen(ia->namelen, (uint8_t*)ia->name,
                             OPEN_FILE_FLAG_READWRITE, 0xFF);
    if (ia->handle == 0xFF) {
        con_str("\r\nERROR: IA: cannot create: ");
        con_str(ia->name);
        con_nl();
        return false;
    }
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
        bytes = rn_fileHandleReadSeq(src->handle, g_iobuf, 0, (uint16_t)g_iobuf_recs * REC_SIZE);
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

/* Path prefix cached by ia_list_wild() for use by ia_list_item().
 * Stored as "DIR/" or "A/B/C/" — includes the trailing slash. */
static uint8_t s_list_path[IA_NAME_LEN];
static uint8_t s_list_pathlen;  /* 0 = root, no prefix to prepend */

uint16_t ia_list_wild(const ia_t *pattern) {
    uint8_t i, last_sep = 0xFF;
    uint8_t pathlen, wildlen;
    const char *wild;

    /* Find last path separator to split "DIR/WILD" */
    for (i = 0; i < pattern->namelen; i++)
        if (pattern->name[i] == '/' || pattern->name[i] == '\\') last_sep = i;

    if (last_sep != 0xFF) {
        /* Has path component: split before/after last separator */
        pathlen = last_sep;                         /* e.g. "A/B/C" */
        wild    = pattern->name + last_sep + 1;     /* e.g. "*.DAT" */
        wildlen = (uint8_t)(pattern->namelen - last_sep - 1);

        /* Cache "A/B/C/" prefix (with trailing slash) for ia_list_item */
        for (i = 0; i < pathlen; i++) s_list_path[i] = (uint8_t)pattern->name[i];
        s_list_path[pathlen] = '/';
        s_list_pathlen = (uint8_t)(pathlen + 1);

        return rn_fileList(pathlen, (uint8_t*)pattern->name,
                           wildlen, (uint8_t*)wild,
                           FILE_LIST_FLAG_INCLUDE_FILES);
    } else {
        /* No path: list root */
        s_list_pathlen = 0;
        return rn_fileList(1, (uint8_t*)"\\",
                           pattern->namelen, (uint8_t*)pattern->name,
                           FILE_LIST_FLAG_INCLUDE_FILES);
    }
}

void ia_list_item(uint16_t n, ia_t *out) {
    FileDetailsStruct fds;
    uint8_t i, k = 0;
    uint8_t c, len;

    rn_fileListItem(n, &fds);

    /* Prepend cached path prefix (e.g. "A/B/C/") so callers get the full path */
    for (i = 0; i < s_list_pathlen && k < IA_NAME_LEN - 1; i++)
        out->name[k++] = s_list_path[i];

    /* Append filename, uppercase */
    len = fds.FilenameLen;
    if (len >= (uint8_t)(IA_NAME_LEN - k)) len = (uint8_t)(IA_NAME_LEN - k - 1);
    for (i = 0; i < len; i++) {
        c = fds.Filename[i];
        if (c >= 'a' && c <= 'z') c = (uint8_t)(c - 'a' + 'A');
        out->name[k++] = c;
    }
    out->name[k] = '\0';
    out->namelen  = k;
    out->handle   = 0xFF;
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
        bytes = rn_fileHandleReadSeq(ia->handle, g_iobuf, 0, (uint16_t)g_iobuf_recs * REC_SIZE);
        if (bytes == 0) break;
        recs = (uint16_t)(bytes / REC_SIZE);
        for (j = 0; j < recs; j++)
            crc_record(&g_iobuf[(uint16_t)(j * REC_SIZE)]);
    }

    ia_close(ia);
    return true;
}

#endif /* NABU_IA */

/* ppip.c - CPPIP/NPPIP/FPPIP - CP/M File Copy Utility
   A C reimplementation of PPIP v1.8 (D. Jewett III, 1985-1988).
   Original Z80 assembly source preserved in old/.
   C port by Intangybles (c)2026.

   See CHANGELOG.md for full version history.

   Build (default - produces CPPIP.COM, NPPIP.COM, FPPIP.COM):
     build.bat

   Build manually (inside Claude Code / Git Bash):
     CPPIP.COM (standard - IA via /N, FreHD SD card support):
       zcc +cpm -vn -create-app -compiler=sdcc --opt-code-size -DNABU_IA -DFREHD \
           -I../NABULIB \
           ppip.c cmdparse.c filename.c diskio.c crc.c iaio.c console.c sdio.c -o CPPIP
     NPPIP.COM (NABU edition - IA always on, no SD card code):
       zcc +cpm -vn -create-app -compiler=sdcc --opt-code-size -DNABU_IA -DNABU_DEFAULT \
           -I../NABULIB \
           ppip.c cmdparse.c filename.c diskio.c crc.c iaio.c console.c -o NPPIP
     FPPIP.COM (FreHD edition - SD always on, no IA code):
       zcc +cpm -vn -create-app -compiler=sdcc --opt-code-size -DFREHD -DFREHD_DEFAULT \
           ppip.c cmdparse.c filename.c diskio.c crc.c console.c sdio.c -o FPPIP

   Target platforms : CP/M 2.2 - NABU CloudCP/M, TRS-80 Model 4P, Kaypro
   Compiler         : z88dk with SDCC backend (Z80, 64KB address space)

   Version numbering: major.minor (build)
   Increment build number for each new test build.

   -----------------------------------------------------------------------
   Version 1.10 (47) - current

   FreHD SD card extension. New binary FPPIP.COM; SD: prefix compiled into
   CPPIP.COM when -DFREHD is set. All SD code in sdio.c/h.
   Hardware-tested on TRS-80 Model 4P with Montezuma Micro CP/M 2.2.

   Key fixes during hardware testing (builds 33-47):

   - SIZE2 partial-block (build 36): last block of an odd-record file
     returns fewer than 256 bytes. SIZE2 gives the actual byte count
     (0 = 256); only those bytes are CRC'd and written to CP/M.

   - Montezuma Micro CP/M 2.2 f_exist() (builds 38-39): Montezuma's BDOS
     returns a user-0 directory entry as fallback when a file is not found.
     Fix: single SRCHFST call; if entry[0] != pf->user, treat as not found.

   - sd_list_wild false match (builds 42-43): SD filenames with name parts
     longer than 8 characters caused FCB conversion to drop the extension.
     Fix: reconstruct 8.3 name from FCB bytes and re-match; skip with
     warning if the extension was lost.

   - IA wildcard truncation (build 44): IA: filenames with name parts
     longer than 8 chars are truncated to fit CP/M. Truncated copies now
     show [truncated] on the copy line; collision shows a rename hint.

   - SD subdirectory error handling (builds 45-47): FreHD OPENDIR is not
     working/supported by the FreHD emulator so a pre-flight directory
     check was not viable. Instead, sd_open_wr() sets g_sd_create_err on
     the first failed create; the batch loop checks this flag and aborts
     after the first failure. Error message includes check directory exists
     when a path separator is present. Works on both emulator and real hardware.
*/

#include "ppip.h"
#include "cmdparse.h"
#include "filename.h"
#include "diskio.h"
#include "console.h"
#include "crc.h"
#include "iaio.h"
#include "sdio.h"

/* ---- Global definitions ---- */

uint8_t   g_drive      = 0;
uint8_t   g_user       = 0;
options_t g_opts       = { false, false, false, false, false, false };
bool      g_ferror     = false;
bool      g_want_help  = false;

ia_t      g_ia_src;
ia_t      g_ia_dst;
bool      g_src_is_ia  = false;
bool      g_dst_is_ia  = false;

#ifdef FREHD
sd_t      g_sd_src;
sd_t      g_sd_dst;
bool      g_src_is_sd  = false;
bool      g_dst_is_sd  = false;
#endif

uint8_t  *g_iobuf      = NULL;  /* TPA-allocated in main() */
uint16_t  g_iobuf_recs = 0;    /* set in main() */

/* Wildcard expansion buffer (Phase 2) */
uint8_t  *g_nargbuf = NULL;    /* TPA-allocated in main() */
uint16_t  g_nargc   = 0;

/* Command-line argument storage */
char      g_argbuf[MAX_ARG][MAX_ARG_LEN];
char     *g_argv[MAX_ARG];
int       g_argc = 0;

/* ---- CON: check ---- */
static bool is_con(const char *arg) {
    return (arg[0] == 'C' && arg[1] == 'O' && arg[2] == 'N' && arg[3] == ':');
}

/* ---- Copy attributes from source FCB to destination FCB ---- */
static void copy_attributes(pfile_t *dst, const pfile_t *src) {
    uint8_t i;
    /* Copy attribute bits (bit 7 of each name+type byte) from src to dst */
    for (i = 0; i < 8; i++) {
        dst->fcb.f[i] = (uint8_t)((dst->fcb.f[i] & 0x7F) | (src->fcb.f[i] & 0x80));
    }
    for (i = 0; i < 3; i++) {
        dst->fcb.t[i] = (uint8_t)((dst->fcb.t[i] & 0x7F) | (src->fcb.t[i] & 0x80));
    }
    f_attrib(dst);
}

/* ---- Single file copy (called once per expanded source match) ---- */
static void copy_one(pfile_t *src, pfile_t *dst) {
    uint8_t retry;

    /* Print "src to dst" â€” no newline yet, rest of line filled in below */
    print_fname(src);
    con_str(" to ");
    print_fname(dst);

    if (g_opts.verify) {
        /* CRC verify loop â€” retry up to CRC_RETRIES times on mismatch */
        for (retry = 0; retry < CRC_RETRIES; retry++) {
            if (!f_copy(src, dst)) return;  /* error msg inside has its own \r\n */

            con_str(" - Verifying");

            /* g_crcval2 = source CRC (saved by f_copy), g_crcval will = dest CRC */
            if (!crc_file(dst)) {
                con_str("\r\nERROR: can't verify dest\r\n");
                return;
            }

            if (g_crcval == g_crcval2) break;  /* CRC match â€” done */

            /* Mismatch: delete bad dest and retry if attempts remain */
            f_delete(dst);
            if (retry + 1 < CRC_RETRIES) {
                con_str(" FAILED - retrying...\r\n");
                print_fname(src); con_str(" to "); print_fname(dst);
            }
        }

        if (g_crcval != g_crcval2) {
            con_str(" FAILED\r\n CRC failed! Please check your disk.\r\n");
            con_out('\007');  /* bell */
            return;
        }

        con_str(" OK");
        if (g_opts.report) { con_str("  CRC: "); con_hex16(g_crcval2); }
        con_nl();
    } else {
        if (!f_copy(src, dst)) return;
        con_nl();
    }

    copy_attributes(dst, src);

    if (g_opts.movf) {
        con_str("  - Erasing ");
        print_fname(src);
        con_nl();
        f_delete(src);
    }
}

/* ---- CON: keyboard-to-file editor (Phase 4) ---- */
static void do_con_copy(void) {
    static uint8_t buf[REC_SIZE];   /* 128-byte record buffer (static: off stack) */
    pfile_t  dst;
    uint8_t  pos;
    uint8_t  i;
    uint8_t  next;  /* for ~ escape sequence */
    char     c;
    bool     warned;
    bool     done;

    if (g_argc < 2) {
        con_str(" Usage: CPPIP CON: dest\r\n");
        return;
    }

    if (!init_pfile(1, &dst)) return;

    /* Create destination (handles Exists! / R/O prompts) */
    if (!new_file(&dst)) return;

    con_str("CON: to ");
    print_fname(&dst);
    con_str("  (^Z to end)\r\n");

    pos    = 0;
    warned = false;
    done   = false;

    while (!done) {
        /* Bell warning when fewer than 6 chars remain in record */
        if (!warned && pos >= REC_SIZE - 6) {
            con_out('\007');
            warned = true;
        }

        c = con_in_ne();    /* no echo â€” we control all output */

        if (c == '\032') {  /* ^Z: end of input */
            con_out('^'); con_out('Z'); con_nl();
            done = true;

        } else if (c == '~') {  /* ~ escape: next char becomes control code */
            next = (uint8_t)con_in_ne();
            if (next == '~') {
                con_out('~');
                buf[pos++] = '~';
            } else {
                buf[pos++] = (uint8_t)(next & 0x1F);  /* silent ctrl char */
            }

        } else if (c == '\010' || c == (char)0x7F) {  /* ^H or DEL: rubout */
            if (pos > 0) {
                pos--;
                con_out('\010'); con_out(' '); con_out('\010');
            }

        } else if (c == '\r') {  /* CR: echo CR+LF, store both */
            con_out('\r'); con_out('\n');
            buf[pos++] = '\r';
            if (pos >= REC_SIZE) {
                set_dma(buf); f_write(&dst); pos = 0; warned = false;
            }
            buf[pos++] = '\n';
            warned = false;

        } else {  /* printable character */
            con_out(c);
            buf[pos++] = (uint8_t)c;
        }

        /* Flush record when full */
        if (pos >= REC_SIZE) {
            set_dma(buf); f_write(&dst); pos = 0; warned = false;
        }
    }

    /* Pad final partial record with ^Z and write */
    for (i = pos; i < REC_SIZE; i++) buf[i] = '\032';
    set_dma(buf);
    f_write(&dst);
    f_close(&dst);
}

/* Check for Ctrl-C between files.
 * Uses BDOS 6 directly -- returns 0 immediately if no key is waiting.
 * Avoids BDOS 11 (CONSTAT) which gives false positives on CloudCP/M
 * after HCCA communication, causing con_in_ne() to block indefinitely. */
static bool check_abort(void) {
    if ((uint8_t)bdos(BDOS_DIRIO, 0xFF) == 0x03) {
        con_str("^C\r\n");
        return true;
    }
    return false;
}

/* ---- IA: argument detector ---- */
#ifdef NABU_IA

/* Return length of path prefix including trailing slash (0 = no path component).
 * Works for any depth: "A/B/C/FILE.DAT" -> 6 (length of "A/B/C/"). */
static uint8_t ia_path_len(const ia_t *ia) {
    uint8_t i, last = 0xFF;
    for (i = 0; i < ia->namelen; i++)
        if (ia->name[i] == '/' || ia->name[i] == '\\') last = i;
    return (last == 0xFF) ? 0 : (uint8_t)(last + 1);
}

/* Return pointer to the filename/wildcard part after the last slash.
 * Returns ia->name directly if no slash present. */
static const char *ia_name_part(const ia_t *ia) {
    uint8_t i, last = 0xFF;
    for (i = 0; i < ia->namelen; i++)
        if (ia->name[i] == '/' || ia->name[i] == '\\') last = i;
    return (last == 0xFF) ? ia->name : &ia->name[last + 1];
}

/* Build ia_t name from path prefix of path_tmpl + filename derived from FCB.
 * path_tmpl may be:
 *   namelen == 0          -> no prefix  -> "NAME.EXT"
 *   ends with / or \      -> directory  -> "DIR/NAME.EXT"
 *   contains mid-path /   -> wildcard   -> "DIR/NAME.EXT" (prefix = up to last /)
 * Any depth is supported: "A/B/C/*.SAV" -> prefix "A/B/C/". */
static void ia_name_with_path(ia_t *out, const ia_t *path_tmpl, const pfile_t *src) {
    uint8_t i, k = 0;
    uint8_t pfxlen;

    /* Determine prefix length */
    if (path_tmpl->namelen > 0 &&
        (path_tmpl->name[path_tmpl->namelen - 1] == '/' ||
         path_tmpl->name[path_tmpl->namelen - 1] == '\\')) {
        pfxlen = path_tmpl->namelen;        /* "SUBDIR/" â€” use whole name */
    } else {
        pfxlen = ia_path_len(path_tmpl);    /* "SUBDIR/*.SAV" â€” up to last / */
    }

    /* Copy path prefix */
    for (i = 0; i < pfxlen && k < IA_NAME_LEN - 1; i++)
        out->name[k++] = path_tmpl->name[i];

    /* Append filename from FCB (strip attribute bits) */
    for (i = 0; i < 8; i++) {
        uint8_t c = src->fcb.f[i] & 0x7F;
        if (c == ' ') break;
        if (k < IA_NAME_LEN - 1) out->name[k++] = c;
    }
    if ((src->fcb.t[0] & 0x7F) != ' ') {
        if (k < IA_NAME_LEN - 1) out->name[k++] = '.';
        for (i = 0; i < 3; i++) {
            uint8_t c = src->fcb.t[i] & 0x7F;
            if (c == ' ') break;
            if (k < IA_NAME_LEN - 1) out->name[k++] = c;
        }
    }
    out->name[k] = '\0';
    out->namelen  = k;
    out->handle   = 0xFF;
}

static bool is_ia_arg(int idx) {
    const char *s;
    if (idx >= g_argc) return false;
    s = g_argv[idx];
    return (s[0] == 'I' && s[1] == 'A' && s[2] == ':');
}

/* ---- IA copy dispatch (Phase 5) ---- */
static void do_copy_ia(void) {
    pfile_t     cpm;
    pfile_t     src_tmpl;
    pfile_t     dst_tmpl;
    pfile_t     src_pf;
    ia_t        ia_file;
    uint8_t     i;
    uint16_t    n;
    uint16_t    cnt;
    uint8_t    *narg;
    uint8_t     saved_dr;
    uint8_t     saved_user;
    const char *nm_part;    /* ia_name_part() result cached for truncation check */

    /* Abort if not on NABU CloudCP/M and user hasn't forced /N */
    if (!ia_is_nabu() && !g_opts.nabu_ia) {
        con_str(" IA: unavailable - use /N to enable on non-CloudCP/M NABU\r\n");
        return;
    }

    /* IA -> IA: not supported */
    if (g_src_is_ia && g_dst_is_ia) {
        con_str(" ERROR: IA: to IA: copy not supported\r\n");
        return;
    }

    /* IA -> CPM */
    if (g_src_is_ia) {
        if (g_argc < 2) {
            con_str(" Usage: CPPIP IA:src dest\r\n");
            return;
        }
        if (!init_pfile(1, &cpm)) return;

        if (ia_has_wild(&g_ia_src)) {
            /* Wildcard IA source: enumerate matching files */

            /* Save dest template; bare drive -> all-? wildcard */
            dst_tmpl = cpm;
            if (dst_tmpl.fcb.f[0] == ' ') {
                for (i = 0; i < 8; i++) dst_tmpl.fcb.f[i] = '?';
                for (i = 0; i < 3; i++) dst_tmpl.fcb.t[i] = '?';
            }

            cnt = ia_list_wild(&g_ia_src);
            if (cnt == 0) {
                con_str(" No IA file(s) found\r\n");
                return;
            }
            for (n = 0; n < cnt; n++) {
                if (check_abort()) return;
                /* Re-establish the server's file-list context before each
                 * ia_list_item() call.  ia_check_dirs() inside ia_open_rd()
                 * issues its own rn_fileList(DIRS,...) calls which clobber
                 * the server's single global list state left by ia_list_wild.
                 * Re-calling ia_list_wild() restores it; return value ignored
                 * (we use the original cnt for loop bounds). */
                ia_list_wild(&g_ia_src);
                ia_list_item(n, &ia_file);

                /* Build src pfile_t from IA filename part only (strips path) */
                if (!make_fcb(ia_name_part(&ia_file), &src_pf)) {
                    con_str("\r\nERROR: invalid IA filename: ");
                    con_str(ia_file.name);
                    con_nl();
                    continue;
                }
                match_wild(&cpm, &dst_tmpl, &src_pf);

                nm_part = ia_name_part(&ia_file);
                ia_print_name(&ia_file);
                con_str(" to ");
                print_fname(&cpm);
                /* Note truncation: name part >8 chars means FCB name field is
                 * full and the original has a 9th character that is not a dot. */
                if (src_pf.fcb.f[7] != ' ' && nm_part[8] && nm_part[8] != '.')
                    con_str(" [truncated]");
                if (!ia_copy_ia_to_cpm(&ia_file, &cpm)) {
                    /* Truncation collision that the user declined: show the
                     * same rename hint as the SD long-name skip warning. */
                    if (!g_ferror && src_pf.fcb.f[7] != ' ' &&
                            nm_part[8] && nm_part[8] != '.') {
                        con_str("   To copy: " PROG_NAME " IA:");
                        con_str(nm_part);
                        con_str(" A:NEWNAME.COM\r\n");
                    }
                    continue;
                }

                if (g_opts.verify) {
                    con_str(" - Verifying");
                    if (!crc_file(&cpm)) {
                        con_str("\r\nERROR: can't verify dest\r\n");
                        continue;
                    }
                    if (g_crcval != g_crcval2) {
                        con_str(" FAILED\r\n CRC failed! Please check your disk.\r\n");
                        con_out('\007');
                        continue;
                    }
                    con_str(" OK");
                    if (g_opts.report) { con_str("  CRC: "); con_hex16(g_crcval2); }
                }
                con_nl();

                if (g_opts.movf) {
                    con_str("  - Erasing ");
                    ia_print_name(&ia_file);
                    con_nl();
                    ia_delete(&ia_file);
                }
            }
            return;
        }

        /* Single IA source file */

        /* Bare drive dest (e.g. D: or D6:) â€” use IA source filename (strip path) */
        if (cpm.fcb.f[0] == ' ') {
            saved_dr   = cpm.fcb.dr;
            saved_user = cpm.user;
            if (!make_fcb(ia_name_part(&g_ia_src), &cpm)) {
                con_str("\r\nERROR: invalid IA source filename\r\n");
                return;
            }
            cpm.fcb.dr = saved_dr;
            cpm.user   = saved_user;
        }

        ia_print_name(&g_ia_src);
        con_str(" to ");
        print_fname(&cpm);
        if (!ia_copy_ia_to_cpm(&g_ia_src, &cpm)) return;

        if (g_opts.verify) {
            con_str(" - Verifying");
            if (!crc_file(&cpm)) {
                con_str("\r\nERROR: can't verify dest\r\n");
                return;
            }
            if (g_crcval != g_crcval2) {
                con_str(" FAILED\r\n CRC failed! Please check your disk.\r\n");
                con_out('\007');
                return;
            }
            con_str(" OK");
            if (g_opts.report) { con_str("  CRC: "); con_hex16(g_crcval2); }
        }
        con_nl();

        if (g_opts.movf) {
            con_str("  - Erasing ");
            ia_print_name(&g_ia_src);
            con_nl();
            ia_delete(&g_ia_src);
        }
        return;
    }

    /* CPM -> IA (with CP/M wildcard expansion on the source side) */
    if (!init_pfile(0, &src_tmpl)) return;
    if (expand_wild(&src_tmpl) == 0) {
        con_str(" No file(s) found\r\n");
        return;
    }

    /* Build IA dest template FCB once for wildcard resolution (name part only) */
    if (g_ia_dst.namelen > 0 && ia_has_wild(&g_ia_dst)) {
        if (!make_fcb(ia_name_part(&g_ia_dst), &dst_tmpl)) {
            con_str("\r\nERROR: invalid IA dest pattern\r\n");
            return;
        }
    }

    for (n = 0; n < g_nargc; n++) {
        ia_t *dst_ia;
        if (check_abort()) return;
        bool  dst_is_dir;

        narg = &g_nargbuf[n * FCB_FNAME_LEN];
        cpm.fcb.dr = src_tmpl.fcb.dr;
        cpm.user   = src_tmpl.user;
        for (i = 0; i < 8; i++) cpm.fcb.f[i] = narg[i];
        for (i = 0; i < 3; i++) cpm.fcb.t[i] = narg[8 + i];
        zero_fcb_ctrl(&cpm);

        /* Trailing slash means directory dest e.g. IA:SUBDIR/ */
        dst_is_dir = (g_ia_dst.namelen > 0 &&
                      (g_ia_dst.name[g_ia_dst.namelen - 1] == '/' ||
                       g_ia_dst.name[g_ia_dst.namelen - 1] == '\\'));

        /* Resolve IA destination name:
         *  - wildcard (e.g. IA:DIR/*.SAV):  match_wild + path prefix
         *  - bare IA: or directory IA:DIR/: derive name from src FCB + path prefix
         *  - explicit name (e.g. IA:DIR/OUT.DAT): use as-is */
        if (g_ia_dst.namelen > 0 && ia_has_wild(&g_ia_dst)) {
            match_wild(&src_pf, &dst_tmpl, &cpm);
            ia_name_with_path(&ia_file, &g_ia_dst, &src_pf);
            dst_ia = &ia_file;
        } else if (g_ia_dst.namelen == 0 || dst_is_dir) {
            ia_name_with_path(&ia_file, &g_ia_dst, &cpm);
            dst_ia = &ia_file;
        } else {
            dst_ia = &g_ia_dst;
        }

        print_fname(&cpm);
        con_str(" to ");
        ia_print_name(dst_ia);
        if (!ia_copy_cpm_to_ia(&cpm, dst_ia)) continue;

        if (g_opts.verify) {
            con_str(" - Verifying");
            if (!ia_crc_file(dst_ia)) {
                con_str("\r\nERROR: can't verify IA dest\r\n");
                continue;
            }
            if (g_crcval != g_crcval2) {
                con_str(" FAILED\r\n CRC failed! Please check your IA store.\r\n");
                con_out('\007');
                continue;
            }
            con_str(" OK");
            if (g_opts.report) { con_str("  CRC: "); con_hex16(g_crcval2); }
        }
        con_nl();
        if (g_opts.movf) {
            con_str("  - Erasing ");
            print_fname(&cpm);
            con_nl();
            f_delete(&cpm);
        }
    }
}
#endif /* NABU_IA */

/* ---- SD: argument detector and copy dispatch (Phase 6) ---- */
#ifdef FREHD

static bool is_sd_arg(int idx) {
    const char *s;
    if (idx >= g_argc) return false;
    s = g_argv[idx];
    return (s[0] == 'S' && s[1] == 'D' && s[2] == ':');
}

/* Build SD destination path from g_sd_dst prefix + source FCB filename.
 * g_sd_dst provides the path prefix (everything up to the last '/').
 * fcb provides the 8.3 filename (name + ext). */
static void sd_dst_from_fcb(sd_t *dst, const pfile_t *fcb) {
    uint8_t k = 0;
    uint8_t j;
    uint8_t c;
    uint8_t plen = 0;
    for (j = 0; j < g_sd_dst.namelen; j++)
        if (g_sd_dst.name[j] == '/') plen = (uint8_t)(j + 1);
    for (j = 0; j < plen && k < SD_NAME_LEN - 1; j++)
        dst->name[k++] = g_sd_dst.name[j];
    for (j = 0; j < 8; j++) {
        c = fcb->fcb.f[j] & 0x7F;
        if (c == ' ') break;
        if (k < SD_NAME_LEN - 1) dst->name[k++] = c;
    }
    if ((fcb->fcb.t[0] & 0x7F) != ' ') {
        if (k < SD_NAME_LEN - 1) dst->name[k++] = '.';
        for (j = 0; j < 3; j++) {
            c = fcb->fcb.t[j] & 0x7F;
            if (c == ' ') break;
            if (k < SD_NAME_LEN - 1) dst->name[k++] = c;
        }
    }
    dst->name[k] = '\0';
    dst->namelen = k;
}

static void do_copy_sd(void) {
    pfile_t  cpm;
    pfile_t  src_tmpl;
    pfile_t  dst_tmpl;
    pfile_t  src_pf;
    sd_t     sd_file;
    sd_t     dst_sd;
    uint8_t  i;
    uint16_t n;
    uint16_t cnt;
    uint8_t *narg;
    uint8_t  saved_dr;
    uint8_t  saved_user;

    /* Abort if FreHD is not detected */
    if (!sd_is_frehd()) {
        con_str(" SD: unavailable - FreHD interface not found\r\n");
        return;
    }

    /* SD -> SD: not supported */
    if (g_src_is_sd && g_dst_is_sd) {
        con_str(" ERROR: SD: to SD: copy not supported\r\n");
        return;
    }

    /* SD -> CPM */
    if (g_src_is_sd) {
        if (g_argc < 2) {
            con_str(" Usage: " PROG_NAME " SD:src dest\r\n");
            return;
        }
        if (!init_pfile(1, &cpm)) return;

        if (sd_has_wild(&g_sd_src)) {
            /* Wildcard SD source: enumerate matching files */

            /* Save dest template; bare drive -> all-? wildcard */
            dst_tmpl = cpm;
            if (dst_tmpl.fcb.f[0] == ' ') {
                for (i = 0; i < 8; i++) dst_tmpl.fcb.f[i] = '?';
                for (i = 0; i < 3; i++) dst_tmpl.fcb.t[i] = '?';
            }

            cnt = sd_list_wild(&g_sd_src);
            if (cnt == 0) {
                con_str(" No SD file(s) found\r\n");
                return;
            }
            for (n = 0; n < cnt; n++) {
                if (check_abort()) return;
                sd_list_item(n, &g_sd_src, &sd_file);

                /* Build CP/M FCB from SD filename (no path component) */
                {
                    const char *fn = sd_file.name;
                    uint8_t plen = 0;
                    uint8_t k;
                    /* Find last '/' to get filename part */
                    for (k = 0; k < sd_file.namelen; k++)
                        if (fn[k] == '/' || fn[k] == '\\') plen = (uint8_t)(k + 1);
                    if (!make_fcb(&fn[plen], &src_pf)) {
                        con_str("\r\nERROR: invalid SD filename: ");
                        con_str(sd_file.name);
                        con_nl();
                        continue;
                    }
                }
                match_wild(&cpm, &dst_tmpl, &src_pf);

                sd_print_name(&sd_file);
                con_str(" to ");
                print_fname(&cpm);
                if (!sd_copy_sd_to_cpm(&sd_file, &cpm)) continue;

                if (g_opts.verify) {
                    con_str(" - Verifying");
                    if (!crc_file(&cpm)) {
                        con_str("\r\nERROR: can't verify dest\r\n");
                        f_delete(&cpm);
                        continue;
                    }
                    if (g_crcval != g_crcval2) {
                        con_str(" FAILED\r\n CRC failed! Dest deleted.\r\n");
                        con_out('\007');
                        f_delete(&cpm);
                        continue;
                    }
                    con_str(" OK");
                    if (g_opts.report) { con_str("  CRC: "); con_hex16(g_crcval2); }
                }
                con_nl();

                if (g_opts.movf) {
                    con_str("  - Erasing SD: source not supported\r\n");
                }
            }
            return;
        }

        /* Single SD source file */

        /* Bare drive dest (e.g. D: or D6:) â€” use SD source filename */
        if (cpm.fcb.f[0] == ' ') {
            const char *fn = g_sd_src.name;
            uint8_t plen = 0;
            uint8_t k;
            for (k = 0; k < g_sd_src.namelen; k++)
                if (fn[k] == '/' || fn[k] == '\\') plen = (uint8_t)(k + 1);
            saved_dr   = cpm.fcb.dr;
            saved_user = cpm.user;
            if (!make_fcb(&fn[plen], &cpm)) {
                con_str("\r\nERROR: invalid SD source filename\r\n");
                return;
            }
            cpm.fcb.dr = saved_dr;
            cpm.user   = saved_user;
        }

        sd_print_name(&g_sd_src);
        con_str(" to ");
        print_fname(&cpm);
        if (!sd_copy_sd_to_cpm(&g_sd_src, &cpm)) return;

        if (g_opts.verify) {
            con_str(" - Verifying");
            if (!crc_file(&cpm)) {
                con_str("\r\nERROR: can't verify dest\r\n");
                f_delete(&cpm);
                return;
            }
            if (g_crcval != g_crcval2) {
                con_str(" FAILED\r\n CRC failed! Dest deleted.\r\n");
                con_out('\007');
                f_delete(&cpm);
                return;
            }
            con_str(" OK");
            if (g_opts.report) { con_str("  CRC: "); con_hex16(g_crcval2); }
        }
        con_nl();

        if (g_opts.movf) {
            con_str("  - Erasing SD: source not supported\r\n");
        }
        return;
    }

    /* CPM -> SD (with CP/M wildcard expansion on the source side) */
    if (!init_pfile(0, &src_tmpl)) return;
    if (expand_wild(&src_tmpl) == 0) {
        con_str(" No file(s) found\r\n");
        return;
    }

    for (n = 0; n < g_nargc; n++) {
        if (check_abort()) return;

        narg = &g_nargbuf[n * FCB_FNAME_LEN];
        cpm.fcb.dr = src_tmpl.fcb.dr;
        cpm.user   = src_tmpl.user;
        for (i = 0; i < 8; i++) cpm.fcb.f[i] = narg[i];
        for (i = 0; i < 3; i++) cpm.fcb.t[i] = narg[8 + i];
        zero_fcb_ctrl(&cpm);

        /* Resolve SD destination name:
         *  - bare "SD:"         : sd_dst_from_fcb derives from CPM FCB
         *  - "SD:DIR/"          : sd_dst_from_fcb prepends path + FCB name
         *  - explicit "SD:name" : use as-is
         *  - wildcard "SD:*.X"  : resolve via match_wild then sd_dst_from_fcb */
        if (g_sd_dst.namelen == 0 ||
            g_sd_dst.name[g_sd_dst.namelen - 1] == '/') {
            sd_dst_from_fcb(&dst_sd, &cpm);
        } else {
            dst_sd = g_sd_dst;
        }

        /* Wildcard dest: strip path prefix, resolve filename part via FCB,
         * then sd_dst_from_fcb re-prepends the path from g_sd_dst. */
        if (g_sd_dst.namelen > 0 && sd_has_wild(&g_sd_dst)) {
            /* Find start of wildcard filename (after last '/') */
            saved_dr = 0;
            for (i = 0; i < g_sd_dst.namelen; i++)
                if (g_sd_dst.name[i] == '/') saved_dr = (uint8_t)(i + 1);
            if (!make_fcb(&g_sd_dst.name[saved_dr], &dst_tmpl)) {
                con_str("\r\nERROR: invalid SD dest pattern\r\n");
                return;
            }
            match_wild(&src_pf, &dst_tmpl, &cpm);
            sd_dst_from_fcb(&dst_sd, &src_pf);
        }

        print_fname(&cpm);
        con_str(" to ");
        sd_print_name(&dst_sd);
        if (!sd_copy_cpm_to_sd(&cpm, &dst_sd)) {
            if (g_sd_create_err) return;  /* directory missing or unwritable - abort */
            continue;
        }

        if (g_opts.verify) {
            con_str(" - Verifying");
            if (!sd_crc_file(&dst_sd)) {
                con_str("\r\nERROR: can't verify SD dest\r\n");
                continue;
            }
            if (g_crcval != g_crcval2) {
                con_str(" FAILED\r\n CRC failed! Please check your SD card.\r\n");
                con_out('\007');
                continue;
            }
            con_str(" OK");
            if (g_opts.report) { con_str("  CRC: "); con_hex16(g_crcval2); }
        }
        con_nl();

        if (g_opts.movf) {
            con_str("  - Erasing ");
            print_fname(&cpm);
            con_nl();
            f_delete(&cpm);
        }
    }
}
#endif /* FREHD */

/* ---- Main copy loop (Phase 2: wildcard expansion) ---- */
static void do_copy(void) {
    pfile_t  src_tmpl, dst_tmpl, src, dst, last_dst;
    uint16_t n;
    uint8_t  i;

#ifdef NABU_IA
    /* Route to IA handler if either argument carries an "IA:" prefix */
    if (is_ia_arg(0) || is_ia_arg(1)) {
        g_src_is_ia = is_ia_arg(0);
        g_dst_is_ia = is_ia_arg(1);
        if (g_src_is_ia) ia_init(&g_ia_src, g_argv[0] + 3);
        if (g_dst_is_ia) ia_init(&g_ia_dst, g_argv[1] + 3);
        do_copy_ia();
        return;
    }
#endif

#ifdef FREHD
    /* Route to SD handler if either argument carries an "SD:" prefix */
    if (is_sd_arg(0) || is_sd_arg(1)) {
        g_src_is_sd = is_sd_arg(0);
        g_dst_is_sd = is_sd_arg(1);
        if (g_src_is_sd) sd_init(&g_sd_src, g_argv[0] + 3);
        if (g_dst_is_sd) sd_init(&g_sd_dst, g_argv[1] + 3);
        do_copy_sd();
        return;
    }
#endif

    /* Build source template */
    if (!init_pfile(0, &src_tmpl)) return;

    /* Build destination template */
    if (g_argc >= 2) {
        if (!init_pfile(1, &dst_tmpl)) return;
    } else {
        /* No dest: copy to current drive/user, keeping source names */
        dst_tmpl.user   = g_user;
        dst_tmpl.fcb.dr = (uint8_t)(g_drive + 1);
        for (i = 0; i < 8; i++) dst_tmpl.fcb.f[i] = '?';
        for (i = 0; i < 3; i++) dst_tmpl.fcb.t[i] = '?';
    }

    /* Pure DU: dest (no filename specified): copy source name to that area */
    if (dst_tmpl.fcb.f[0] == ' ') {
        for (i = 0; i < 8; i++) dst_tmpl.fcb.f[i] = '?';
        for (i = 0; i < 3; i++) dst_tmpl.fcb.t[i] = '?';
    }

    /* Expand source (handles both wildcard and exact names) */
    if (expand_wild(&src_tmpl) == 0) {
        con_str(" No file(s) found\r\n");
        return;
    }

    /* Sentinel: last_dst starts as invalid so first file is never a duplicate */
    clear_fcb(&last_dst);
    last_dst.user = 0xFF;

    for (n = 0; n < g_nargc; n++) {
        uint8_t *narg = &g_nargbuf[n * FCB_FNAME_LEN];
        if (check_abort()) return;

        /* Build actual src pfile_t from the directory entry in nargbuf */
        src.fcb.dr = src_tmpl.fcb.dr;
        src.user   = src_tmpl.user;
        for (i = 0; i < 8; i++) src.fcb.f[i] = narg[i];
        for (i = 0; i < 3; i++) src.fcb.t[i] = narg[8 + i];
        zero_fcb_ctrl(&src);

        /* Resolve destination name against this source entry */
        match_wild(&dst, &dst_tmpl, &src);

        /* Skip if source and destination are the same file */
        if (fname_equal(&src, &dst)) {
            print_fname(&src);
            con_str(" same\r\n");
            continue;
        }

        /* Skip if this dest was already written this run (duplicate) */
        if (fname_equal(&dst, &last_dst)) {
            print_fname(&dst);
            con_str(" Duplicate!\r\n");
            continue;
        }

        copy_one(&src, &dst);
        last_dst = dst;
    }
}

/* ---- Program entry point ---- */
void main(void) {
    bool zrdos;

    /* Claim free TPA for I/O buffers -- must be first, before any file I/O.
     *
     * CP/M stores the BDOS entry address at 0x0006 (low) / 0x0007 (high).
     * z88dk initialises SP to that address; the stack grows DOWN from there.
     * We must reserve STACK_RESERVE bytes at the top of TPA for the stack
     * before claiming the rest for our buffers.  Without the reserve, the
     * stack immediately overwrites the top of g_iobuf on the first call.
     *
     * Remaining free space is sliced into:
     *   g_nargbuf  -- fixed-size wildcard name table (MAX_NARG * 11 bytes)
     *   g_iobuf    -- all remaining space, capped at MAX_IOBUF_RECS records
     *
     * This removes ~22KB of zero-filled BSS from the .COM image, cutting
     * load time nearly in half while giving a larger I/O buffer at runtime.
     */
    {
        extern uint8_t _BSS_END_tail[];   /* SDCC adds one '_', linker symbol is __BSS_END_tail */
        uint8_t  *tpa    = _BSS_END_tail;
        uint16_t  free_b = (uint16_t)((uint8_t*)(*(uint16_t*)0x0006) - tpa);
        uint16_t  narg_b = (uint16_t)(MAX_NARG * FCB_FNAME_LEN);

        /* Reserve stack space at top of TPA -- stack grows down from BDOS */
        if (free_b > STACK_RESERVE) free_b -= STACK_RESERVE;
        else free_b = 0;

        g_nargbuf    = tpa;
        g_iobuf      = tpa + narg_b;
        g_iobuf_recs = (free_b > narg_b)
                       ? (uint16_t)((free_b - narg_b) / REC_SIZE)
                       : 0;
        if (g_iobuf_recs > MAX_IOBUF_RECS) g_iobuf_recs = MAX_IOBUF_RECS;
    }

    /* Version banner â€” shows clean version; build number in /H help title */
#if defined(NABU_DEFAULT)
    con_str(PROG_NAME " v" PPIP_VERSION " NABU Edition");
    if (ia_is_nabu()) con_str(" [CloudCP/M]");
    con_str("\r\n");
#elif defined(FREHD_DEFAULT)
    con_str(PROG_NAME " v" PPIP_VERSION " FreHD Edition");
    con_str(sd_is_frehd() ? " [FreHD Detected]" : " [FreHD not found]");
    con_str("\r\n");
#else
    con_str(PROG_NAME " v" PPIP_VERSION);
#if defined(NABU_IA)
    if (ia_is_nabu()) con_str(" [CloudCP/M]");
#endif
#if defined(FREHD)
    if (sd_is_frehd()) con_str(" [FreHD Detected]");
#endif
    con_str("\r\n");
#endif

    /* Init CRC tables */
    crc_init();

    /* Save current drive and user */
    get_du(&g_drive, &g_user);

    /* Parse command tail (tokenise args and options) */
    parse_cmdline();

#ifdef NABU_DEFAULT
    /* NABU edition: IA always active regardless of /N */
    g_opts.nabu_ia = true;
#endif

    /* Show help if no arguments given or /H requested */
    if (g_argc == 0 || g_want_help) {
        print_help();
        goto done;
    }

    /* Check ZRDOS: if running under ZRDOS, skip disk reset */
    zrdos = ((uint8_t)bdos(BDOS_ZRDVER, 0) != 0);
    if (!zrdos) {
        bdos(BDOS_RSTDSK, 0);
    }

    /* CON: source â€” keyboard to file */
    if (is_con(g_argv[0])) {
        do_con_copy();
        goto done;
    }

    /* Normal file copy */
    do_copy();

done:
    /* Restore original drive and user before returning to CP/M */
    set_du(g_drive, g_user);
}

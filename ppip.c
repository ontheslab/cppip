/* ppip.c - CPPIP - CP/M File Copy Utility
   A C reimplementation of PPIP v1.8 (D. Jewett III, 1985-1988).
   Original Z80 assembly source preserved in PPIP Master/.
   C port by Intangybles (c)2026.

   Compile CP/M (standard — TRS-80 Model 4P, Kaypro, generic CP/M 2.2):
     zcc +cpm -vn -create-app -compiler=sdcc --opt-code-size \
         ppip.c cmdparse.c filename.c diskio.c crc.c console.c -o CPPIP

   Compile NABU CloudCP/M (adds IA: RetroNET file store support):
     zcc +cpm -vn -create-app -compiler=sdcc --opt-code-size \
         -DNABU_IA -I../NABULIB \
         ppip.c cmdparse.c filename.c diskio.c crc.c console.c iaio.c -o CPPIP

   Target platforms : CP/M 2.2  — NABU, TRS-80 Model 4P, Kaypro
   Compiler         : z88dk with SDCC backend (Z80, 64KB address space)

   -----------------------------------------------------------------------
   Version 1.0.15 (current)
   - IA filenames from rn_fileListItem() forced to uppercase; ia_init()
     also uppercases on entry. Prevents case mismatch between files copied
     to and from the IA store (Windows server is case-preserving).
   - CPPIP.COM ~47,928 bytes (NABU build).

   Version 1.0.14
   - CPM->IA wildcard fix: bare "IA:" destination now derives each
     destination filename from the CP/M source FCB (e.g. *.DAT -> IA:
     copies each file as IA:NAME.DAT). Previously sent a 0-length filename
     to the IA store, causing a comms error and lockup.
   - IA->CPM wildcard expansion: IA:*.DAT D: now works. Uses rn_fileList()
     and rn_fileListItem() to enumerate matching files on the IA store,
     then copies each to the CP/M destination with name derived from the
     IA filename. /V, /C, and /M all work per-file in the wildcard loop.

   Version 1.0.13
   - /V CRC verify added for CPM->IA copies. After writing to the IA
     store, ia_crc_file() re-reads the file back and compares CRC against
     the source pass. Previously verify was silently ignored for CPM->IA.

   Version 1.0.10 / 1.0.11
   - First live IA: development builds. Initial IA->CPM, CPM->IA, and
     CON: editor testing on real NABU hardware and MAME emulation.
     Several options and behaviours adjusted during this phase; versions
     rolled back to 1.0.09 baseline and reworked cleanly into 1.0.12.

   Version 1.0.12
   - NABU detection: ia_is_nabu() wraps isCloudCPM() (checks CloudCP/M
     BIOS flag at 0xFF29). IA: operations blocked on non-NABU systems
     (prevents TRS-80 lockup via spurious HCCA port access). /N option
     allows override on non-CloudCP/M NABU systems (ZSDOS etc.).
   - Help now shows [NABU Detected] or [use /N to enable] dynamically
     next to the IA: prefix description.
   - /A (archive) option removed. CP/M 2.2 never sets or clears the
     archive bit automatically, making the option impractical.
   - /H added for help (replaces bare "/" which looked odd in output).
   - IA->CPM bare drive dest fixed (e.g. IA:FRED.COM D: now copies to
     D:FRED.COM instead of D:. ).
   - /V verify wired up for IA->CPM (source CRC accumulated during read,
     dest re-read via crc_file() and compared after copy).
   - "Intangybles" brand name spelling corrected throughout.

   Version 1.0.09
   - Phase 5: NABU IA RetroNET file store extension.
     IA: prefix on source or destination routes to do_copy_ia().
     New iaio.c/h wraps RetroNET-FileStore.h (included once, in iaio.c).
     Three copy routes: CPM->CPM (unchanged), CPM->IA, IA->CPM.
     ia_open_wr() handles Exists!/Delete? prompt for IA destinations.
     IA->CPM: last partial record padded with ^Z per CP/M convention.
     All IA code gated by -DNABU_IA; plain CP/M builds unchanged.
   - Phase 4: CON: as source. con_in_ne() loop with backspace, ~ escape
     for control characters, ^Z to end; output padded to 128-byte records.
   - MAX_ARG_LEN increased from 20 to 32 to accommodate IA filenames.

   Version 1.0.05
   - Phase 3: CRC-16 CCITT verify fully wired up. /V triggers crc_file()
     on dest after copy and compares against source CRC (g_crcval2).
     Retry loop: up to CRC_RETRIES (3) attempts on mismatch; bad dest
     deleted between retries; bell + error message on final failure.
   - Output format matched original PPIP v1.7 exactly: all activity on
     one line ("src to dst Exists! Delete? y - Verifying OK  CRC: xxxx").
   - /V/C and /VC option concatenation fixed (inner scan stops at /).
   - /E, /W, /M all tested and working.

   Version 1.0.01
   - Phase 2: wildcard expansion via BDOS SRCHFST/SRCHNXT.
     expand_wild() fills g_nargbuf with up to MAX_NARG (512) matches.
     match_wild() resolves ? in dest template against each source name.
     Duplicate dest detection via last_dst sentinel.
   - do_copy() split into do_copy() loop and copy_one() per-file handler.
   - Binary renamed CPPIP.COM to distinguish the C port from original.

   Version 1.1
   - Two CP/M compatibility bugs fixed on first NABU deployment:
     (1) make_fcb() rewritten without goto — SDCC miscompiles goto
         crossing a block-local variable declaration on Z80, producing
         garbage FCB names for both source and destination.
     (2) NABU CloudCP/M CCP uses null byte (not space) as argument
         separator in command tail at 0x0080. Fixed by normalising any
         control character to space during command tail copy.

   Version 1.0 — initial C port
   - Phase 1: single file copy with DU: prefix (drive A-P, user 0-31).
     Own ppip_fcb_t (36 bytes) — z88dk struct fcb is 180+ bytes.
     pfile_t stores user byte before FCB, mirroring original FCB[-1].
     Static 16KB I/O buffer (128 x 128-byte records).
     ZRDOS detection: skips disk reset if bdos(48,0) returns non-zero.
     Attribute copy: R/O and archive bits propagated from src to dest FCB.
*/

/* NABU-specific defines (harmless on plain CP/M if NABU-LIB not included) */
/* #define BIN_TYPE BIN_CPM     */
/* #define DISABLE_VDP          */
/* #define DISABLE_KEYBOARD_INT */

#include "ppip.h"
#include "cmdparse.h"
#include "filename.h"
#include "diskio.h"
#include "console.h"
#include "crc.h"
#include "iaio.h"

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

uint8_t   g_iobuf[IOBUF_SIZE];
uint16_t  g_iobuf_recs = IOBUF_RECORDS;

/* Wildcard expansion buffer (Phase 2) */
uint8_t   g_nargbuf[MAX_NARG * FCB_FNAME_LEN];
uint16_t  g_nargc = 0;

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

    /* Print "src to dst" — no newline yet, rest of line filled in below */
    print_fname(src);
    con_str(" to ");
    print_fname(dst);

    if (g_opts.verify) {
        /* CRC verify loop — retry up to CRC_RETRIES times on mismatch */
        for (retry = 0; retry < CRC_RETRIES; retry++) {
            if (!f_copy(src, dst)) return;  /* error msg inside has its own \r\n */

            con_str(" - Verifying");

            /* g_crcval2 = source CRC (saved by f_copy), g_crcval will = dest CRC */
            if (!crc_file(dst)) {
                con_str("\r\nERROR: can't verify dest\r\n");
                return;
            }

            if (g_crcval == g_crcval2) break;  /* CRC match — done */

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

        c = con_in_ne();    /* no echo — we control all output */

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

/* ---- IA: argument detector ---- */
#ifdef NABU_IA

/* Build an ia_t name from a CP/M FCB (f[]+t[] -> "NAME.EXT") */
static void ia_name_from_fcb(ia_t *out, const pfile_t *src) {
    uint8_t i, k = 0;
    for (i = 0; i < 8; i++) {
        uint8_t c = src->fcb.f[i] & 0x7F;
        if (c == ' ') break;
        out->name[k++] = c;
    }
    if ((src->fcb.t[0] & 0x7F) != ' ') {
        out->name[k++] = '.';
        for (i = 0; i < 3; i++) {
            uint8_t c = src->fcb.t[i] & 0x7F;
            if (c == ' ') break;
            out->name[k++] = c;
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
    pfile_t  cpm;
    pfile_t  src_tmpl;
    ia_t     ia_file;
    uint8_t  i;
    uint16_t n;
    uint16_t cnt;
    uint8_t *narg;
    uint8_t  saved_dr;
    uint8_t  saved_user;

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
            cnt = ia_list_wild(&g_ia_src);
            if (cnt == 0) {
                con_str(" No IA file(s) found\r\n");
                return;
            }
            for (n = 0; n < cnt; n++) {
                ia_list_item(n, &ia_file);

                /* Build CPM dest filename from IA source name */
                saved_dr   = cpm.fcb.dr;
                saved_user = cpm.user;
                if (!make_fcb(ia_file.name, &cpm)) {
                    con_str("\r\nERROR: invalid IA filename: ");
                    con_str(ia_file.name);
                    con_nl();
                    continue;
                }
                cpm.fcb.dr = saved_dr;
                cpm.user   = saved_user;

                ia_print_name(&ia_file);
                con_str(" to ");
                print_fname(&cpm);
                if (!ia_copy_ia_to_cpm(&ia_file, &cpm)) return;

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
                    ia_print_name(&ia_file);
                    con_nl();
                    ia_delete(&ia_file);
                }
            }
            return;
        }

        /* Single IA source file */

        /* Bare drive dest (e.g. D: or D6:) — use IA source filename */
        if (cpm.fcb.f[0] == ' ') {
            saved_dr   = cpm.fcb.dr;
            saved_user = cpm.user;
            if (!make_fcb(g_ia_src.name, &cpm)) {
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
    for (n = 0; n < g_nargc; n++) {
        ia_t *dst_ia;

        narg = &g_nargbuf[n * FCB_FNAME_LEN];
        cpm.fcb.dr = src_tmpl.fcb.dr;
        cpm.user   = src_tmpl.user;
        for (i = 0; i < 8; i++) cpm.fcb.f[i] = narg[i];
        for (i = 0; i < 3; i++) cpm.fcb.t[i] = narg[8 + i];
        zero_fcb_ctrl(&cpm);

        /* Bare "IA:" — derive destination filename from CPM source FCB */
        if (g_ia_dst.namelen == 0) {
            ia_name_from_fcb(&ia_file, &cpm);
            dst_ia = &ia_file;
        } else {
            dst_ia = &g_ia_dst;
        }

        print_fname(&cpm);
        con_str(" to ");
        ia_print_name(dst_ia);
        if (!ia_copy_cpm_to_ia(&cpm, dst_ia)) return;

        if (g_opts.verify) {
            con_str(" - Verifying");
            if (!ia_crc_file(dst_ia)) {
                con_str("\r\nERROR: can't verify IA dest\r\n");
                return;
            }
            if (g_crcval != g_crcval2) {
                con_str(" FAILED\r\n CRC failed! Please check your IA store.\r\n");
                con_out('\007');
                return;
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

    /* Version banner — always shown so we can confirm which build is running */
    con_str("CPPIP v" PPIP_VERSION "\r\n");

    /* Init CRC tables */
    crc_init();

    /* Save current drive and user */
    get_du(&g_drive, &g_user);

    /* Parse command tail (tokenise args and options) */
    parse_cmdline();

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

    /* CON: source — keyboard to file */
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

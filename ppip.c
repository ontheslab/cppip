/* ppip.c - CPPIP/NPPIP - CP/M File Copy Utility
   A C reimplementation of PPIP v1.8 (D. Jewett III, 1985-1988).
   Original Z80 assembly source preserved in PPIP Master/.
   C port by Intangybles (c)2026.

   Build (default — produces both CPPIP.COM and NPPIP.COM):
     build.bat

   Build manually (inside Claude Code / Git Bash):
     CPPIP.COM:
       zcc +cpm -vn -create-app -compiler=sdcc --opt-code-size -DNABU_IA \
           -I../NABULIB \
           ppip.c cmdparse.c filename.c diskio.c crc.c iaio.c console.c -o CPPIP
     NPPIP.COM (NABU edition — IA always on):
       zcc +cpm -vn -create-app -compiler=sdcc --opt-code-size -DNABU_IA -DNABU_DEFAULT \
           -I../NABULIB \
           ppip.c cmdparse.c filename.c diskio.c crc.c iaio.c console.c -o NPPIP

   Target platforms : CP/M 2.2  — NABU, TRS-80 Model 4P, Kaypro
   Compiler         : z88dk with SDCC backend (Z80, 64KB address space)

   Version numbering: 1.00 (NN) — major.minor (build)
   Increment build number NN for each new test build.

   -----------------------------------------------------------------------
   Version 1.00 (32) — current
   - Fix: TPA allocator reserved no space for the stack. z88dk CP/M startup
     sets SP to the BDOS address (top of TPA); stack grows down from there.
     Allocating all free TPA for g_iobuf left the stack nowhere to go,
     causing immediate buffer corruption on the first NABULIB call (lockups
     and zero-length copies). Fix: subtract STACK_RESERVE (1024 bytes) from
     free TPA before sizing the I/O buffer.

   Version 1.00 (31)
   - TPA buffer allocation: g_iobuf and g_nargbuf moved from static BSS to
     free TPA claimed at startup. CPPIP.COM shrinks from ~49.8KB to ~28KB
     (44% reduction). I/O buffer grows from 128 records to ~175 at runtime
     on a typical CloudCP/M system. Uses __BSS_END_tail linker symbol for
     program end; BDOS base read from CP/M page zero (0x0006).
   - Fix: BDOS 11 (CONSTAT) returns a false positive after HCCA/IA
     communication on CloudCP/M. The ask_delete() type-ahead drain and
     check_abort() both used BDOS 11; replaced with direct BDOS 6 (DIRIO,
     0xFF) calls, which return 0 immediately when no key is waiting and
     are reliable on all CP/M 2.2 variants.
   - Keyboard buffer drained before each "Exists!/RO! Delete?" prompt so
     rapid keypresses cannot silently auto-answer the next confirmation.

   Version 1.00 (30)
   - Fix: Ctrl-C at "Exists! Delete?" prompt now triggers a CP/M warm boot
     instead of being silently treated as N and continuing the batch copy.
     CloudCP/M's BDOS 1 (CONIN) passes 0x03 to the application; ask_delete()
     now detects it and jumps to 0x0000 (standard CP/M warm boot vector).

   Version 1.00 (29)
   - Fix: batch IA copy loop (CPM->IA and IA->CPM) no longer stops at the
     first skip/error. Inner-loop error paths used "return" (exiting the
     whole function) instead of "continue" (skipping to the next file).
     Saying N to "Exists! Delete?" during a wildcard IA copy now skips
     that file and continues with the rest, matching CPM->CPM behaviour.

   Version 1.00 (28)
   - Dual-binary build: CPPIP.COM (standard, /N for IA) and NPPIP.COM
     (NABU edition, IA always active, CloudCP/M tag in startup banner).
   - Version format changed to 1.00 (NN): major.minor plus build number.
   - PROG_NAME and PPIP_VER_STR macros unify banner/help across both builds.

   Version 1.00 (27)
   - IA wildcard batch copy (IA:DIR/*.* D:) now works past the first file.
     The NIA server has one global file-list context shared by all
     rn_fileList() calls. ia_check_dirs() (called from ia_open_rd() during
     each file's copy) issues its own rn_fileList(DIRS,...) calls which
     clobber the INCLUDE_FILES list set up by ia_list_wild(). On the next
     loop iteration, ia_list_item(n) tried to index into the dir-check
     list (1 entry) rather than the file list, causing
     "rn_fileListItem requested index N but there are only 1 items".
     Fix: re-call ia_list_wild() at the top of each loop iteration to
     restore the server's file-list context before ia_list_item(n).

   Version 1.0.26 — NIA server crash prevention and path fixes

   Two root problems were found and fixed (v1.0.20-1.0.26):

   PROBLEM 1: Copying to/from a subdirectory that does not exist crashes
   the NIA server and hangs the NABU indefinitely.

   Root cause: the NIA server (NABU-Internet-Adapter .NET app) throws
   DirectoryNotFoundException and drops the TCP connection when
   rn_fileOpen() is called for a path whose directory does not exist.
   The NABU then hangs forever in hcca_DiReadByte() waiting for a
   response that never comes. A handle check after rn_fileOpen() is
   useless — rn_fileOpen() never returns.

   The NIA server has two distinct code paths for rn_FileOpen():
     - Drive-letter paths (e.g. Z:\TEST\FILE): server calls
       Directory.CreateDirectory() first, then opens. Safe always.
     - Plain paths (e.g. TEST\FILE): server goes straight to File.Open().
       If the directory is missing it crashes. Undocumented behaviour;
       discovered by disassembly of fstest.NABU and analysis of the NIA
       server binary.

   Solution: two-part.
   a) ia_check_dirs() — before any rn_fileOpen() on a plain path (read
      or write), each directory component is verified with rn_fileList
      (INCLUDE_DIRECTORIES), walking level by level. rn_fileList returns
      0 for a missing directory without crashing. If any component is
      missing, a clear error is printed and rn_fileOpen is never called.
   b) Drive-letter paths — ia_init() detects X:/ prefix and converts '/'
      to '\\', sending the path to the server as "X:\DIR\FILE". The
      server auto-creates the directory tree. ia_check_dirs() is skipped
      for these paths. Also recognised: /X/ Unix-style prefix (e.g.
      /D/1/FILE → D:\1\FILE) — same server code path, friendlier to type.
      A bare leading '/' not matching /X/ is stripped to prevent the
      server mapping it to its Windows drive root and crashing.
      ia_init() refactored to a src-pointer + output-index loop to keep
      prefix detection and the main copy loop clean.

   PROBLEM 2: Files stored with lowercase names on the IA store are not
   found when read back, because rn_fileSize() does case-sensitive lookup
   internally. Querying "ARCOPY.ZIP" for a file stored as "arcopy.zip"
   returns -1 (not found) even though Windows NTFS is case-insensitive.

   Solution: ia_exists()/rn_fileSize() removed from ia_open_rd() entirely.
   rn_fileOpen(READONLY) uses the Windows OS File.Open() which IS
   case-insensitive and finds the file regardless of stored case. The
   returned handle (0xFF = failure) is checked instead. ia_check_dirs()
   is still called for plain paths on reads to catch missing directories
   before rn_fileOpen() is invoked.

   Version 1.0.19
   - IA subfolder/path support. Source and destination can now include
     directory components: IA:FRED/FILE.DAT, IA:A/B/C/*.DAT, IA:SUBDIR/.
     ia_list_wild() splits path/wildcard on last separator and caches the
     path prefix in s_list_path[] so ia_list_item() returns full paths.
     ia_name_with_path() builds "PATH/NAME.EXT" from a template + FCB for
     CPM->IA copies. ia_name_part() strips the path prefix before make_fcb()
     for IA->CPM copies. Any depth supported: "A/B/C/*.SAV" etc.
   - IA_NAME_LEN increased from 32 to 64 to accommodate multi-level paths.

   Version 1.0.18
   - Options changed from toggle to set-only. Typing /V/V or /V/V/V
     simply means verify on — repeated switches no longer cancel out.
   - CPM->IA wildcard dest (e.g. *.COM IA:*.SAV) now resolves correctly
     via match_wild — previously IA:*.SAV was used as a literal filename.
   - IA->CPM wildcard dest template now saved before loop and resolved
     per-file via match_wild (same fix, other direction).

   Version 1.0.15
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

   Version 1.0.10 / 1.0.11
   - First live IA: development builds. Initial IA->CPM, CPM->IA, and
     CON: editor testing on real NABU hardware and MAME emulation.
     Several options and behaviours adjusted during this phase; versions
     rolled back to 1.0.09 baseline and reworked cleanly into 1.0.12.

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
        pfxlen = path_tmpl->namelen;        /* "SUBDIR/" — use whole name */
    } else {
        pfxlen = ia_path_len(path_tmpl);    /* "SUBDIR/*.SAV" — up to last / */
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

/* ---- IA copy dispatch (Phase 5) ---- */
static void do_copy_ia(void) {
    pfile_t  cpm;
    pfile_t  src_tmpl;
    pfile_t  dst_tmpl;
    pfile_t  src_pf;
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

                ia_print_name(&ia_file);
                con_str(" to ");
                print_fname(&cpm);
                if (!ia_copy_ia_to_cpm(&ia_file, &cpm)) continue;

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

        /* Bare drive dest (e.g. D: or D6:) — use IA source filename (strip path) */
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

    /* Version banner — shows clean version; build number in /H help title */
#ifdef NABU_DEFAULT
    con_str(PROG_NAME " v" PPIP_VERSION " NABU Edition");
    if (ia_is_nabu()) con_str(" [CloudCP/M]");
    con_str("\r\n");
#else
    con_str(PROG_NAME " v" PPIP_VERSION "\r\n");
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

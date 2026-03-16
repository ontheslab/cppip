#include "ppip.h"
#include "cmdparse.h"
#include "filename.h"
#include "console.h"

/*
 * Process a single option character after the '/' switch.
 * All options are set-only (cannot be toggled back off from the command
 * line). /M also forces /V on.
 */
static void process_option(char c) {
    /* Convert to uppercase */
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');

    switch (c) {
        case 'V': g_opts.verify  = true; break;
        case 'C': g_opts.report  = true; break;
        case 'E': g_opts.emend   = true; break;
        case 'W': g_opts.wipe    = true; break;
        case 'H': g_want_help    = true; break;
        case 'M': g_opts.movf    = true; g_opts.verify = true; break;
#ifdef NABU_IA
        case 'N': g_opts.nabu_ia = true; break;
#endif
        default:
            con_out(c);
            con_str(" - unrecognised option\r\n");
    }
}

/*
 * Convert a character to uppercase.
 */
static char to_upper(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
    return c;
}

/*
 * Parse the CP/M command tail from address 0x0080.
 *
 * Byte 0x0080: character count
 * Bytes 0x0081..: the command tail (may start with a space)
 *
 * Tokenises into g_argc / g_argv[] / g_argbuf[][].
 * Option switches (/V etc.) are consumed and applied; they do not
 * appear as arguments.
 *
 * CP/M mode: if '=' is found in the tail, argv[0] and argv[1] are
 * swapped after parsing (dest=src becomes src, dest).
 * PIP compatibility.
 */
void parse_cmdline(void) {
    volatile uint8_t *cb = (volatile uint8_t*)0x0080;
    uint8_t len = cb[0];
    char    workbuf[130];
    char   *p;
    bool    has_equal = false;
    uint8_t i;

    g_argc = 0;

    if (len == 0) return;

    /* Copy command tail to working buffer, convert to uppercase.
     * NABU CP/M CCP separates arguments with null or CR rather than
     * space - normalise any control character to a space so the
     * tokeniser can find all arguments. */
    for (i = 0; i < len && i < 128; i++) {
        uint8_t ch = cb[i + 1];
        if (ch < ' ') ch = ' ';   /* null/CR/any ctrl -> space */
        workbuf[i] = to_upper((char)ch);
    }
    workbuf[i] = '\0';

    /* Scan for '=' to detect CP/M mode */
    for (i = 0; workbuf[i]; i++)
        if (workbuf[i] == '=') { has_equal = true; break; }

    /* Tokenise */
    p = workbuf;
    while (*p && g_argc < MAX_ARG) {
        char    *dst;
        uint8_t  k;

        /* Skip whitespace */
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '\r') break;

        /* '=' is a delimiter between dest and src in CP/M mode; skip it */
        if (*p == '=') { p++; continue; }

        /* Option switch */
        if (*p == SWITCH_CHAR) {
            p++;  /* skip '/' */
            while (*p && *p != ' ' && *p != '\t' && *p != '=' && *p != SWITCH_CHAR)
                process_option(*p++);
            continue;
        }

        /* Regular argument: copy until delimiter */
        dst = g_argbuf[g_argc];
        k = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != '=' && k < (uint8_t)(MAX_ARG_LEN - 1)) {
            dst[k++] = *p++;
        }
        dst[k] = '\0';

        if (k > 0) {
            g_argv[g_argc] = dst;
            g_argc++;
        }
    }

    /* CP/M mode: swap argv[0] and argv[1] (dest=src -> src, dest) */
    if (has_equal && g_argc >= 2) {
        char *tmp  = g_argv[0];
        g_argv[0]  = g_argv[1];
        g_argv[1]  = tmp;
    }
}

/*
 * Build a pfile_t from g_argv[argv_idx].
 * Parses DU: prefix and builds the FCB.
 * Returns false on error (bad DU: spec or invalid filename).
 *
 * Note: make_fcb calls clear_fcb which resets fcb.dr to 0.
 * We save drive and user from parse_du and restore them after make_fcb.
 */
bool init_pfile(int argv_idx, pfile_t *pf) {
    const char *arg;
    const char *fname;
    uint8_t     saved_dr;
    uint8_t     saved_user;

    if (argv_idx >= g_argc) return false;
    arg = g_argv[argv_idx];

    /* Default drive/user */
    pf->user   = g_user;
    pf->fcb.dr = (uint8_t)(g_drive + 1);

    /* Parse DU: prefix (sets pf->user and pf->fcb.dr if prefix present) */
    fname = parse_du(arg, pf);
    if (!fname) {
        con_str("\r\nERROR: bad drive/user spec in ");
        con_str(arg);
        con_nl();
        return false;
    }

    /* Save drive/user before make_fcb calls clear_fcb and resets dr to 0 */
    saved_dr   = pf->fcb.dr;
    saved_user = pf->user;

    /* Build FCB from plain filename (clear_fcb inside resets dr to 0) */
    if (!make_fcb(fname, pf)) {
        con_str("\r\nERROR: invalid characters in ");
        con_str(arg);
        con_nl();
        return false;
    }

    /* Restore drive and user that were cleared by make_fcb/clear_fcb */
    pf->fcb.dr = saved_dr;
    pf->user   = saved_user;

    return true;
}

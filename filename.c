#include "ppip.h"
#include "filename.h"
#include "diskio.h"
#include "console.h"

/* Characters illegal in CP/M filenames (from ILLTBL in ppip-2.z80) */
static const char illegal_chars[] = ",;:=\'\"";

bool valid_fcb_char(char c) {
    uint8_t i;
    if (c <= ' ') return false;   /* must be printable and not space */
    for (i = 0; illegal_chars[i]; i++)
        if (c == illegal_chars[i]) return false;
    return true;
}

/* Fill FCB name+type with spaces, zero all control fields */
void clear_fcb(pfile_t *pf) {
    uint8_t i;
    pf->fcb.dr = 0;
    for (i = 0; i < 8; i++) pf->fcb.f[i] = ' ';
    for (i = 0; i < 3; i++) pf->fcb.t[i] = ' ';
    zero_fcb_ctrl(pf);
}

/* Zero only the control fields (extent, allocation, record counters) */
void zero_fcb_ctrl(pfile_t *pf) {
    uint8_t i;
    pf->fcb.ex = 0;
    pf->fcb.s1 = 0;
    pf->fcb.s2 = 0;
    pf->fcb.rc = 0;
    for (i = 0; i < 16; i++) pf->fcb.al[i] = 0;
    pf->fcb.cr = 0;
    pf->fcb.r0 = 0;
    pf->fcb.r1 = 0;
    pf->fcb.r2 = 0;
}

/*
 * Build an FCB from a plain filename (no DU: prefix).
 * name must already be upper-case.
 * Returns false if invalid characters found.
 * Note: calls clear_fcb which resets dr to 0; caller must restore dr and user.
 *
 * Written without goto to avoid SDCC codegen issues with goto crossing
 * local variable declarations inside for loops - corrected errors.
 */
bool make_fcb(const char *name, pfile_t *pf) {
    const char *p = name;
    const char *ext;
    uint8_t     i;
    char        c;

    clear_fcb(pf);

    /* --- Pass 1: name field (up to 8 chars, stop at '.', ' ', NUL) --- */
    for (i = 0; i < 8; i++) {
        c = *p;
        if (c == '\0' || c == ' ') break;
        if (c == '.') { p++; break; }       /* advance past '.' then fill ext */
        if (c == '*') {                      /* wildcard: rest of name = '?' */
            uint8_t j;
            for (j = i; j < 8; j++) pf->fcb.f[j] = '?';
            p++;
            while (*p && *p != '.' && *p != ' ') p++;
            if (*p == '.') p++;
            break;
        }
        if (!valid_fcb_char(c)) return false;
        pf->fcb.f[i] = c;
        p++;
    }

    /* If we stopped at '.' that was NOT the star-wildcard branch,
     * p already points past the '.'.  If we stopped at NUL/' ', skip ext. */
    /* Find where the extension starts: p is already correct from loop above,
     * but we may still be in the name field (ran all 8 chars without hitting '.').
     * Scan forward to find a '.' we haven't yet passed. */
    if (i == 8) {
        /* name field was exactly 8 chars; scan past any overflow and find '.' */
        while (*p && *p != '.' && *p != ' ') p++;
        if (*p == '.') p++;   /* skip '.' */
    }

    ext = p;    /* extension starts here (may be NUL if no extension) */

    /* --- Pass 2: type/extension field (up to 3 chars) --- */
    for (i = 0; i < 3; i++) {
        c = ext[i];
        if (c == '\0' || c == ' ') break;
        if (c == '.') return false;  /* second dot = invalid */
        if (c == '*') {
            uint8_t j;
            for (j = i; j < 3; j++) pf->fcb.t[j] = '?';
            break;
        }
        if (!valid_fcb_char(c)) return false;
        pf->fcb.t[i] = c;
    }

    return true;
}

/*
 * Parse optional DU: prefix from argument string.
 * Fills pf->user and pf->fcb.dr from the prefix.
 * Returns pointer to the filename portion (after the ':').
 * Returns NULL on malformed DU: spec.
 * If no ':' found, uses current drive+user and returns original pointer.
 */
const char *parse_du(const char *arg, pfile_t *pf) {
    uint8_t    drive = g_drive;   /* default: current drive (0=A) */
    uint8_t    user  = g_user;    /* default: current user */
    const char *colon;
    const char *p;

    /* Find first colon */
    colon = arg;
    while (*colon && *colon != ':') colon++;

    if (!*colon) {
        /* No colon: use defaults */
        pf->user   = user;
        pf->fcb.dr = (uint8_t)(drive + 1);  /* FCB: 0=current, 1=A, ... */
        return arg;
    }

    if (colon == arg) return NULL;  /* ':' is first char = error */

    p = arg;

    /* Optional drive letter (A-P or a-p) */
    if ((*p >= 'A' && *p <= MAX_DRIVE) || (*p >= 'a' && *p <= (MAX_DRIVE - 'A' + 'a'))) {
        if (*p >= 'a') drive = (uint8_t)(*p - 'a');
        else           drive = (uint8_t)(*p - 'A');
        p++;
    }

    /* Optional user number (1 or 2 digits, 0-31) */
    if (*p >= '0' && *p <= '9') {
        user = (uint8_t)(*p - '0');
        p++;
        if (*p >= '0' && *p <= '9') {
            user = (uint8_t)(user * 10 + (*p - '0'));
            p++;
        }
    }

    /* Must hit ':' here */
    if (*p != ':') return NULL;
    p++;  /* skip ':' */

    /* Validate limits */
    if (drive > (uint8_t)(MAX_DRIVE - 'A')) return NULL;
    if (user  > MAX_USER)                   return NULL;

    pf->user   = user;
    pf->fcb.dr = (uint8_t)(drive + 1);  /* FCB encoding: 1=A, 2=B, ... */
    return p;
}

/*
 * Print file name in PPIP format: D##:FILENAME.EXT
 * drive and user come from the pfile_t
 */
void print_fname(const pfile_t *pf) {
    uint8_t i;
    uint8_t drive = (uint8_t)(pf->fcb.dr > 0 ? pf->fcb.dr - 1 : g_drive);
    uint8_t user  = pf->user;

    /* Drive letter */
    con_out((char)('A' + drive));

    /* User number (1 or 2 digits) */
    if (user >= 10) {
        con_out((char)('0' + user / 10));
        con_out((char)('0' + user % 10));
    } else {
        con_out((char)('0' + user));
    }
    con_out(':');

    /* Filename (8 chars, trim trailing spaces) */
    for (i = 0; i < 8; i++) {
        char c = (char)(pf->fcb.f[i] & 0x7F);  /* mask attribute bit */
        if (c == ' ') break;
        con_out(c);
    }
    con_out('.');

    /* Extension (3 chars, trim trailing spaces) */
    for (i = 0; i < 3; i++) {
        char c = (char)(pf->fcb.t[i] & 0x7F);  /* mask attribute bit */
        if (c == ' ') break;
        con_out(c);
    }
}

/*
 * Compare two FCB names for equality.
 * Masks bit 7 (attribute bits) before comparing, as in the original STRNCMP.
 * Also compares user and drive so we can detect "copy to self".
 */
bool fname_equal(const pfile_t *a, const pfile_t *b) {
    uint8_t i;
    if (a->user != b->user) return false;
    if (a->fcb.dr != b->fcb.dr) return false;
    for (i = 0; i < 8; i++)
        if ((a->fcb.f[i] & 0x7F) != (b->fcb.f[i] & 0x7F)) return false;
    for (i = 0; i < 3; i++)
        if ((a->fcb.t[i] & 0x7F) != (b->fcb.t[i] & 0x7F)) return false;
    return true;
}

/* Copy just the 11-byte name+type from src to dst (preserving dr and user) */
void copy_fname(pfile_t *dst, const pfile_t *src) {
    uint8_t i;
    for (i = 0; i < 8; i++) dst->fcb.f[i] = src->fcb.f[i];
    for (i = 0; i < 3; i++) dst->fcb.t[i] = src->fcb.t[i];
}

/* Returns true if the FCB name/type contains any wildcard '?' */
bool has_wild(const pfile_t *pf) {
    uint8_t i;
    for (i = 0; i < 8; i++)
        if (pf->fcb.f[i] == '?') return true;
    for (i = 0; i < 3; i++)
        if (pf->fcb.t[i] == '?') return true;
    return false;
}

/*
 * Expand source wildcard FCB via BDOS SRCHFST/SRCHNXT.
 * Each match is stored as FCB_FNAME_LEN (11) bytes in g_nargbuf.
 * Attribute bits from the directory entry are preserved so that R/O
 * status is available when building the destination FCB.
 * Sets g_nargc. Returns the match count.
 */
uint16_t expand_wild(pfile_t *pf) {
    uint8_t  offset;
    uint8_t *entry;
    uint8_t *narg;
    uint16_t count = 0;
    uint8_t  i;

    zero_fcb_ctrl(pf);
    set_dma((void*)0x0080);
    f_setusr(pf);
    offset = (uint8_t)bdos(BDOS_SRCHFST, (int)&pf->fcb);

    while (offset != 0xFF && count < MAX_NARG) {
        /* Directory entry: DMA + offset*32; skip byte 0 (user/drive) */
        entry = (uint8_t*)0x0080 + ((uint16_t)offset * 32) + 1;
        narg  = &g_nargbuf[count * FCB_FNAME_LEN];
        for (i = 0; i < FCB_FNAME_LEN; i++)
            narg[i] = entry[i];
        count++;
        set_dma((void*)0x0080);
        offset = (uint8_t)bdos(BDOS_SRCHNXT, 0);
    }

    g_nargc = count;
    return count;
}

/*
 * Resolve destination template against an actual source filename.
 * For each name/type position: if template char is '?', use source char;
 * otherwise keep template char. Drive and user come from dst_tmpl.
 */
void match_wild(pfile_t *dst, const pfile_t *dst_tmpl, const pfile_t *src) {
    uint8_t i;
    for (i = 0; i < 8; i++)
        dst->fcb.f[i] = (dst_tmpl->fcb.f[i] == '?')
                        ? (uint8_t)(src->fcb.f[i] & 0x7F)
                        : dst_tmpl->fcb.f[i];
    for (i = 0; i < 3; i++)
        dst->fcb.t[i] = (dst_tmpl->fcb.t[i] == '?')
                        ? (uint8_t)(src->fcb.t[i] & 0x7F)
                        : dst_tmpl->fcb.t[i];
    dst->fcb.dr = dst_tmpl->fcb.dr;
    dst->user   = dst_tmpl->user;
    zero_fcb_ctrl(dst);
}

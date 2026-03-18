#include "ppip.h"
#include "console.h"
#include "iaio.h"

void con_out(char c) {
    bdos(BDOS_CONOUT, (int)(uint8_t)c);
}

void con_str(const char *s) {
    while (*s) con_out(*s++);
}

void con_nl(void) {
    con_out('\r');
    con_out('\n');
}

/* Wait for a keypress and return the character (BDOS 1 — auto-echoes) */
char con_in(void) {
    return (char)(uint8_t)bdos(BDOS_CONIN, 0);
}

/* Wait for a keypress, no echo.
 * BDOS 6 with 0xFF = direct console input: returns 0 if no char ready,
 * otherwise returns the char without echoing it. Poll until char arrives. */
char con_in_ne(void) {
    char c;
    do { c = (char)(uint8_t)bdos(BDOS_DIRIO, 0xFF); } while (!c);
    return c;
}

/* Non-blocking: return true if a key is waiting */
bool con_stat(void) {
    return bdos(BDOS_CONSTAT, 0) != 0;
}

/* Convert uppercase - hex nibble */
static void hex_nibble(uint8_t n) {
    n &= 0x0F;
    con_out(n < 10 ? '0' + n : 'A' + n - 10);
}

static void hex_byte(uint8_t b) {
    hex_nibble(b >> 4);
    hex_nibble(b);
}

void con_hex16(uint16_t val) {
    hex_byte((uint8_t)(val >> 8));
    hex_byte((uint8_t)val);
}

/* Ask "Delete? " and return true if user answers Y/y.
 * Ctrl-C triggers a CP/M warm boot (jump to 0x0000) — same as standard
 * CP/M break handling.  The destination file is never open at this point
 * so there is nothing to clean up before rebooting. */
bool ask_delete(void) {
    char c;
    while ((uint8_t)bdos(BDOS_DIRIO, 0xFF)) { }  /* drain type-ahead; BDOS 6 returns 0 if no char */
    con_str(" Delete? ");
    c = con_in();           /* BDOS 1 echoes the char automatically */
    if (c == 0x03) {
        con_nl();
        ((void(*)(void))0x0000)();  /* CP/M warm boot */
    }
    if (c != 'Y' && c != 'y') con_nl();  /* end line on No; Yes continues inline */
    return (c == 'Y' || c == 'y');
}

void print_help(void) {
    con_nl();
    con_str(PROG_NAME " v" PPIP_VER_STR " - CP/M File Copy Utility (Intangybles)");
    con_nl();
    con_nl();
    con_str("USAGE:");
    con_nl();
    con_str("  " PROG_NAME " [DU:]source[.ext] [[DU:][dest[.ext]]] [/options]");
    con_nl();
    con_str("    or");
    con_nl();
    con_str("  " PROG_NAME " [[DU:]dest[.ext]=][DU:]source[.ext] [/options]");
    con_nl();
    con_nl();
    con_str("DU: prefix: D=drive A-P, U=user 0-31  e.g. B3:FILE.TXT");
    con_nl();
#ifdef NABU_IA
    con_str("IA: prefix: NABU RetroNET file store  e.g. IA:FILE.TXT");
#ifndef NABU_DEFAULT
    con_str(ia_is_nabu() ? "  [NABU Detected]" : "  [use /N to enable]");
#endif
    con_nl();
#endif
    con_nl();
    con_str("Options (default OFF, toggle with /x):");
    con_nl();
    con_str("  /V  CRC verify after copy");
    con_nl();
    con_str("  /C  Print CRC value");
    con_nl();
    con_str("  /E  Delete existing R/W dest without asking");
    con_nl();
    con_str("  /W  Delete existing R/W and R/O dest without asking");
    con_nl();
    con_str("  /M  Move: copy then delete source (forces /V)");
    con_nl();
#if defined(NABU_IA) && !defined(NABU_DEFAULT)
    con_str("  /N  Enable IA: interface on non-CloudCP/M NABU systems");
    con_nl();
#endif
    con_str("  /H  This help");
    con_nl();
    con_nl();
    con_str("Source can be CON: to type text directly to a file.");
    con_nl();
}

#ifndef PPIP_H
#define PPIP_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <cpm.h>    /* provides bdos() with correct z88dk calling convention */

/* ---- BDOS function numbers ---- */
#define BDOS_CONIN      1
#define BDOS_CONOUT     2
/* Note: BDOS 7 = Get I/O byte on CP/M 2.2 — NOT console-no-echo.
 * Use BDOS_DIRIO (6) with 0xFF for no-echo blocking input (see con_in_ne). */
#define BDOS_DIRIO      6
#define BDOS_CONSTAT    11
#define BDOS_RSTDSK     13
#define BDOS_SELDSK     14
#define BDOS_FOPEN      15
#define BDOS_FCLOSE     16
#define BDOS_SRCHFST    17
#define BDOS_SRCHNXT    18
#define BDOS_FDELETE    19
#define BDOS_FREAD      20
#define BDOS_FWRITE     21
#define BDOS_FCREATE    22
#define BDOS_GETDSK     25
#define BDOS_SETDMA     26
#define BDOS_SETATT     30
#define BDOS_SETUSR     32
#define BDOS_ZRDVER     48

/* ---- Limits and sizes ---- */
#define REC_SIZE        128     /* CP/M record (DMA block) size in bytes */
#define FCB_NAME_LEN    8       /* filename field in FCB */
#define FCB_TYPE_LEN    3       /* type/ext field in FCB */
#define FCB_FNAME_LEN   11      /* name + type (8+3) */
#define IOBUF_RECORDS   128     /* I/O buffer depth: 128 * 128 = 16384 bytes */
#define IOBUF_SIZE      (IOBUF_RECORDS * REC_SIZE)
#define MAX_ARG         8       /* max command arguments */
#define MAX_ARG_LEN     32      /* max chars per CP/M argument (DU: + 8.3 name) */
#define MAX_NARG        512     /* max wildcard expansion results */
#define MAX_DRIVE       ('P')   /* highest accessible drive letter */
#define MAX_USER        31      /* highest accessible user area */
#define SWITCH_CHAR     '/'     /* option prefix character */
#define CRC_RETRIES     3       /* retries on CRC verify failure */
#define PPIP_VERSION    "1.0.27"

/* ---- IA: RetroNET file descriptor (NABU_IA builds only) ---- */
#define IA_NAME_LEN     64      /* max IA path+filename length — supports multi-level paths */

typedef struct {
    char    name[IA_NAME_LEN];  /* filename on IA store (uppercase, NUL-terminated) */
    uint8_t namelen;            /* strlen(name) cached for rn_ calls */
    uint8_t handle;             /* open handle; 0xFF = not open */
} ia_t;

/* ---- CP/M FCB (36 bytes) ---- */
typedef struct {
    uint8_t dr;             /* drive: 0=current, 1=A, 2=B ... 16=P */
    uint8_t f[8];           /* filename, 8 chars, space-padded */
    uint8_t t[3];           /* type/ext, 3 chars; t[0] bit7=R/O, t[2] bit7=archive */
    uint8_t ex;             /* extent (low) */
    uint8_t s1, s2;         /* system reserved */
    uint8_t rc;             /* record count */
    uint8_t al[16];         /* allocation bitmap */
    uint8_t cr;             /* current sequential record */
    uint8_t r0, r1, r2;     /* random record number */
} ppip_fcb_t;               /* 36 bytes */

/* ---- PPIP file descriptor ---- */
/* user byte lives at FCB[-1] as in original assembly */
typedef struct {
    uint8_t     user;   /* CP/M user area for this file */
    ppip_fcb_t  fcb;    /* standard 36-byte FCB */
} pfile_t;

/* ---- Program options ---- */
typedef struct {
    bool verify;    /* /V - CRC-16 verify after copy */
    bool report;    /* /C - print CRC value */
    bool emend;     /* /E - delete existing R/W dest without asking */
    bool wipe;      /* /W - delete existing R/W and R/O dest without asking */
    bool movf;      /* /M - move: copy then delete source (forces verify) */
    bool nabu_ia;   /* /N - enable IA: on non-CloudCP/M NABU systems */
} options_t;

/* ---- Globals (defined in ppip.c) ---- */
extern uint8_t   g_drive;           /* current drive at startup (0=A) */
extern uint8_t   g_user;            /* current user at startup */
extern options_t g_opts;            /* active option flags */
extern uint8_t   g_iobuf[];         /* disk I/O buffer */
extern uint16_t  g_iobuf_recs;      /* buffer size in records */
extern uint16_t  g_crcval;          /* running CRC value */
extern bool      g_ferror;          /* fatal file error flag */
extern bool      g_want_help;       /* /H or no-args: show help then exit */

/* IA: RetroNET file descriptors (NABU_IA builds) */
extern ia_t      g_ia_src;          /* parsed IA: source descriptor */
extern ia_t      g_ia_dst;          /* parsed IA: dest descriptor */
extern bool      g_src_is_ia;       /* true when argv[0] is IA: */
extern bool      g_dst_is_ia;       /* true when argv[1] is IA: */

/* wildcard expansion: each entry is FCB_FNAME_LEN bytes */
extern uint8_t   g_nargbuf[];       /* expanded file name list (Phase 2) */
extern uint16_t  g_nargc;           /* count of expanded names */

/* command-line arguments */
extern char      g_argbuf[][MAX_ARG_LEN];
extern char     *g_argv[];
extern int       g_argc;

#endif /* PPIP_H */

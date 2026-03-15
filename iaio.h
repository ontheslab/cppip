#ifndef IAIO_H
#define IAIO_H

#ifdef NABU_IA

#include "ppip.h"

/* All IA Functions */
/* Return true if running under NABU CloudCP/M (safe to call rn_ functions).
 * Returns false on any other CP/M system — IA: operations must not proceed. */
bool     ia_is_nabu(void);

/* Initialise an ia_t from a plain filename string (no "IA:" prefix).
 * Copies up to IA_NAME_LEN-1 chars, sets namelen, marks handle closed. */
void     ia_init(ia_t *ia, const char *name);

/* Print "IA:filename" to console */
void     ia_print_name(const ia_t *ia);

/* Return true if the file exists on the IA store */
bool     ia_exists(const ia_t *ia);

/* Open for read (fails with error if file not found) */
bool     ia_open_rd(ia_t *ia);

/* Open for write (handles Exists!/Delete? prompt; truncates if overwriting) */
bool     ia_open_wr(ia_t *ia);

/* Close an open IA handle */
void     ia_close(ia_t *ia);

/* Delete the file from the IA store */
void     ia_delete(ia_t *ia);

/* Copy CPM file -> IA (src already name-resolved; dst is open target) */
bool     ia_copy_cpm_to_ia(pfile_t *src, ia_t *dst);

/* Copy IA -> CPM file (src is IA; dst pfile_t must have dr/user/name set) */
bool     ia_copy_ia_to_cpm(ia_t *src, pfile_t *dst);

/* Read IA file and compute CRC into g_crcval (for /V verify after CPM->IA) */
bool     ia_crc_file(ia_t *ia);

/* Return true if ia->name contains a wildcard character (* or ?) */
bool     ia_has_wild(const ia_t *ia);

/* Return count of IA files matching pattern->name; call ia_list_item() to retrieve */
uint16_t ia_list_wild(const ia_t *pattern);

/* Populate out with the nth result from the last ia_list_wild() call (0-based) */
void     ia_list_item(uint16_t n, ia_t *out);

#endif /* NABU_IA */

#endif /* IAIO_H */

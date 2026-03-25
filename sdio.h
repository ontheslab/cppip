#ifndef SDIO_H
#define SDIO_H

#ifdef FREHD

#include "ppip.h"

/* Return true if a FreHD interface is present on the I/O bus.
 * Sends GETVER; SIZE2 == 6 means FreHD is present.
 * STATUS == 0xFF means no device at all. */
bool     sd_is_frehd(void);

/* Initialise an sd_t from a plain filename string (no "SD:" prefix).
 * Copies up to SD_NAME_LEN-1 chars, uppercases, sets namelen. */
void     sd_init(sd_t *sd, const char *name);

/* Print "SD:filename" to console */
void     sd_print_name(const sd_t *sd);

/* Return true if the file exists on the SD card */
bool     sd_exists(const sd_t *sd);

/* Open for read (prints error if not found) */
bool     sd_open_rd(const sd_t *sd);

/* Open for write (handles Exists!/Delete? prompt; creates or truncates) */
bool     sd_open_wr(const sd_t *sd);

/* Close the currently open SD file */
void     sd_close(void);

/* Copy SD file -> CPM (src is SD descriptor; dst pfile_t must be set up) */
bool     sd_copy_sd_to_cpm(const sd_t *src, pfile_t *dst);

/* Copy CPM file -> SD (src pfile_t already opened; dst is SD target) */
bool     sd_copy_cpm_to_sd(pfile_t *src, const sd_t *dst);

/* Read SD file and compute CRC into g_crcval (for /V verify after CPM->SD) */
bool     sd_crc_file(const sd_t *sd);

/* Return true if sd->name contains a wildcard character (* or ?) */
bool     sd_has_wild(const sd_t *sd);

/* Enumerate SD files matching pattern; results stored in g_nargbuf.
 * Returns count of matches found (0 = none). */
uint16_t sd_list_wild(const sd_t *pattern);

/* Populate out with the nth result from the last sd_list_wild() call (0-based).
 * base provides the path prefix from the original pattern (e.g. g_sd_src). */
void     sd_list_item(uint16_t n, const sd_t *base, sd_t *out);

#endif /* FREHD */

#endif /* SDIO_H */

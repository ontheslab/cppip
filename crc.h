#ifndef CRC_H
#define CRC_H
#include "ppip.h"

void crc_init(void);
void crc_update(uint8_t byte);
void crc_record(const uint8_t *rec);  /* update for one 128-byte record */
bool crc_file(pfile_t *pf);           /* compute CRC of entire file */

/* Global CRC state (in crc.c) */
extern uint16_t g_crcval;
extern uint16_t g_crcval2;  /* saved source CRC for verify comparison */

#endif

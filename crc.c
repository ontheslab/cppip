/*
 * CRC-16 module
 *
 * Algorithm: CCITT CRC-16, polynomial x^16 + x^12 + x^5 + x^0
 * Same as used by MODEM7, CHEK, and the original PPIP.
 * Table is built at startup by crc_init().
 */
#include "ppip.h"
#include "crc.h"
#include "diskio.h"

uint16_t g_crcval  = 0;
uint16_t g_crcval2 = 0;

/* 512-byte CRC lookup table (256 entries x 2 bytes) */
static uint16_t crc_table[256];

void crc_init(void) {
    uint16_t i;
    for (i = 0; i < 256; i++) {
        uint16_t crc = 0;
        uint8_t  b   = (uint8_t)i;
        uint8_t  bit;
        crc ^= ((uint16_t)b << 8);
        for (bit = 0; bit < 8; bit++) {
            if (crc & 0x8000)
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            else
                crc <<= 1;
        }
        crc_table[i] = crc;
    }
}

void crc_update(uint8_t byte) {
    g_crcval = (uint16_t)(crc_table[(g_crcval >> 8) ^ byte] ^ (g_crcval << 8));
}

/*
 * Update CRC for one 128-byte record.
 * uint8_t i counts 0..127 safely (128 < 256, no overflow).
 */
void crc_record(const uint8_t *rec) {
    uint8_t i;
    for (i = 0; i < REC_SIZE; i++) crc_update(rec[i]);
}

/*
 * Compute CRC of an entire file.
 * Opens the file, reads all records, closes it.
 * Result in g_crcval.
 * Returns false on open error.
 */
bool crc_file(pfile_t *pf) {
    bool     eof;
    uint16_t recs;

    g_crcval = 0;

    if (!f_open(pf)) {
        return false;
    }

    eof = false;
    while (!eof) {
        recs = blk_read(pf, g_iobuf, g_iobuf_recs, &eof);
        (void)recs;  /* CRC updated inside blk_read when verify=true */
    }

    f_close(pf);
    return true;
}

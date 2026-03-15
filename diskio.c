#include "ppip.h"
#include "diskio.h"
#include "filename.h"
#include "console.h"
#include "crc.h"

/* Scratch DMA buffer address (CP/M default DMA at 0x0080) */
#define DEFAULT_DMA ((void*)0x0080)

/* --- Drive / User management --- */

void get_du(uint8_t *drive, uint8_t *user) {
    *drive = (uint8_t)bdos(BDOS_GETDSK, 0);
    *user  = (uint8_t)bdos(BDOS_SETUSR, 0xFF);
}

void set_du(uint8_t drive, uint8_t user) {
    bdos(BDOS_SELDSK, (int)drive);
    bdos(BDOS_SETUSR, (int)user);
}

/* Set user area from FCB's embedded user byte */
void f_setusr(const pfile_t *pf) {
    bdos(BDOS_SETUSR, (int)pf->user);
}

/* Set the DMA (Direct Memory Access) address for the next I/O operation */
void set_dma(void *buf) {
    bdos(BDOS_SETDMA, (int)buf);
}

/* --- File open / close / create / delete --- */

bool f_open(pfile_t *pf) {
    uint8_t result;
    zero_fcb_ctrl(pf);
    f_setusr(pf);
    result = (uint8_t)bdos(BDOS_FOPEN, (int)&pf->fcb);
    return (result != 0xFF);
}

bool f_close(pfile_t *pf) {
    uint8_t result;
    f_setusr(pf);
    result = (uint8_t)bdos(BDOS_FCLOSE, (int)&pf->fcb);
    return (result != 0xFF);
}

bool f_create(pfile_t *pf) {
    uint8_t result;
    zero_fcb_ctrl(pf);
    f_setusr(pf);
    result = (uint8_t)bdos(BDOS_FCREATE, (int)&pf->fcb);
    return (result != 0xFF);
}

void f_delete(pfile_t *pf) {
    f_setusr(pf);
    bdos(BDOS_FDELETE, (int)&pf->fcb);
}

/* --- Single record read / write --- */

/* Read one 128-byte record into the current DMA buffer.
   Returns true on success, false on EOF or error. */
bool f_read(pfile_t *pf) {
    return (uint8_t)bdos(BDOS_FREAD, (int)&pf->fcb) == 0;
}

/* Write one 128-byte record from the current DMA buffer.
   Returns true on success, false on error (disk full). */
bool f_write(pfile_t *pf) {
    return (uint8_t)bdos(BDOS_FWRITE, (int)&pf->fcb) == 0;
}

/* --- Directory search --- */

/*
 * Check if a file exists.
 * Returns the directory offset (0-3) if found, 0xFF if not found.
 * Uses the default DMA buffer (0x0080) as scratch space.
 */
uint8_t f_exist(pfile_t *pf) {
    zero_fcb_ctrl(pf);
    set_dma(DEFAULT_DMA);
    f_setusr(pf);
    return (uint8_t)bdos(BDOS_SRCHFST, (int)&pf->fcb);
}

/*
 * Copy the directory entry name found by f_exist() into our FCB.
 * offset is the value returned by SRCHFST (0-3).
 * The directory entry is in the DMA buffer at offset * 32.
 * Byte 0 of the entry is the drive/user; bytes 1-11 are name+type.
 */
void copy_dir_entry(pfile_t *pf, uint8_t offset) {
    uint8_t *entry = (uint8_t*)DEFAULT_DMA + ((uint16_t)offset << 5);  /* offset * 32 */
    uint8_t  i;
    entry++;  /* skip the drive/user byte at entry[0] */
    for (i = 0; i < 8; i++) pf->fcb.f[i] = entry[i];
    for (i = 0; i < 3; i++) pf->fcb.t[i] = entry[8 + i];
}

/* Set file attributes from the FCB's attribute bytes */
void f_attrib(pfile_t *pf) {
    f_setusr(pf);
    bdos(BDOS_SETATT, (int)&pf->fcb);
}

/* --- New file creation with overwrite handling --- */

/*
 * Create a new destination file.
 * Handles the case where the file already exists:
 *   - R/O files: require /W or user confirmation
 *   - R/W files: require /E, /W, or user confirmation
 * Returns true if the file was created successfully.
 */
bool new_file(pfile_t *pf) {
    uint8_t offset;
    bool    is_ro;

    /* Zero control fields before directory search */
    zero_fcb_ctrl(pf);

    /* Check if file already exists */
    offset = f_exist(pf);

    if (offset != 0xFF) {
        /* File exists: copy its directory name (with attribute bits) */
        copy_dir_entry(pf, offset);

        is_ro = (pf->fcb.t[0] & 0x80) != 0;

        if (is_ro) {
            if (!g_opts.wipe) {
                con_str(" R/O!");
                if (!ask_delete()) return false;
            }
            /* Clear R/O bit so we can delete/overwrite */
            pf->fcb.t[0] &= 0x7F;
            f_attrib(pf);
        } else {
            if (!g_opts.emend && !g_opts.wipe) {
                con_str(" Exists!");
                if (!ask_delete()) return false;
            }
        }

        f_delete(pf);
    }

    /* Zero control fields again after delete, before create */
    zero_fcb_ctrl(pf);

    if (!f_create(pf)) {
        con_str("\r\nERROR: directory is full\r\n");
        g_ferror = true;
        return false;
    }

    return true;
}

/* --- Block read / write loops --- */

/*
 * Read up to 'recs' records from file into buf.
 * Returns the number of records actually read.
 * Sets *eof = true when the end of file is reached.
 * Updates CRC if g_opts.verify is set (Phase 3).
 */
uint16_t blk_read(pfile_t *pf, void *buf, uint16_t recs, bool *eof) {
    uint8_t  *p = (uint8_t*)buf;
    uint16_t  count = 0;

    f_setusr(pf);
    *eof = false;

    while (recs-- > 0) {
        set_dma(p);
        if (!f_read(pf)) {
            /* EOF or error: stop reading */
            *eof = true;
            break;
        }
        if (g_opts.verify) crc_record(p);  /* Phase 3: update running CRC */
        p += REC_SIZE;
        count++;
    }

    return count;
}

/*
 * Write 'recs' records from buf to file.
 * Returns true on success, false on disk-full error.
 */
bool blk_write(pfile_t *pf, void *buf, uint16_t recs) {
    uint8_t  *p = (uint8_t*)buf;

    if (recs == 0) return true;

    f_setusr(pf);

    while (recs-- > 0) {
        set_dma(p);
        if (!f_write(pf)) return false;  /* disk full */
        p += REC_SIZE;
    }

    return true;
}

/* --- File copy --- */

/*
 * Copy file src to dst.
 * dst must already have the correct drive, user, and FCB name set.
 * new_file() is called here to handle existing dest files.
 * Returns true on success.
 */
bool f_copy(pfile_t *src, pfile_t *dst) {
    bool     eof;
    uint16_t recs;

    g_crcval = 0;

    /* Open source */
    if (!f_open(src)) {
        con_str("\r\nERROR: can't open source\r\n");
        g_ferror = true;
        return false;
    }

    /* Create destination (handles exists / R/O / prompts) */
    if (!new_file(dst)) {
        f_close(src);   /* make sure source is closed */
        return false;
    }

    /* Read/write loop */
    eof = false;
    while (!eof) {
        recs = blk_read(src, g_iobuf, g_iobuf_recs, &eof);
        if (!blk_write(dst, g_iobuf, recs)) {
            f_delete(dst);
            /* Restore DMA to a safe buffer before error message */
            set_dma(DEFAULT_DMA);
            con_str("\r\nERROR: Disk full. Copy deleted.\r\n");
            g_ferror = true;
            f_close(src);
            return false;
        }
    }

    /* Save source CRC before closing (verify comparison happens in copy_one) */
    g_crcval2 = g_crcval;

    /* Close destination */
    if (!f_close(dst)) {
        con_str("\r\nERROR: can't close destination\r\n");
        g_ferror = true;
        return false;
    }

    set_dma(DEFAULT_DMA);  /* restore DMA to safe location */
    return true;
}

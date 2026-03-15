#ifndef DISKIO_H
#define DISKIO_H
#include "ppip.h"

void     get_du(uint8_t *drive, uint8_t *user);
void     set_du(uint8_t drive, uint8_t user);
void     f_setusr(const pfile_t *pf);
void     set_dma(void *buf);
bool     f_open(pfile_t *pf);
bool     f_close(pfile_t *pf);
bool     f_create(pfile_t *pf);
void     f_delete(pfile_t *pf);
bool     f_read(pfile_t *pf);
bool     f_write(pfile_t *pf);
uint8_t  f_exist(pfile_t *pf);
void     copy_dir_entry(pfile_t *pf, uint8_t offset);
void     f_attrib(pfile_t *pf);
bool     new_file(pfile_t *pf);
uint16_t blk_read(pfile_t *pf, void *buf, uint16_t recs, bool *eof);
bool     blk_write(pfile_t *pf, void *buf, uint16_t recs);
bool     f_copy(pfile_t *src, pfile_t *dst);

#endif

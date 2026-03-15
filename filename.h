#ifndef FILENAME_H
#define FILENAME_H
#include "ppip.h"

bool        valid_fcb_char(char c);
void        clear_fcb(pfile_t *pf);
void        zero_fcb_ctrl(pfile_t *pf);
bool        make_fcb(const char *name, pfile_t *pf);
const char *parse_du(const char *arg, pfile_t *pf);
void        print_fname(const pfile_t *pf);
bool        fname_equal(const pfile_t *a, const pfile_t *b);
void        copy_fname(pfile_t *dst, const pfile_t *src);
bool        has_wild(const pfile_t *pf);
uint16_t    expand_wild(pfile_t *pf);
void        match_wild(pfile_t *dst, const pfile_t *dst_tmpl, const pfile_t *src);

#endif

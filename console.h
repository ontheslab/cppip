#ifndef CONSOLE_H
#define CONSOLE_H
#include "ppip.h"

void     con_out(char c);
void     con_str(const char *s);
void     con_nl(void);
char     con_in(void);
char     con_in_ne(void);  /* console in, no echo (BDOS 7) */
bool     con_stat(void);
void     con_hex16(uint16_t val);
bool     ask_delete(void);
void     print_help(void);

#endif

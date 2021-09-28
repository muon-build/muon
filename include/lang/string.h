#ifndef MUON_LANG_STRING_H
#define MUON_LANG_STRING_H

#include <stdint.h>

struct workspace;

typedef uint32_t str;

uint32_t wk_str_pushf(struct workspace *wk, const char *fmt, ...)  __attribute__ ((format(printf, 2, 3)));
char *wk_str(struct workspace *wk, uint32_t id);
void wk_str_appf(struct workspace *wk, uint32_t *id, const char *fmt, ...)  __attribute__ ((format(printf, 3, 4)));
void wk_str_app(struct workspace *wk, uint32_t *id, const char *str);
void wk_str_appn(struct workspace *wk, uint32_t *id, const char *str, uint32_t n);
uint32_t wk_str_push(struct workspace *wk, const char *str);
uint32_t wk_str_pushn(struct workspace *wk, const char *str, uint32_t n);
char *wk_objstr(struct workspace *wk, uint32_t id);
char *wk_file_path(struct workspace *wk, uint32_t id);
uint32_t wk_str_push_stripped(struct workspace *wk, const char *s);
uint32_t wk_str_split(struct workspace *wk, const char *s, const char *sep);

#endif

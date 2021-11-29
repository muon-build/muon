#ifndef MUON_LANG_STRING_H
#define MUON_LANG_STRING_H

#include "lang/object.h"

struct workspace;

#define WKSTR(cstring) (struct str){ .s = cstring, .len = strlen(cstring) }

bool buf_push(char *buf, uint32_t len, uint32_t *i, char s);
bool buf_pushs(char *buf, uint32_t len, uint32_t *i, char *s);
bool buf_pushn(char *buf, uint32_t len, uint32_t *i, char *s, uint32_t n);

bool str_unescape(char *buf, uint32_t len, const struct str *ss, uint32_t *r);

bool str_has_null(const struct str *ss);

const struct str *get_str(struct workspace *wk, obj s);
const char *get_cstr(struct workspace *wk, obj s);
obj make_str(struct workspace *wk, const char *str);
obj make_strn(struct workspace *wk, const char *str, uint32_t n);
obj make_strf(struct workspace *wk, const char *fmt, ...)  __attribute__ ((format(printf, 2, 3)));

void str_app(struct workspace *wk, obj id, const char *str);
void str_appf(struct workspace *wk, obj id, const char *fmt, ...)  __attribute__ ((format(printf, 3, 4)));
void str_appn(struct workspace *wk, obj id, const char *str, uint32_t n);

obj str_clone(struct workspace *wk_src, struct workspace *wk_dest, obj val);

bool str_eql(const struct str *ss1, const struct str *ss2);
bool str_startswith(const struct str *ss, const struct str *pre);
bool str_endswith(const struct str *ss, const struct str *suf);
obj str_join(struct workspace *wk, obj s1, obj s2);

bool str_to_i(const struct str *ss, int64_t *res);

obj str_split(struct workspace *wk, const struct str *ss, const struct str *split);
#endif

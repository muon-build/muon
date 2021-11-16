#ifndef MUON_LANG_STRING_H
#define MUON_LANG_STRING_H

#include "lang/types.h"

struct workspace;

#define WKSTR(cstring) (struct str){ .s = cstring, .len = strlen(cstring) }

enum str_flags {
	str_flag_big = 1 << 0,
};

struct str {
	const char *s;
	uint32_t len;
	enum str_flags flags;
};

#if __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(struct str) == 16, "");
#endif

bool wk_str_unescape(char *buf, uint32_t len, const struct str *ss, uint32_t *r);

bool wk_str_has_null(const struct str *ss);

const struct str *get_str(struct workspace *wk, str s);
const char *get_cstr(struct workspace *wk, str s);
str wk_str_push(struct workspace *wk, const char *str);
str wk_str_pushn(struct workspace *wk, const char *str, uint32_t n);
str wk_str_pushf(struct workspace *wk, const char *fmt, ...)  __attribute__ ((format(printf, 2, 3)));

void wk_str_appf(struct workspace *wk, str *id, const char *fmt, ...)  __attribute__ ((format(printf, 3, 4)));

void wk_str_app(struct workspace *wk, str *id, const char *str);
void wk_str_appn(struct workspace *wk, str *id, const char *str, uint32_t n);

str str_clone(struct workspace *wk_src, struct workspace *wk_dest, str val);

bool wk_streql(const struct str *ss1, const struct str *ss2);
bool wk_cstreql(const struct str *ss, const char *cstring);
bool wk_str_startswith(const struct str *ss, const struct str *pre);
bool wk_str_endswith(const struct str *ss, const struct str *suf);
str wk_strcat(struct workspace *wk, str s1, str s2);

bool wk_str_to_i(const struct str *ss, int64_t *res);

obj wk_str_split(struct workspace *wk, const struct str *ss, const struct str *split);
#endif

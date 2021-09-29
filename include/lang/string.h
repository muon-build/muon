#ifndef MUON_LANG_STRING_H
#define MUON_LANG_STRING_H

#include <stdint.h>

struct workspace;

enum str_flags {
	str_flag_big = 1 << 0,
};

struct str {
	const char *s;
	uint32_t len;
	enum str_flags flags;
};

_Static_assert(sizeof(struct str) == 16, "");

typedef uint32_t str;

const struct str *get_str(struct workspace *wk, str s);
const char *get_cstr(struct workspace *wk, str s);
str wk_str_push(struct workspace *wk, const char *str);
str wk_str_pushn(struct workspace *wk, const char *str, uint32_t n);
str wk_str_pushf(struct workspace *wk, const char *fmt, ...)  __attribute__ ((format(printf, 2, 3)));
str wk_str_push_stripped(struct workspace *wk, const char *s);

void wk_str_appf(struct workspace *wk, str *id, const char *fmt, ...)  __attribute__ ((format(printf, 3, 4)));

void wk_str_app(struct workspace *wk, str *id, const char *str);
void wk_str_appn(struct workspace *wk, str *id, const char *str, uint32_t n);

//TODO: ?
char *wk_file_path(struct workspace *wk, uint32_t id);

#endif

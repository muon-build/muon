#ifndef MUON_LANG_STRING_H
#define MUON_LANG_STRING_H

#include "lang/object.h"

struct workspace;

#define WKSTR(cstring) (struct str){ .s = cstring, .len = strlen(cstring) }


/* sbuf */

enum sbuf_flags {
	sbuf_flag_overflown        = 1 << 0,
	sbuf_flag_overflow_obj_str = 0 << 1, // the default
	sbuf_flag_overflow_alloc   = 1 << 1,
	sbuf_flag_overflow_error   = 1 << 2,
	sbuf_flag_write            = 1 << 3,
};

#define SBUF_STATIC_LEN 1024

struct sbuf {
	char *buf;
	uint32_t *len, cap, buflen;
	char static_buf[SBUF_STATIC_LEN];
	enum sbuf_flags flags;
	obj s;
	FILE *file;
};

void sbuf_init(struct sbuf *sb, enum sbuf_flags flags);
void sbuf_destroy(struct sbuf *sb);
void sbuf_clear(struct sbuf *sb);
void sbuf_push(struct workspace *wk, struct sbuf *sb, char s);
void sbuf_pushn(struct workspace *wk, struct sbuf *sb, const char *s, uint32_t n);
void sbuf_pushs(struct workspace *wk, struct sbuf *sb, const char *s);
void sbuf_pushf(struct workspace *wk, struct sbuf *sb, const char *fmt, ...) __attribute__ ((format(printf, 3, 4)));

void str_unescape(struct workspace *wk, struct sbuf *sb, const struct str *ss,
	bool escape_whitespace);

bool str_has_null(const struct str *ss);

const char *get_cstr(struct workspace *wk, obj s);
obj make_str(struct workspace *wk, const char *str);
obj make_strn(struct workspace *wk, const char *str, uint32_t n);
obj make_strf(struct workspace *wk, const char *fmt, ...)  __attribute__ ((format(printf, 2, 3)));

void str_app(struct workspace *wk, obj s, const char *str);
void str_appf(struct workspace *wk, obj s, const char *fmt, ...)  __attribute__ ((format(printf, 3, 4)));
void str_appn(struct workspace *wk, obj s, const char *str, uint32_t n);

obj str_clone(struct workspace *wk_src, struct workspace *wk_dest, obj val);

bool str_eql(const struct str *ss1, const struct str *ss2);
bool str_startswith(const struct str *ss, const struct str *pre);
bool str_endswith(const struct str *ss, const struct str *suf);
obj str_join(struct workspace *wk, obj s1, obj s2);

bool str_to_i(const struct str *ss, int64_t *res);

obj str_split(struct workspace *wk, const struct str *ss, const struct str *split);
obj str_strip(struct workspace *wk, const struct str *ss, const struct str *strip);
obj str_split_strip(struct workspace *wk, const struct str *ss, const struct str *split, const struct str *strip);
#endif

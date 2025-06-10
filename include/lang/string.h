/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MUON_LANG_STRING_H
#define MUON_LANG_STRING_H

#include "compat.h"

#include <stdarg.h>

#include "lang/object.h"

struct workspace;

/* STR and STRL are both conveniences for creating temporary string objects.
 * These are useful for passing to str_ functions without needing to make a
 * heap allocation. STR() is compile time constant but it requires a
 * compile-time known string STRL() uses strlen() so it may be used with any
 * string.
 */

#define STR(__str) (struct str){ .s = "" __str "", .len = sizeof(__str) - 1 }
#define STR_static(__str) { .s = "" __str "", .len = sizeof(__str) - 1 }
#define STRL(__str)                              \
	(struct str)                             \
	{                                        \
		.s = __str, .len = (uint32_t)strlen(__str) \
	}

/* tstr - Temporary STRing
 *
 * tstrs should almost always be created using the TSTR macro.  This creates a
 * tstr struct as well as allocating a buffer for it on the stack.
 *
 * The tstr_ family of functions will use this temporary buffer until if
 * overflows.  If it overflows it will become an obj_str unless it has the flag
 * tstr_flag_overflow_alloc (e.g. from TSTR_manual) which will cause it to
 * overflow by allocating directly with malloc.
 *
 * A TSTR must be freed if and only if it has the flag
 * tstr_flag_overflow_alloc, if it overflows without this flag then the
 * workspace manages its memory.
 *
 * Conversion of a TSTR to a string should be done with tstr_into_str which
 * reuses the underlying string object if it has already been created.
 */

enum tstr_flags {
	tstr_flag_overflown = 1 << 0,
	tstr_flag_overflow_obj_str = 0 << 1, // the default
	tstr_flag_overflow_alloc = 1 << 1,
	tstr_flag_overflow_error = 1 << 2,
	tstr_flag_write = 1 << 3,
	tstr_flag_string_exposed = 1 << 4,
};

#define TSTR_CUSTOM(name, static_len, flags)     \
	struct tstr name;                        \
	char tstr_static_buf_##name[static_len]; \
	tstr_init(&name, tstr_static_buf_##name, static_len, (enum tstr_flags)flags);
#define TSTR(name) TSTR_CUSTOM(name, 1024, 0)
#define TSTR_manual(name) TSTR_CUSTOM(name, 1024, tstr_flag_overflow_alloc)
#define TSTR_FILE(__name, __f) struct tstr __name = { .flags = tstr_flag_write, .buf = (void *)__f };
#define TSTR_STR(__s) (struct str) { .s = (__s)->buf, .len = (__s)->len }

struct tstr {
	char *buf;
	uint32_t len, cap;
	enum tstr_flags flags;
	obj s;
};

void tstr_init(struct tstr *sb, char *initial_buffer, uint32_t initial_buffer_cap, enum tstr_flags flags);
void tstr_destroy(struct tstr *sb);
void tstr_clear(struct tstr *sb);
void tstr_grow(struct workspace *wk, struct tstr *sb, uint32_t inc);
void tstr_push(struct workspace *wk, struct tstr *sb, char s);
void tstr_pushn(struct workspace *wk, struct tstr *sb, const char *s, uint32_t n);
void tstr_pushs(struct workspace *wk, struct tstr *sb, const char *s);
void tstr_pushf(struct workspace *wk, struct tstr *sb, const char *fmt, ...) MUON_ATTR_FORMAT(printf, 3, 4);
void tstr_vpushf(struct workspace *wk, struct tstr *sb, const char *fmt, va_list args);
void tstr_push_json_escaped(struct workspace *wk, struct tstr *buf, const char *str, uint32_t len);
void tstr_push_json_escaped_quoted(struct workspace *wk, struct tstr *buf, const struct str *str);
void tstr_trim_trailing_newline(struct tstr *sb);
obj tstr_into_str(struct workspace *wk, struct tstr *sb);

/* str - strings
 *
 * struct str a.k.a. obj_string a.k.a. string objects are the primary
 * representation for strings.  They are counted strings and generally may
 * contain 0 bytes.  They are also guaranteed to include a 0 terminator for
 * convenience.  Typically they are constructed with the `make_str*` functions.
 *
 * String objects have the same lifetime as all objects: the lifetime of the
 * workspace they were created with.
 */

void str_escape(struct workspace *wk, struct tstr *sb, const struct str *ss, bool escape_printable);
void str_escape_json(struct workspace *wk, struct tstr *sb, const struct str *ss);

bool str_has_null(const struct str *ss);

const char *get_cstr(struct workspace *wk, obj s);
obj make_str(struct workspace *wk, const char *str);
obj make_strn(struct workspace *wk, const char *str, uint32_t n);
obj make_strf(struct workspace *wk, const char *fmt, ...) MUON_ATTR_FORMAT(printf, 2, 3);
obj make_strfv(struct workspace *wk, const char *fmt, va_list args);

obj make_str_enum(struct workspace *wk, const char *str, obj values);
obj mark_typeinfo_as_enum(struct workspace *wk, obj ti, obj values);
obj make_strn_enum(struct workspace *wk, const char *str, uint32_t n, obj values);
bool check_str_enum(struct workspace *wk, obj l, enum obj_type l_t, obj r, enum obj_type r_t);
bool str_enum_add_type(struct workspace *wk, uint32_t id, obj *res);
void str_enum_add_type_value(struct workspace *wk, obj type, const char *value);
obj str_enum_get(struct workspace *wk, obj type, const char *name);

void str_app(struct workspace *wk, obj *s, const char *str);
void str_appf(struct workspace *wk, obj *s, const char *fmt, ...) MUON_ATTR_FORMAT(printf, 3, 4);
void str_appn(struct workspace *wk, obj *s, const char *str, uint32_t n);
void str_apps(struct workspace *wk, obj *s, obj s_id);

obj str_clone(struct workspace *wk_src, struct workspace *wk_dest, obj val);
obj str_clone_mutable(struct workspace *wk, obj val);

bool str_eql(const struct str *ss1, const struct str *ss2);
bool str_eql_glob(const struct str *ss1, const struct str *ss2);
bool str_eqli(const struct str *ss1, const struct str *ss2);
bool str_startswith(const struct str *ss, const struct str *pre);
bool str_startswithi(const struct str *ss, const struct str *pre);
bool str_endswith(const struct str *ss, const struct str *suf);
bool str_endswithi(const struct str *ss, const struct str *suf);
bool str_contains(const struct str *str, const struct str *substr);
bool str_containsi(const struct str *str, const struct str *substr);
obj str_join(struct workspace *wk, obj s1, obj s2);

bool str_to_i(const struct str *ss, int64_t *res, bool strip);
bool str_to_i_base(const struct str *ss, int64_t *res, bool strip, uint32_t base);

obj str_split(struct workspace *wk, const struct str *ss, const struct str *split);
obj str_splitlines(struct workspace *wk, const struct str *ss);
enum str_strip_flag {
	str_strip_flag_right_only = 1 << 1,
};
obj str_strip(struct workspace *wk, const struct str *ss, const struct str *strip, enum str_strip_flag flags);
obj str_split_strip(struct workspace *wk, const struct str *ss, const struct str *split, const struct str *strip);
bool str_split_in_two(const struct str *s, struct str *l, struct str *r, char split);
void str_to_lower(struct str *str);

void cstr_copy_(char *dest, const struct str *src, uint32_t dest_len);
#define cstr_copy(__dest, __src) cstr_copy_(__dest, __src, ARRAY_LEN(__dest));

bool is_whitespace(char c);
bool is_whitespace_except_newline(char c);

void snprintf_append_(char *buf, uint32_t buf_len, uint32_t *buf_i, const char *fmt, ...) MUON_ATTR_FORMAT(printf, 4, 5);
#define snprintf_append(__buf, __buf_i, __fmt, ...) snprintf_append_(__buf, sizeof(__buf), __buf_i, __fmt, __VA_ARGS__)

enum shell_type {
	shell_type_posix,
	shell_type_cmd,
};
enum shell_type shell_type_for_host_machine(void);
obj str_shell_split(struct workspace *wk, const struct str *str, enum shell_type shell);

bool str_fuzzy_match(const struct str *input, const struct str *guess, int32_t *dist);
#endif

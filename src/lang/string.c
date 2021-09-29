#include "posix.h"

#include <string.h>

#include "error.h"
#include "lang/object.h"
#include "lang/private.h"
#include "lang/string.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/mem.h"

const struct str *
get_str(struct workspace *wk, str s)
{
	assert(s);
	/* if ((s >> 1) == 0) { */
	/* 	return NULL; */
	/* } */

	if (!((s & wk_id_tag_str) == wk_id_tag_str)) {
		struct obj *obj = bucket_array_get(&wk->objs, s >> 1);
		assert(obj->type == obj_string);
		s = obj->dat.str;
	}

	assert((s & wk_id_tag_str) == wk_id_tag_str);
	return bucket_array_get(&wk->strs, s >> 1);
}

const char *
get_cstr(struct workspace *wk, str s)
{
	const struct str *ss = get_str(wk, s);

	uint32_t i;
	for (i = 0; i < ss->len; ++i) {
		if (!ss->s[i]) {
			assert(false && "cstr can not contain null bytes");
		}
	}

	return ss->s;
}

static struct str *
grow_str(struct workspace *wk, str s, uint32_t grow_by)
{
	assert(s);
	assert(((s & wk_id_tag_str) == wk_id_tag_str));

	uint32_t i = s >> 1;

	struct str *ss = bucket_array_get(&wk->strs, i);
	uint32_t new_len = ss->len + grow_by + 1;

	if (ss->flags & str_flag_big) {
		ss->s = z_realloc((void *)ss->s, new_len);
	} else if (new_len > wk->chrs.bucket_size) {
		ss->flags |= str_flag_big;
		char *np = z_malloc(new_len);
		memcpy(np, ss->s, ss->len);
		ss->s = np;
	} else {
		char *np = bucket_array_pushn(&wk->chrs, ss->s, ss->len, new_len);
		ss->s = np;
	}

	return ss;
}

static struct str *
reserve_str(struct workspace *wk, str *s, uint32_t len)
{
	if (wk->strs.len >= UINT32_MAX >> 1) {
		error_unrecoverable("string overflow");
	}

	*s = ((wk->strs.len) << 1) | wk_id_tag_str;

	enum str_flags f = 0;
	const char *p;

	uint32_t new_len = len + 1;

	if (new_len > wk->chrs.bucket_size) {
		f |= str_flag_big;
		p = z_calloc(new_len, 1);
	} else {
		p = bucket_array_pushn(&wk->chrs, NULL, 0, new_len);
	}

	return bucket_array_push(&wk->strs, &(struct str){
		.s = p,
		.len = len,
		.flags = f,
	});
}

str
_make_str(struct workspace *wk, const char *p, uint32_t len)
{
	str s;

	if (!p) {
		return wk_id_tag_str;
	}

	memcpy((void *)reserve_str(wk, &s, len)->s, p, len);

	return s;
}

uint32_t
wk_str_push_stripped(struct workspace *wk, const char *s)
{
	while (*s && (*s == ' ' || *s == '\n')) {
		++s;
	}

	int32_t len;

	for (len = strlen(s) - 1; len >= 0; --len) {
		if (!(s[len] == ' ' || s[len] == '\n')) {
			break;
		}
	}
	++len;

	return wk_str_pushn(wk, s, len);
}

str
wk_str_pushn(struct workspace *wk, const char *str, uint32_t n)
{
	return _make_str(wk, str, n);
}

str
wk_str_push(struct workspace *wk, const char *str)
{
	return _make_str(wk, str, strlen(str));
}

str
wk_str_pushf(struct workspace *wk, const char *fmt, ...)
{
	uint32_t len;
	va_list args, args_copy;
	va_start(args, fmt);
	va_copy(args_copy, args);

	len = vsnprintf(NULL, 0, fmt, args_copy);

	str s;
	struct str *ss = reserve_str(wk, &s, len);
	obj_vsnprintf(wk, (char *)ss->s, len + 1, fmt, args);

	va_end(args_copy);
	va_end(args);

	return s;
}

void
// TODO: remove *
wk_str_appn(struct workspace *wk, str *s, const char *str, uint32_t n)
{
	struct str *ss = grow_str(wk, *s, n);
	memcpy((char *)&ss->s[ss->len], str, n);
	ss->len += n;
}

void
// TODO: remove *
wk_str_app(struct workspace *wk, str *s, const char *str)
{
	wk_str_appn(wk, s, str, strlen(str));
}

void
// TODO: remove *
wk_str_appf(struct workspace *wk, str *s, const char *fmt, ...)
{
	uint32_t len;
	va_list args, args_copy;
	va_start(args, fmt);
	va_copy(args_copy, args);

	len = vsnprintf(NULL, 0, fmt, args_copy);

	struct str *ss = grow_str(wk, *s, len);

	obj_vsnprintf(wk, (char *)ss->s, len + 1, fmt, args);

	va_end(args_copy);
	va_end(args);
}

uint32_t
make_str(struct workspace *wk, const char *str)
{
	uint32_t id;
	make_obj(wk, &id, obj_string)->dat.str = wk_str_push(wk, str);
	return id;
}

static bool
_wk_streql(struct workspace *wk, const struct str *ss1, const struct str *ss2)
{
	return ss1->len == ss2->len && memcmp(ss1->s, ss2->s, ss1->len) == 0;
}

bool
wk_streql(struct workspace *wk, str s1, str s2)
{
	return _wk_streql(wk, get_str(wk, s1), get_str(wk, s2));
}

bool
wk_cstreql(struct workspace *wk, str s1, const char *cstring)
{
#define WKSTR(cstring) (struct str){ .s = cstring, .len = strlen(cstring) }

	return _wk_streql(wk, get_str(wk, s1), &WKSTR("hello"));
}

#include "posix.h"

#include <string.h>

#include "error.h"
#include "lang/private.h"
#include "lang/string.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/mem.h"

uint32_t
make_str(struct workspace *wk, const char *str)
{
	uint32_t id;
	make_obj(wk, &id, obj_string)->dat.str = wk_str_push(wk, str);
	return id;
}

uint32_t
wk_str_split(struct workspace *wk, const char *s, const char *sep)
{
	uint32_t arr;
	make_obj(wk, &arr, obj_array);

	uint32_t i, len;
	const char *start;
	bool first = false;
	for (i = 0; s[i]; ++i) {
		if (strchr(sep, s[i])) {
			if (first) {
				first = false;

				uint32_t str_id;
				make_obj(wk, &str_id, obj_string)->dat.str = wk_str_pushn(wk, start, len);

				/* L("split: '%s'", wk_objstr(wk, str_id)); */

				obj_array_push(wk, arr, str_id);
			}
		} else {
			if (!first) {
				start = &s[i];
				first = true;
				len = 0;
			}
			++len;
		}
	}

	return arr;
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

static void
grow_strbuf(struct workspace *wk, uint32_t len)
{
	wk->strbuf_len = len;
	if (len >= wk->strbuf_cap) {
		L("growing strbuf: %d", len);
		wk->strbuf_cap = len + 1;
		wk->strbuf = z_realloc(wk->strbuf, len);
	}

}

static uint32_t
_str_push(struct workspace *wk)
{
	uint32_t ret;

	ret = wk->strs.len;

	darr_grow_by(&wk->strs, wk->strbuf_len);

	/* LOG_I("pushing %d, to %ld (%ld)", len, wk->strs.len - ret, wk->strs.cap - ret); */
	memcpy(darr_get(&wk->strs, ret), wk->strbuf, wk->strbuf_len);

	/* L("%d, '%s'", ret, wk->strbuf); */

	if (ret > UINT32_MAX >> 1) {
		error_unrecoverable("string overflow");
	}

	return (ret << 1) | wk_id_tag_str;
}

uint32_t
wk_str_pushn(struct workspace *wk, const char *str, uint32_t n)
{
	if (n >= UINT32_MAX) {
		error_unrecoverable("string overflow");
	}

	grow_strbuf(wk, n + 1);
	strncpy(wk->strbuf, str, n);
	wk->strbuf[n] = 0;

	return _str_push(wk);
}

uint32_t
wk_str_push(struct workspace *wk, const char *str)
{
	if (!str) {
		return wk_id_tag_str;
	}

	size_t l = strlen(str);
	if (l >= UINT32_MAX) {
		error_unrecoverable("string overflow");
	}

	grow_strbuf(wk, l + 1);
	strcpy(wk->strbuf, str);

	return _str_push(wk);
}

uint32_t
wk_str_pushf(struct workspace *wk, const char *fmt, ...)
{
	uint32_t ret, len;
	va_list args, args_copy;
	va_start(args, fmt);
	va_copy(args_copy, args);

	len = vsnprintf(NULL, 0, fmt,  args_copy);

	if (len >= UINT32_MAX) {
		error_unrecoverable("string overflow");
	}

	grow_strbuf(wk, len + 1);
	vsprintf(wk->strbuf, fmt, args);

	ret = _str_push(wk);

	va_end(args_copy);
	va_end(args);

	return ret;
}

static void
_str_app(struct workspace *wk, uint32_t *_id)
{
	uint32_t curlen, cur_end, new_id;

	assert(((*_id & wk_id_tag_str) == wk_id_tag_str) && "obj passed as wk_str");

	uint32_t id = (*_id >> 1);

	curlen = strlen(wk_str(wk, *_id)) + 1;
	cur_end = id + curlen;

	if (cur_end != wk->strs.len) {
		/* L("moving '%s' to the end of pool (%d, %d)", wk_str(wk, *id), cur_end, curlen); */
		new_id = wk->strs.len;
		darr_grow_by(&wk->strs, curlen);
		memcpy(&wk->strs.e[new_id], wk_str(wk, *_id), curlen);
		id = new_id;
		/* L("result: '%s'", wk_str(wk, *id)); */
	}

	*_id = (id << 1) | wk_id_tag_str;

	assert(wk->strs.len);
	--wk->strs.len;
	_str_push(wk);
}

void
wk_str_appn(struct workspace *wk, uint32_t *id, const char *str, uint32_t n)
{
	if (n >= UINT32_MAX) {
		error_unrecoverable("string overflow");
	}

	grow_strbuf(wk, n + 1);
	strncpy(wk->strbuf, str, n);
	wk->strbuf[n] = 0;

	_str_app(wk, id);
}

void
wk_str_app(struct workspace *wk, uint32_t *id, const char *str)
{
	size_t l = strlen(str);
	if (l >= UINT32_MAX) {
		error_unrecoverable("string overflow");
	}

	grow_strbuf(wk, l + 1);
	strcpy(wk->strbuf, str);

	_str_app(wk, id);
}

void
wk_str_appf(struct workspace *wk, uint32_t *id, const char *fmt, ...)
{
	uint32_t len;
	va_list args, args_copy;
	va_start(args, fmt);
	va_copy(args_copy, args);

	len = vsnprintf(NULL, 0, fmt, args_copy);

	if (len >= UINT32_MAX) {
		error_unrecoverable("string overflow");
	}

	grow_strbuf(wk, len + 1);
	vsprintf(wk->strbuf, fmt, args);

	va_end(args_copy);
	va_end(args);

	_str_app(wk, id);
}

char *
wk_str(struct workspace *wk, uint32_t id)
{
	if ((id >> 1) == 0) {
		return NULL;
	}

	assert(((id & wk_id_tag_str) == wk_id_tag_str) && "obj passed as wk_str");
	id >>= 1;

	return darr_get(&wk->strs, id);
}

char *
wk_objstr(struct workspace *wk, uint32_t id)
{
	struct obj *obj = get_obj(wk, id);
	assert(obj->type == obj_string);
	return wk_str(wk, obj->dat.str);
}


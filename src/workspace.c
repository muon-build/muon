#include "posix.h"

#include <stdarg.h>
#include <string.h>

#include "workspace.h"
#include "interpreter.h"
#include "log.h"

struct obj *
get_obj(struct workspace *wk, uint32_t id)
{
	return darr_get(&wk->objs, id);
}

bool
get_obj_id(struct workspace *wk, const char *name, uint32_t *id)
{
	uint64_t *idp;
	if ((idp = hash_get(&wk->obj_names, name))) {
		*id = *idp;
		return true;
	} else {
		LOG_W(log_interp, "unknown object name '%s'", name);
		return false;
	}
}

struct obj *
get_obj_by_name(struct workspace *wk, const char *name)
{
	uint32_t id;
	if (!get_obj_id(wk, name, &id)) {
		return NULL;
	}

	return get_obj(wk, id);
}

struct obj *
make_obj(struct workspace *wk, uint32_t *id, enum obj_type type)
{
	*id = darr_push(&wk->objs, &(struct obj){ .type = type });
	return darr_get(&wk->objs, *id);
}

uint32_t
wk_str_push(struct workspace *wk, const char *str)
{
	uint32_t len, ret;

	len = strlen(str);
	ret = wk->strs.len;
	darr_grow_by(&wk->strs, len + 1);
	strncpy(darr_get(&wk->strs, ret), str, len + 1);

	return ret;
}

uint32_t
wk_str_vpushf(struct workspace *wk, const char *fmt, va_list args)
{
	uint32_t len, ret, len2;

	len = vsnprintf(NULL, 0, fmt, args);

	ret = wk->strs.len;
	darr_grow_by(&wk->strs, len + 1);
	len2 = vsnprintf(darr_get(&wk->strs, ret), len + 1, fmt,  args);

	assert(len == len2);

	return ret;
}

uint32_t
wk_str_pushf(struct workspace *wk, const char *fmt, ...)
{
	uint32_t ret;
	va_list args;
	va_start(args, fmt);
	ret = wk_str_vpushf(wk, fmt, args);
	va_end(args);
	return ret;
}

void
wk_strapp(struct workspace *wk, uint32_t *id, const char *fmt, ...)
{
	uint32_t curlen;
	const char *cur = wk_str(wk, *id);

	curlen = strlen(cur);

	if (*id + curlen + 1 == wk->strs.len) {
		/* L(log_misc, "str '%s' is already at the end of pool", cur); */
	} else {
		/* L(log_misc, "moving '%s' to the end of pool", cur); */
		*id = wk_str_push(wk, cur);
	}

	assert(wk->strs.len);
	--wk->strs.len;

	va_list args;
	va_start(args, fmt);
	wk_str_vpushf(wk, fmt, args);
	va_end(args);
}

char *
wk_str(struct workspace *wk, uint32_t id)
{
	return darr_get(&wk->strs, id);
}

static void
init_builtin_objects(struct workspace *wk)
{
	uint32_t id;
	make_obj(wk, &id, obj_meson);
	hash_set(&wk->obj_names, "meson", id);
}

void
workspace_init(struct workspace *wk)
{
	darr_init(&wk->objs, sizeof(struct obj));
	darr_init(&wk->tgts, sizeof(uint32_t));
	darr_init(&wk->strs, sizeof(char));
	hash_init(&wk->obj_names, 2048);

	init_builtin_objects(wk);

	make_obj(wk, &wk->project.args, obj_array);
}

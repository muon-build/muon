#include "posix.h"

#include <stdarg.h>
#include <string.h>

#include "interpreter.h"
#include "log.h"
#include "mem.h"
#include "workspace.h"

struct obj *
get_obj(struct workspace *wk, uint32_t id)
{
	return darr_get(&wk->objs, id);
}

bool
get_obj_id(struct workspace *wk, const char *name, uint32_t *id, uint32_t proj_id)
{
	uint64_t *idp;
	struct project *proj = darr_get(&wk->projects, proj_id);

	if ((idp = hash_get(&proj->scope, name))) {
		*id = *idp;
		return true;
	} else if ((idp = hash_get(&wk->scope, name))) {
		*id = *idp;
		return true;
	} else {
		return false;
	}
}

struct obj *
make_obj(struct workspace *wk, uint32_t *id, enum obj_type type)
{
	*id = darr_push(&wk->objs, &(struct obj){ .type = type });
	return darr_get(&wk->objs, *id);
}

bool
prefix_len(const char *str, const char *prefix, uint32_t *len)
{
	uint32_t l = strlen(prefix);
	if (strncmp(str, prefix, l) == 0) {
		if (strlen(str) > l && str[l] == '/') {
			++l;
		} else {
			return false;
		}
		*len = l;
		return true;
	} else {
		*len = 0;
		return false;
	}
}

uint32_t
longest_prefix_len(const char *str, const char *prefix[])
{
	uint32_t len, max = 0;
	for (; *prefix; ++prefix) {
		if (prefix_len(str, *prefix, &len)) {
			if (len > max) {
				max = len;
			}
		}
	}

	return max;
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

				/* L(log_interp, "split: '%s'", wk_objstr(wk, str_id)); */

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

	/* while (*s && !(*s == ' ' || *s == '\n')) { */
	/* 	++s; */
	/* 	++len; */
	/* } */

	return wk_str_pushn(wk, s, len);
}

static void
grow_strbuf(struct workspace *wk, uint32_t len)
{
	if (len >= wk->strbuf_cap) {
		LOG_W(log_misc, "growing strbuf: %d", len);
		wk->strbuf_cap = len + 1;
		wk->strbuf = z_realloc(wk->strbuf, len);
	}

}

uint32_t
_str_push(struct workspace *wk)
{
	uint32_t len, ret;

	len = strlen(wk->strbuf) + 1;
	ret = wk->strs.len;

	darr_grow_by(&wk->strs, len);
	strncpy(darr_get(&wk->strs, ret), wk->strbuf, len);

	/* L(log_interp, "%d, '%s'", ret, buf); */

	return ret;
}

uint32_t
wk_str_pushn(struct workspace *wk, const char *str, uint32_t n)
{
	grow_strbuf(wk, n + 1);
	strncpy(wk->strbuf, str, n);
	wk->strbuf[n] = 0;

	return _str_push(wk);
}

uint32_t
wk_str_push(struct workspace *wk, const char *str)
{
	return wk_str_pushn(wk, str, strlen(str));
}

uint32_t
wk_str_vpushf(struct workspace *wk, const char *fmt, va_list args)
{
	uint32_t len = vsnprintf(NULL, 0, fmt,  args);
	grow_strbuf(wk, len);
	vsprintf(wk->strbuf, fmt,  args);

	return _str_push(wk);
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

static void
_str_app(struct workspace *wk, uint32_t *id)
{
	uint32_t curlen, cur_end, new_id;

	curlen = strlen(wk_str(wk, *id)) + 1;
	cur_end = *id + curlen;

	if (cur_end != wk->strs.len) {
		/* L(log_misc, "moving '%s' to the end of pool (%d, %d)", wk_str(wk, *id), cur_end, curlen); */
		new_id = wk->strs.len;
		darr_grow_by(&wk->strs, curlen);
		memcpy(&wk->strs.e[new_id], wk_str(wk, *id), curlen);
		*id = new_id;
		/* L(log_misc, "result: '%s'", wk_str(wk, *id)); */
	}

	assert(wk->strs.len);
	--wk->strs.len;
	_str_push(wk);
}

void
wk_str_appn(struct workspace *wk, uint32_t *id, const char *str, uint32_t n)
{
	grow_strbuf(wk, n + 1);
	strncpy(wk->strbuf, str, n);
	wk->strbuf[n] = 0;

	_str_app(wk, id);
}

void
wk_str_app(struct workspace *wk, uint32_t *id, const char *str)
{
	wk_str_appn(wk, id, str, strlen(str));
}

void
wk_str_appf(struct workspace *wk, uint32_t *id, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	uint32_t len = vsnprintf(NULL, 0, fmt, args);
	grow_strbuf(wk, len);
	vsprintf(wk->strbuf, fmt, args);
	va_end(args);

	_str_app(wk, id);
}

char *
wk_str(struct workspace *wk, uint32_t id)
{
	if (id == 0) {
		return NULL;
	}
	return darr_get(&wk->strs, id);
}

char *
wk_objstr(struct workspace *wk, uint32_t id)
{
	struct obj *obj = get_obj(wk, id);
	assert(obj->type == obj_string);
	return wk_str(wk, obj->dat.str);
}

char *
wk_file_path(struct workspace *wk, uint32_t id)
{
	struct obj *obj = get_obj(wk, id);
	assert(obj->type == obj_file);
	return wk_str(wk, obj->dat.file);
}

struct project *
make_project(struct workspace *wk, uint32_t *id, const char *subproj_name,
	const char *cwd, const char *build_dir)
{
	*id = darr_push(&wk->projects, &(struct project){ 0 });
	struct project *proj = darr_get(&wk->projects, *id);

	darr_init(&proj->tokens, 4, sizeof(struct tokens));
	hash_init(&proj->scope, 128);

	make_obj(wk, &proj->opts, obj_dict);
	make_obj(wk, &proj->targets, obj_array);
	make_obj(wk, &proj->cfg.args, obj_array);

	if (subproj_name) {
		proj->subproject_name = wk_str_push(wk, subproj_name);
	} else {
		proj->subproject_name = 0;
	}

	proj->cwd = wk_str_push(wk, cwd);
	proj->build_dir = wk_str_push(wk, build_dir);

	return proj;
}

struct project *
current_project(struct workspace *wk)
{
	return darr_get(&wk->projects, wk->cur_project);
}

void
workspace_init(struct workspace *wk)
{
	*wk = (struct workspace){ 0 };
	darr_init(&wk->projects, 16, sizeof(struct project));
	darr_init(&wk->option_overrides, 32, sizeof(struct option_override));
	darr_init(&wk->objs, 1024, sizeof(struct obj));
	darr_init(&wk->strs, 2048, sizeof(char));
	hash_init(&wk->scope, 32);

	uint32_t id;
	make_obj(wk, &id, obj_null);
	assert(id == 0);

	make_obj(wk, &id, obj_meson);
	hash_set(&wk->scope, "meson", id);

	make_obj(wk, &id, obj_machine);
	hash_set(&wk->scope, "host_machine", id);

	darr_push(&wk->strs, &(char) { 0 });

	wk->strbuf_cap = 2048;
	wk->strbuf = z_malloc(wk->strbuf_cap);
}

void
workspace_destroy(struct workspace *wk)
{
	uint32_t i, j;
	struct project *proj;
	for (i = 0; i < wk->projects.len; ++i) {
		proj = darr_get(&wk->projects, i);

		hash_destroy(&proj->scope);

		for (j = 0; j < proj->tokens.len; ++j) {
			tokens_destroy(darr_get(&proj->tokens, j));
		}

		darr_destroy(&proj->tokens);
	}

	darr_destroy(&wk->projects);
	darr_destroy(&wk->option_overrides);
	darr_destroy(&wk->objs);
	darr_destroy(&wk->strs);
	hash_destroy(&wk->scope);

	z_free(wk->strbuf);
}

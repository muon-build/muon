#include "posix.h"

#include <stdarg.h>
#include <string.h>

#include "compilers.h"
#include "error.h"
#include "lang/eval.h"
#include "lang/interpreter.h"
#include "lang/workspace.h"
#include "log.h"
#include "output/output.h"
#include "platform/filesystem.h"
#include "platform/mem.h"
#include "platform/path.h"

enum id_tag {
	id_tag_obj = 0x0,
	id_tag_str = 0x1,
};

struct obj *
get_obj(struct workspace *wk, uint32_t id)
{
	assert(((id & id_tag_obj) == id_tag_obj) && "wk_str passed to get_obj");

	return bucket_array_get(&wk->objs, id >> 1);
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
	if (wk->objs.len > UINT32_MAX >> 1) {
		error_unrecoverable("object overflow");
	}

	*id = ((wk->objs.len) << 1) | id_tag_obj;
	return bucket_array_push(&wk->objs, &(struct obj){ .type = type });
}

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

uint32_t
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

	return (ret << 1) | id_tag_str;
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

	assert(((*_id & id_tag_str) == id_tag_str) && "obj passed as wk_str");

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

	*_id = (id << 1) | id_tag_str;

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

	assert(((id & id_tag_str) == id_tag_str) && "obj passed as wk_str");
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

	hash_init(&proj->scope, 128);

	make_obj(wk, &proj->opts, obj_dict);
	make_obj(wk, &proj->compilers, obj_dict);
	make_obj(wk, &proj->targets, obj_array);
	make_obj(wk, &proj->tests, obj_array);
	make_obj(wk, &proj->cfg.args, obj_dict);

	if (subproj_name) {
		proj->subproject_name = wk_str_push(wk, subproj_name);
	} else {
		proj->subproject_name = 0;
	}

	proj->cwd = wk_str_push(wk, cwd);
	proj->source_root = proj->cwd;
	proj->build_dir = wk_str_push(wk, build_dir);

	return proj;
}

struct project *
current_project(struct workspace *wk)
{
	return darr_get(&wk->projects, wk->cur_project);
}

void
workspace_init_bare(struct workspace *wk)
{
	*wk = (struct workspace){ 0 };

	darr_init(&wk->strs, 2048, sizeof(char));
	darr_push(&wk->strs, &(char) { 0 });

	bucket_array_init(&wk->objs, 128, sizeof(struct obj));
	uint32_t id;
	make_obj(wk, &id, obj_null);
	assert((id >> 1) == 0);

	wk->strbuf_cap = 2048;
	wk->strbuf = z_malloc(wk->strbuf_cap);
}

void
workspace_init(struct workspace *wk)
{
	workspace_init_bare(wk);

	darr_init(&wk->projects, 16, sizeof(struct project));
	darr_init(&wk->option_overrides, 32, sizeof(struct option_override));
	darr_init(&wk->source_data, 4, sizeof(struct source_data));
	hash_init(&wk->scope, 32);

	uint32_t id;
	make_obj(wk, &id, obj_meson);
	hash_set(&wk->scope, "meson", id);

	make_obj(wk, &id, obj_machine);
	hash_set(&wk->scope, "host_machine", id);

	make_obj(wk, &wk->binaries, obj_dict);
	make_obj(wk, &wk->host_machine, obj_dict);
	make_obj(wk, &wk->sources, obj_array);
}

void
workspace_destroy_bare(struct workspace *wk)
{
	darr_destroy(&wk->strs);
	bucket_array_destroy(&wk->objs);
	z_free(wk->strbuf);
}

void
workspace_destroy(struct workspace *wk)
{
	uint32_t i;
	struct project *proj;
	for (i = 0; i < wk->projects.len; ++i) {
		proj = darr_get(&wk->projects, i);

		hash_destroy(&proj->scope);
	}

	for (i = 0; i < wk->source_data.len; ++i) {
		struct source_data *sdata = darr_get(&wk->source_data, i);

		source_data_destroy(sdata);
	}

	darr_destroy(&wk->projects);
	darr_destroy(&wk->option_overrides);
	darr_destroy(&wk->source_data);
	hash_destroy(&wk->scope);

	workspace_destroy_bare(wk);
}

bool
workspace_setup_dirs(struct workspace *wk, const char *build, const char *argv0, bool mkdir)
{
	if (!path_cwd(wk->source_root, PATH_MAX)) {
		return false;
	} else if (!path_make_absolute(wk->build_root, PATH_MAX, build)) {
		return false;
	}

	if (path_is_basename(argv0)) {
		uint32_t len = strlen(argv0);
		assert(len < PATH_MAX);
		memcpy(wk->argv0, argv0, len);
	} else {
		if (!path_make_absolute(wk->argv0, PATH_MAX, argv0)) {
			return false;
		}
	}

	if (!path_join(wk->muon_private, PATH_MAX, wk->build_root, outpath.private_dir)) {
		return false;
	}

	if (mkdir) {
		if (!fs_mkdir_p(wk->muon_private)) {
			return false;
		}
	}

	return true;
}

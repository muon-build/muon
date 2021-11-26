#include "posix.h"

#include <string.h>

#include "backend/output.h"
#include "error.h"
#include "functions/default/options.h"
#include "lang/workspace.h"
#include "log.h"
#include "platform/mem.h"
#include "platform/path.h"

struct obj *
get_obj(struct workspace *wk, uint32_t id)
{
	return bucket_array_get(&wk->objs, id);
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
	*id = wk->objs.len;
	return bucket_array_push(&wk->objs, &(struct obj){ .type = type });
}

const char *
wk_file_path(struct workspace *wk, uint32_t id)
{
	struct obj *obj = get_obj(wk, id);
	assert(obj->type == obj_file);
	return get_cstr(wk, obj->dat.file);
}

struct obj *
push_install_target(struct workspace *wk, obj src, obj dest, obj mode)
{
	uint32_t id;
	struct obj *tgt = make_obj(wk, &id, obj_install_target);
	tgt->dat.install_target.src = src;
	// TODO this has a mode [, user, group]
	tgt->dat.install_target.mode = mode;

	obj sdest;
	if (path_is_absolute(get_cstr(wk, dest))) {
		sdest = dest;
	} else {
		obj prefix;
		get_option(wk, current_project(wk), "prefix", &prefix);

		char buf[PATH_MAX];
		if (!path_join(buf, PATH_MAX, get_cstr(wk, prefix), get_cstr(wk, dest))) {
			return NULL;
		}

		sdest = wk_str_push(wk, buf);
	}

	tgt->dat.install_target.dest = sdest;

	obj_array_push(wk, wk->install, id);
	return tgt;
}

struct obj *
push_install_target_install_dir(struct workspace *wk, obj ssrc,
	obj install_dir, obj mode)
{
	char basename[PATH_MAX], dest[PATH_MAX];
	if (!path_basename(basename, PATH_MAX, get_cstr(wk, ssrc))) {
		return NULL;
	} else if (!path_join(dest, PATH_MAX, get_cstr(wk, install_dir), basename)) {
		return NULL;
	}

	obj sdest = wk_str_push(wk, dest);

	return push_install_target(wk, ssrc, sdest, mode);
}

struct obj *
push_install_target_basename(struct workspace *wk, obj base_path, obj filename,
	obj install_dir, obj mode)
{
	assert(base_path);

	char src[PATH_MAX];
	if (!path_join(src, PATH_MAX, get_cstr(wk, base_path), get_cstr(wk, filename))) {
		return NULL;
	}

	char dest[PATH_MAX];
	if (!path_join(dest, PATH_MAX, get_cstr(wk, install_dir), get_cstr(wk, filename))) {
		return NULL;
	}

	return push_install_target(wk, wk_str_push(wk, src), wk_str_push(wk, dest), mode);
}

struct push_install_targets_ctx {
	obj install_dirs;
	obj install_mode;
	bool install_dirs_is_arr;
	uint32_t i;
};

static enum iteration_result
push_install_targets_iter(struct workspace *wk, void *_ctx, uint32_t val_id)
{
	struct push_install_targets_ctx *ctx = _ctx;

	assert(get_obj(wk, val_id)->type == obj_file);

	obj install_dir;

	if (ctx->install_dirs_is_arr) {
		obj_array_index(wk, ctx->install_dirs, ctx->i, &install_dir);
		assert(install_dir);
	} else {
		install_dir = ctx->install_dirs;
	}

	++ctx->i;

	struct obj *d = get_obj(wk, install_dir);

	if (d->type == obj_bool && !d->dat.boolean) {
		// skip if we get passed `false` for an install dir
		return ir_cont;
	} else if (d->type != obj_string) {
		LOG_E("install_dir values must be strings, got %s", obj_type_to_s(d->type));
		return ir_err;
	}

	if (!push_install_target_install_dir(wk, get_obj(wk, val_id)->dat.file, install_dir, ctx->install_mode)) {
		return ir_err;
	}
	return ir_cont;
}

bool
push_install_targets(struct workspace *wk, obj filenames,
	obj install_dirs, obj install_mode)
{
	struct push_install_targets_ctx ctx = {
		.install_dirs = install_dirs,
		.install_mode = install_mode,
		.install_dirs_is_arr = get_obj(wk, install_dirs)->type == obj_array,
	};

	assert(get_obj(wk, filenames)->type == obj_array);
	assert(ctx.install_dirs_is_arr || get_obj(wk, install_dirs)->type == obj_string);

	if (ctx.install_dirs_is_arr
	    && get_obj(wk, install_dirs)->dat.arr.len != get_obj(wk, filenames)->dat.arr.len) {
		LOG_E("missing/extra install_dirs");
		return false;

	}

	return obj_array_foreach(wk, filenames, &ctx, push_install_targets_iter);
}

struct project *
make_project(struct workspace *wk, uint32_t *id, const char *subproject_name,
	const char *cwd, const char *build_dir)
{
	*id = darr_push(&wk->projects, &(struct project){ 0 });
	struct project *proj = darr_get(&wk->projects, *id);

	hash_init(&proj->scope, 128);

	make_obj(wk, &proj->args, obj_dict);
	make_obj(wk, &proj->compilers, obj_dict);
	make_obj(wk, &proj->link_args, obj_dict);
	make_obj(wk, &proj->opts, obj_dict);
	make_obj(wk, &proj->summary, obj_dict);
	make_obj(wk, &proj->targets, obj_array);
	make_obj(wk, &proj->tests, obj_array);

	if (subproject_name) {
		proj->subproject_name = wk_str_push(wk, subproject_name);
	} else {
		proj->subproject_name = 0;
	}

	proj->cwd = wk_str_push(wk, cwd);
	proj->source_root = proj->cwd;
	proj->build_dir = wk_str_push(wk, build_dir);
	proj->build_root = proj->build_dir;

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

	bucket_array_init(&wk->chrs, 4096, 1);
	bucket_array_init(&wk->objs, 128, sizeof(struct obj));

	uint32_t id;
	make_obj(wk, &id, obj_null);
	assert(id == 0);
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
	make_obj(wk, &id, obj_disabler);
	assert(id == disabler_id);

	make_obj(wk, &id, obj_meson);
	hash_set(&wk->scope, "meson", id);

	make_obj(wk, &id, obj_machine);
	hash_set(&wk->scope, "host_machine", id);
	hash_set(&wk->scope, "build_machine", id);
	hash_set(&wk->scope, "target_machine", id);

	make_obj(wk, &wk->binaries, obj_dict);
	make_obj(wk, &wk->host_machine, obj_dict);
	make_obj(wk, &wk->sources, obj_array);
	make_obj(wk, &wk->install, obj_array);
	make_obj(wk, &wk->install_scripts, obj_array);
	make_obj(wk, &wk->subprojects, obj_dict);
	make_obj(wk, &wk->global_args, obj_dict);
	make_obj(wk, &wk->global_link_args, obj_dict);
}

void
workspace_destroy_bare(struct workspace *wk)
{
	bucket_array_destroy(&wk->chrs);

	uint32_t i;
	for (i = 0; i < wk->objs.len; ++i) {
		struct obj *o = bucket_array_get(&wk->objs, i);
		if (o->type == obj_string) {
			struct str *s = &o->dat.str;
			if (s->flags & str_flag_big) {
				z_free((void *)s->s);
			}
		}
	}
	bucket_array_destroy(&wk->objs);
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

	if (!path_join(wk->muon_private, PATH_MAX, wk->build_root, output_path.private_dir)) {
		return false;
	}

	if (mkdir) {
		if (!fs_mkdir_p(wk->muon_private)) {
			return false;
		}
	}

	return true;
}

static enum iteration_result
print_summaries_line_iter(struct workspace *wk, void *_ctx, obj k, obj v)
{
	obj_fprintf(wk, log_file(), "      %#o: %o\n", k, v);

	return ir_cont;
}

static enum iteration_result
print_summaries_section_iter(struct workspace *wk, void *_ctx, obj k, obj v)
{
	obj_fprintf(wk, log_file(), "    %#o\n", k);

	obj_dict_foreach(wk, v, NULL, print_summaries_line_iter);
	return ir_cont;
}

void
workspace_print_summaries(struct workspace *wk)
{
	bool printed_summary_header = false;
	uint32_t i;
	struct project *proj;
	for (i = 0; i < wk->projects.len; ++i) {
		proj = darr_get(&wk->projects, i);

		struct obj *d = get_obj(wk, proj->summary);
		if (!d->dat.dict.len) {
			continue;
		}

		if (!printed_summary_header) {
			log_plain("\n");
			LOG_I("summary:");
			printed_summary_header = true;
		}

		obj_fprintf(wk, log_file(), "- %s %s", get_cstr(wk, proj->cfg.name), get_cstr(wk, proj->cfg.version));
		obj_dict_foreach(wk, proj->summary, NULL, print_summaries_section_iter);
	}
}

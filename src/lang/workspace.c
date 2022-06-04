#include "posix.h"

#include <string.h>

#include "backend/output.h"
#include "error.h"
#include "lang/interpreter.h"
#include "lang/workspace.h"
#include "log.h"
#include "options.h"
#include "platform/mem.h"
#include "platform/path.h"

bool
get_obj_id(struct workspace *wk, const char *name, obj *res, uint32_t proj_id)
{
	uint64_t *idp;
	struct project *proj = darr_get(&wk->projects, proj_id);

	if ((idp = hash_get_str(&proj->scope, name))) {
		*res = *idp;
		return true;
	} else if ((idp = hash_get_str(&wk->scope, name))) {
		*res = *idp;
		return true;
	} else {
		return false;
	}
}

struct obj_install_target *
push_install_target(struct workspace *wk, obj src, obj dest, obj mode)
{
	obj id;
	make_obj(wk, &id, obj_install_target);
	struct obj_install_target *tgt = get_obj_install_target(wk, id);
	tgt->src = src;
	// TODO this has a mode [, user, group]
	tgt->mode = mode;

	obj sdest;
	if (path_is_absolute(get_cstr(wk, dest))) {
		sdest = dest;
	} else {
		obj prefix;
		get_option_value(wk, current_project(wk), "prefix", &prefix);

		char buf[PATH_MAX];
		if (!path_join(buf, PATH_MAX, get_cstr(wk, prefix), get_cstr(wk, dest))) {
			return NULL;
		}

		sdest = make_str(wk, buf);
	}

	tgt->dest = sdest;

	obj_array_push(wk, wk->install, id);
	return tgt;
}

struct obj_install_target *
push_install_target_install_dir(struct workspace *wk, obj src,
	obj install_dir, obj mode)
{
	char basename[PATH_MAX], dest[PATH_MAX];
	if (!path_basename(basename, PATH_MAX, get_cstr(wk, src))) {
		return NULL;
	} else if (!path_join(dest, PATH_MAX, get_cstr(wk, install_dir), basename)) {
		return NULL;
	}

	obj sdest = make_str(wk, dest);

	return push_install_target(wk, src, sdest, mode);
}

struct obj_install_target *
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

	return push_install_target(wk, make_str(wk, src), make_str(wk, dest), mode);
}

struct push_install_targets_ctx {
	obj install_dirs;
	obj install_mode;
	bool install_dirs_is_arr;
	uint32_t i;
};

static enum iteration_result
push_install_targets_iter(struct workspace *wk, void *_ctx, obj val_id)
{
	struct push_install_targets_ctx *ctx = _ctx;

	obj install_dir;

	if (ctx->install_dirs_is_arr) {
		obj_array_index(wk, ctx->install_dirs, ctx->i, &install_dir);
		assert(install_dir);
	} else {
		install_dir = ctx->install_dirs;
	}

	++ctx->i;

	enum obj_type dt = get_obj_type(wk, install_dir);

	if (dt == obj_bool && !get_obj_bool(wk, install_dir)) {
		// skip if we get passed `false` for an install dir
		return ir_cont;
	} else if (dt != obj_string) {
		LOG_E("install_dir values must be strings, got %s", obj_type_to_s(dt));
		return ir_err;
	}

	if (!push_install_target_install_dir(wk, *get_obj_file(wk, val_id), install_dir, ctx->install_mode)) {
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
		.install_dirs_is_arr = get_obj_type(wk, install_dirs) == obj_array,
	};

	assert(ctx.install_dirs_is_arr || get_obj_type(wk, install_dirs) == obj_string);

	if (ctx.install_dirs_is_arr) {
		struct obj_array *a1 = get_obj_array(wk, filenames);
		struct obj_array *a2 = get_obj_array(wk, install_dirs);
		if (a1->len != a2->len) {
			LOG_E("missing/extra install_dirs");
			return false;
		}
	}

	return obj_array_foreach(wk, filenames, &ctx, push_install_targets_iter);
}

struct project *
make_project(struct workspace *wk, uint32_t *id, const char *subproject_name,
	const char *cwd, const char *build_dir)
{
	*id = darr_push(&wk->projects, &(struct project){ 0 });
	struct project *proj = darr_get(&wk->projects, *id);

	hash_init_str(&proj->scope, 128);

	make_obj(wk, &proj->args, obj_dict);
	make_obj(wk, &proj->compilers, obj_dict);
	make_obj(wk, &proj->link_args, obj_dict);
	make_obj(wk, &proj->opts, obj_dict);
	make_obj(wk, &proj->summary, obj_dict);
	make_obj(wk, &proj->targets, obj_array);
	make_obj(wk, &proj->tests, obj_array);
	make_obj(wk, &proj->dep_cache.static_deps, obj_dict);
	make_obj(wk, &proj->dep_cache.shared_deps, obj_dict);
	make_obj(wk, &proj->wrap_provides_deps, obj_dict);
	make_obj(wk, &proj->wrap_provides_exes, obj_dict);
	proj->subprojects_dir = make_str(wk, "subprojects");

	if (subproject_name) {
		proj->subproject_name = make_str(wk, subproject_name);
	} else {
		proj->subproject_name = 0;
	}

	proj->cwd = make_str(wk, cwd);
	proj->source_root = proj->cwd;
	proj->build_dir = make_str(wk, build_dir);
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
	*wk = (struct workspace){
		.interp_node = interp_node,
		.assign_variable = assign_variable,
		.unassign_variable = unassign_variable,
		.get_variable = get_obj_id,
		.eval_project_file = eval_project_file,
	};

	bucket_array_init(&wk->chrs, 4096, 1);
	bucket_array_init(&wk->objs, 1024, sizeof(struct obj_internal));

	const struct {
		uint32_t item_size;
		uint32_t bucket_size;
	} sizes[] = {
		[obj_number] = { sizeof(int64_t), 1024 },
		[obj_string] = { sizeof(struct str), 1024 },
		[obj_compiler] = { sizeof(struct obj_compiler), 4 },
		[obj_array] = { sizeof(struct obj_array), 2048 },
		[obj_dict] = { sizeof(struct obj_dict), 512 },
		[obj_build_target] = { sizeof(struct obj_build_target), 16 },
		[obj_custom_target] = { sizeof(struct obj_custom_target), 16 },
		[obj_subproject] = { sizeof(struct obj_subproject), 16 },
		[obj_dependency] = { sizeof(struct obj_dependency), 16 },
		[obj_external_program] = { sizeof(struct obj_external_program), 32 },
		[obj_external_library] = { sizeof(struct obj_external_library), 32 },
		[obj_run_result] = { sizeof(struct obj_run_result), 32 },
		[obj_configuration_data] = { sizeof(struct obj_configuration_data), 16 },
		[obj_test] = { sizeof(struct obj_test), 64 },
		[obj_module] = { sizeof(struct obj_module), 16 },
		[obj_install_target] = { sizeof(struct obj_install_target), 128 },
		[obj_environment] = { sizeof(struct obj_environment), 4 },
		[obj_include_directory] = { sizeof(struct obj_include_directory), 16 },
		[obj_option] = { sizeof(struct obj_option), 32 },
		[obj_generator] = { sizeof(struct obj_generator), 16 },
		[obj_generated_list] = { sizeof(struct obj_generated_list), 16 },
		[obj_alias_target] = { sizeof(struct obj_alias_target), 4 },
		[obj_both_libs] = { sizeof(struct obj_both_libs), 4 },
		[obj_typeinfo] = { sizeof(struct obj_typeinfo), 4 },
	};

	uint32_t i;
	for (i = _obj_aos_start; i < obj_type_count; ++i) {
		bucket_array_init(&wk->obj_aos[i - _obj_aos_start], sizes[i].bucket_size, sizes[i].item_size);
	}

	obj id;
	make_obj(wk, &id, obj_null);
	assert(id == 0);

	hash_init(&wk->obj_hash, 128, sizeof(obj));
}

void
workspace_init(struct workspace *wk)
{
	workspace_init_bare(wk);

	strcpy(wk->argv0, "dummy");
	strcpy(wk->build_root, "/dummy");
	if (!path_cwd(wk->source_root, PATH_MAX)) {
		assert(false);
	}

	darr_init(&wk->projects, 16, sizeof(struct project));
	darr_init(&wk->option_overrides, 32, sizeof(struct option_override));
	darr_init(&wk->source_data, 4, sizeof(struct source_data));
	hash_init_str(&wk->scope, 32);

	obj id;
	make_obj(wk, &id, obj_disabler);
	assert(id == disabler_id);

	make_obj(wk, &id, obj_meson);
	hash_set_str(&wk->scope, "meson", id);

	make_obj(wk, &id, obj_machine);
	hash_set_str(&wk->scope, "host_machine", id);
	hash_set_str(&wk->scope, "build_machine", id);
	hash_set_str(&wk->scope, "target_machine", id);

	make_obj(wk, &wk->binaries, obj_dict);
	make_obj(wk, &wk->host_machine, obj_dict);
	make_obj(wk, &wk->sources, obj_array);
	make_obj(wk, &wk->install, obj_array);
	make_obj(wk, &wk->install_scripts, obj_array);
	make_obj(wk, &wk->postconf_scripts, obj_array);
	make_obj(wk, &wk->subprojects, obj_dict);
	make_obj(wk, &wk->global_args, obj_dict);
	make_obj(wk, &wk->global_link_args, obj_dict);
	make_obj(wk, &wk->dep_overrides_static, obj_dict);
	make_obj(wk, &wk->dep_overrides_dynamic, obj_dict);
	make_obj(wk, &wk->find_program_overrides, obj_dict);
	make_obj(wk, &wk->global_opts, obj_dict);

	if (!init_global_options(wk)) {
		UNREACHABLE;
	}
}

void
workspace_destroy_bare(struct workspace *wk)
{
	bucket_array_destroy(&wk->chrs);

	bucket_array_destroy(&wk->objs);

	uint32_t i;
	struct bucket_array *str_ba = &wk->obj_aos[obj_string - _obj_aos_start];
	for (i = 0; i < str_ba->len; ++i) {
		struct str *s = bucket_array_get(str_ba, i);
		if (s->flags & str_flag_big) {
			z_free((void *)s->s);
		}
	}

	for (i = _obj_aos_start; i < obj_type_count; ++i) {
		bucket_array_destroy(&wk->obj_aos[i - _obj_aos_start]);
	}

	hash_destroy(&wk->obj_hash);
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
workspace_setup_paths(struct workspace *wk, const char *build, const char *argv0,
	uint32_t argc, char *const argv[])
{
	if (!path_make_absolute(wk->build_root, PATH_MAX, build)) {
		return false;
	}

	if (path_is_basename(argv0)) {
		uint32_t len = strlen(argv0);
		assert(len < PATH_MAX);
		strcpy(wk->argv0, argv0);
	} else {
		if (!path_make_absolute(wk->argv0, PATH_MAX, argv0)) {
			return false;
		}
	}

	wk->original_commandline.argc = argc;
	wk->original_commandline.argv = argv;

	if (!path_join(wk->muon_private, PATH_MAX, wk->build_root, output_path.private_dir)) {
		return false;
	}

	if (!fs_mkdir_p(wk->muon_private)) {
		return false;
	}

	char gitignore_file[PATH_MAX];
	char hgignore_file[PATH_MAX];
	struct str *gitignore_src = &WKSTR("*\n");
	struct str *hgignore_src = &WKSTR("syntax: glob\n**/*\n");
	if (!path_join(gitignore_file, PATH_MAX, wk->build_root, ".gitignore")) {
		return false;
	}
	if (!fs_write(gitignore_file, (const uint8_t *)gitignore_src->s, gitignore_src->len)) {
		return false;
	}
	if (!path_join(hgignore_file, PATH_MAX, wk->build_root, ".hgignore")) {
		return false;
	}
	if (!fs_write(hgignore_file, (const uint8_t *)hgignore_src->s, hgignore_src->len)) {
		return false;
	}

	return true;
}

static enum iteration_result
print_summaries_line_iter(struct workspace *wk, void *_ctx, obj k, obj v)
{
	FILE *out = _ctx;

	obj_fprintf(wk, out, "      %#o: %o\n", k, v);

	return ir_cont;
}

static enum iteration_result
print_summaries_section_iter(struct workspace *wk, void *_ctx, obj k, obj v)
{
	FILE *out = _ctx;

	obj_fprintf(wk, out, "    %#o\n", k);

	obj_dict_foreach(wk, v, out, print_summaries_line_iter);
	return ir_cont;
}

void
workspace_print_summaries(struct workspace *wk, FILE *out)
{
	bool printed_summary_header = false;
	uint32_t i;
	struct project *proj;
	for (i = 0; i < wk->projects.len; ++i) {
		proj = darr_get(&wk->projects, i);

		struct obj_dict *d = get_obj_dict(wk, proj->summary);
		if (!d->len) {
			continue;
		}

		if (!printed_summary_header) {
			fprintf(out, "summary:\n");
			printed_summary_header = true;
		}

		obj_fprintf(wk, out, "- %s %s", get_cstr(wk, proj->cfg.name), get_cstr(wk, proj->cfg.version));
		obj_dict_foreach(wk, proj->summary, out, print_summaries_section_iter);
	}
}

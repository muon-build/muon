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
	make_obj(wk, &proj->include_dirs, obj_dict);
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

	wk->argv0 = "dummy";
	wk->build_root = "dummy";

	obj id;
	make_obj(wk, &id, obj_disabler);
	assert(id == disabler_id);

	SBUF(source_root);
	path_cwd(wk, &source_root);
	wk->source_root = get_cstr(wk, sbuf_into_str(wk, &source_root, false));

	darr_init(&wk->projects, 16, sizeof(struct project));
	darr_init(&wk->option_overrides, 32, sizeof(struct option_override));
	darr_init(&wk->source_data, 4, sizeof(struct source_data));
	hash_init_str(&wk->scope, 32);

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
	make_obj(wk, &wk->compiler_check_cache, obj_dict);

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
	SBUF(path);
	path_make_absolute(wk, &path, build);
	wk->build_root = get_cstr(wk, sbuf_into_str(wk, &path, true));

	if (path_is_basename(argv0)) {
		wk->argv0 = get_cstr(wk, make_str(wk, argv0));
	} else {
		path_make_absolute(wk, &path, argv0);
		wk->argv0 = get_cstr(wk, sbuf_into_str(wk, &path, true));
	}

	wk->original_commandline.argc = argc;
	wk->original_commandline.argv = argv;

	path_join(wk, &path, wk->build_root, output_path.private_dir);
	wk->muon_private = get_cstr(wk, sbuf_into_str(wk, &path, true));

	if (!fs_mkdir_p(wk->muon_private)) {
		return false;
	}

	{
		const struct str *gitignore_src = &WKSTR("*\n");
		path_join(wk, &path, wk->build_root, ".gitignore");
		if (!fs_write(path.buf, (const uint8_t *)gitignore_src->s, gitignore_src->len)) {
			return false;
		}
	}

	{
		const struct str *hgignore_src = &WKSTR("syntax: glob\n**/*\n");
		path_join(wk, &path, wk->build_root, ".hgignore");
		if (!fs_write(path.buf, (const uint8_t *)hgignore_src->s, hgignore_src->len)) {
			return false;
		}
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

	if (get_str(wk, k)->len) {
		obj_fprintf(wk, out, "    %#o\n", k);
	}

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
		if (proj->not_ok) {
			continue;
		}

		struct obj_dict *d = get_obj_dict(wk, proj->summary);
		if (!d->len) {
			continue;
		}

		if (!printed_summary_header) {
			fprintf(out, "summary:\n");
			printed_summary_header = true;
		}

		fprintf(out, "- %s %s\n", get_cstr(wk, proj->cfg.name), get_cstr(wk, proj->cfg.version));
		obj_dict_foreach(wk, proj->summary, out, print_summaries_section_iter);
	}
}

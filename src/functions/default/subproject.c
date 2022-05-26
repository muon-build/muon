#include "posix.h"

#include "functions/default/subproject.h"
#include "functions/string.h"
#include "lang/interpreter.h"
#include "log.h"
#include "options.h"
#include "platform/filesystem.h"
#include "platform/path.h"
#include "wrap.h"

static bool
subproject_prepare(struct workspace *wk, const char **cwd, const char **build_dir, bool required, bool *found)
{
	static char new_cwd[PATH_MAX], new_build_dir[PATH_MAX];

	if (!fs_dir_exists(*cwd)) {
		bool wrap_ok = false;
		char wrap_path[PATH_MAX], base_path[PATH_MAX];
		snprintf(wrap_path, PATH_MAX, "%s.wrap", *cwd);

		if (!fs_file_exists(wrap_path)) {
			goto wrap_done;
		}

		if (!path_dirname(base_path, PATH_MAX, *cwd)) {
			return false;
		}

		struct wrap wrap = { 0 };
		enum wrap_mode wrap_mode = get_option_wrap_mode(wk);
		if (!wrap_handle(wrap_path, base_path, &wrap, wrap_mode != wrap_mode_nodownload)) {
			goto wrap_cleanup;
		}

		if (wrap.fields[wf_directory]) {
			if (!path_join(new_cwd, PATH_MAX, base_path, wrap.fields[wf_directory])) {
				return false;
			}

			if (!path_dirname(base_path, PATH_MAX, *build_dir)) {
				return false;
			} else if (!path_join(new_build_dir, PATH_MAX, base_path, wrap.fields[wf_directory])) {
				return false;
			}

			*cwd = new_cwd;
			*build_dir = new_build_dir;
		}

		wrap_ok = true;
wrap_cleanup:
		wrap_destroy(&wrap);
wrap_done:
		if (!wrap_ok) {
			if (required) {
				LOG_E("project %s not found", *cwd);
				return false;
			} else {
				*found = false;
				return true;
			}
		}
	}

	char src[PATH_MAX];
	if (!path_join(src, PATH_MAX, *cwd, "meson.build")) {
		return false;
	}

	if (!fs_file_exists(src)) {
		if (required) {
			LOG_E("project %s does not contain a meson.build", *cwd);
			return false;
		} else {
			*found = false;
			return true;
		}
	}

	*found = true;
	return true;
}

bool
subproject(struct workspace *wk, obj name, enum requirement_type req, struct args_kw *default_options,
	struct args_kw *versions, obj *res)
{
	// don't re-evaluate the same subproject
	if (obj_dict_index(wk, wk->subprojects, name, res)) {
		return true;
	}

	if (req == requirement_skip) {
		make_obj(wk, res, obj_subproject);
		return true;
	}

	const char *subproj_name = get_cstr(wk, name);
	char buf[PATH_MAX], cwd[PATH_MAX], build_dir[PATH_MAX];

	if (!path_join(buf, PATH_MAX, get_cstr(wk, current_project(wk)->source_root), get_cstr(wk, current_project(wk)->subprojects_dir))) {
		return false;
	} else if (!path_join(cwd, PATH_MAX, buf, subproj_name)) {
		return false;
	}

	if (!path_join(buf, PATH_MAX, wk->build_root, get_cstr(wk, current_project(wk)->subprojects_dir))) {
		return false;
	} else if (!path_join(build_dir, PATH_MAX, buf, subproj_name)) {
		return false;
	}

	uint32_t subproject_id;
	bool found;

	const char *sp_cwd = cwd, *sp_build_dir = build_dir;

	if (!subproject_prepare(wk, &sp_cwd, &sp_build_dir, req == requirement_required, &found)) {
		return false;
	}

	if (!found) {
		make_obj(wk, res, obj_subproject);
		return true;
	}

	if (default_options && default_options->set) {
		if (!parse_and_set_default_options(wk, default_options->node,
			default_options->val, name, true)) {
			return false;
		}
	}

	if (!eval_project(wk, subproj_name, sp_cwd, sp_build_dir, &subproject_id)) {
		return false;
	}

	if (versions && versions->set) {
		struct project *subp = darr_get(&wk->projects, subproject_id);

		bool compare_result;
		if (!version_compare(wk, versions->node, get_str(wk, subp->cfg.version),
			versions->val, &compare_result)) {
			return false;
		}

		if (!compare_result) {
			if (req == requirement_required) {
				interp_error(wk, versions->node,
					"subproject version mismatch; wanted %o, got %o",
					versions->val, subp->cfg.version);
				return false;
			}
		}
	}

	make_obj(wk, res, obj_subproject);
	struct obj_subproject *sub = get_obj_subproject(wk, *res);
	sub->id = subproject_id;
	sub->found = true;
	obj_dict_set(wk, wk->subprojects, name, *res);

	if (fs_dir_exists(wk->build_root)) {
		if (!fs_mkdir_p(build_dir)) {
			return false;
		}
	}

	return true;
}

bool
func_subproject(struct workspace *wk, obj _, uint32_t args_node, obj *res)
{
	struct args_norm an[] = { { obj_string }, ARG_TYPE_NULL };
	enum kwargs {
		kw_default_options,
		kw_required,
		kw_version,

	};
	struct args_kw akw[] = {
		[kw_default_options] = { "default_options", ARG_TYPE_ARRAY_OF | obj_string },
		[kw_required] = { "required", tc_required_kw },
		[kw_version] = { "version", ARG_TYPE_ARRAY_OF | obj_string },
		0
	};

	if (!interp_args(wk, args_node, an, NULL, akw)) {
		return false;
	}

	enum requirement_type req;
	if (!coerce_requirement(wk, &akw[kw_required], &req)) {
		return false;
	}

	if (!subproject(wk, an[0].val, req, &akw[kw_default_options], &akw[kw_version], res)) {
		return false;
	}

	return true;
}

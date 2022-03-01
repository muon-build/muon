#include "posix.h"

#include <assert.h>
#include <inttypes.h>
#include <string.h>

#include "coerce.h"
#include "lang/interpreter.h"
#include "log.h"
#include "platform/filesystem.h"
#include "platform/path.h"

bool
coerce_include_type(struct workspace *wk, const struct str *str, uint32_t err_node, enum include_type *res)
{
	static const char *include_type_strs[] = {
		[include_type_preserve] = "preserve",
		[include_type_system] = "system",
		[include_type_non_system] = "non-system",
		0,
	};

	uint32_t i;
	for (i = 0; include_type_strs[i]; ++i) {
		if (str_eql(str, &WKSTR(include_type_strs[i]))) {
			*res = i;
			return true;
		}
	}

	interp_error(wk, err_node, "invalid value for include_type: %s", str->s);
	return false;
}

bool
coerce_string(struct workspace *wk, uint32_t node, obj val, obj *res)
{
	switch (get_obj_type(wk, val)) {
	case obj_bool:
		if (get_obj_bool(wk, val)) {
			*res = make_str(wk, "true");
		} else {
			*res = make_str(wk, "false");
		}
		break;
	case obj_file:
		*res = *get_obj_file(wk, val);
		break;
	case obj_number: {
		*res = make_strf(wk, "%" PRId64, get_obj_number(wk, val));
		break;
	}
	case obj_string: {
		*res = val;
		break;
	}
	default:
		interp_error(wk, node, "unable to coerce %o to string", val);
		return false;
	}

	return true;
}

struct coerce_string_array_ctx {
	uint32_t node;
	obj arr;
};

static enum iteration_result
coerce_string_array_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct coerce_string_array_ctx *ctx = _ctx;
	obj res;

	if (!coerce_string(wk, ctx->node, val, &res)) {
		return ir_err;
	}

	obj_array_push(wk, ctx->arr, res);
	return ir_cont;
}

bool
coerce_string_array(struct workspace *wk, uint32_t node, obj arr, obj *res)
{
	make_obj(wk, res, obj_array);
	struct coerce_string_array_ctx ctx = {
		.node = node,
		.arr = *res,
	};

	return obj_array_foreach(wk, arr, &ctx, coerce_string_array_iter);
}

bool
coerce_executable(struct workspace *wk, uint32_t node, obj val, obj *res)
{
	obj str;

	enum obj_type t = get_obj_type(wk, val);
	switch (t) {
	case obj_file:
		str = *get_obj_file(wk, val);
		break;
	case obj_build_target: {
		struct obj_build_target *o = get_obj_build_target(wk, val);
		char tmp1[PATH_MAX], dest[PATH_MAX];

		if (!path_join(dest, PATH_MAX, get_cstr(wk, o->build_dir),
			get_cstr(wk, o->build_name))) {
			return false;
		} else if (!path_relative_to(tmp1, PATH_MAX, wk->build_root, dest)) {
			return false;
		} else if (!path_executable(dest, PATH_MAX, tmp1)) {
			return false;
		}

		str = make_str(wk, dest);
		break;
	}
	case obj_external_program: {
		struct obj_external_program *o = get_obj_external_program(wk, val);
		if (!o->found) {
			interp_error(wk, node, "a not found external_program cannot be used here");
			return ir_err;
		}

		str = o->full_path;
		break;
	}
	default:
		interp_error(wk, node, "unable to coerce '%s' into executable", obj_type_to_s(t));
		return false;
	}

	*res = str;
	return true;
}

bool
coerce_requirement(struct workspace *wk, struct args_kw *kw_required, enum requirement_type *requirement)
{
	if (kw_required->set) {
		enum obj_type t = get_obj_type(wk, kw_required->val);

		if (t == obj_bool) {
			if (get_obj_bool(wk, kw_required->val)) {
				*requirement = requirement_required;
			} else {
				*requirement = requirement_auto;
			}
		} else if (t == obj_feature_opt) {
			switch (get_obj_feature_opt(wk, kw_required->val)->state) {
			case feature_opt_disabled:
				*requirement = requirement_skip;
				break;
			case feature_opt_enabled:
				*requirement = requirement_required;
				break;
			case feature_opt_auto:
				*requirement = requirement_auto;
				break;
			}
		} else {
			interp_error(wk, kw_required->node, "expected type %s or %s, got %s",
				obj_type_to_s(obj_bool),
				obj_type_to_s(obj_feature_opt),
				obj_type_to_s(t)
				);
			return false;
		}
	} else {
		*requirement = requirement_required;
	}

	return true;
}

typedef bool (*exists_func)(const char *);

enum coerce_into_files_mode {
	mode_input,
	mode_output,
};

struct coerce_into_files_ctx {
	uint32_t node;
	obj arr;
	const char *type, *output_dir;
	exists_func exists;
	enum coerce_into_files_mode mode;
};

static enum iteration_result
coerce_custom_target_output_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct coerce_into_files_ctx *ctx = _ctx;

	obj_array_push(wk, ctx->arr, val);
	return ir_cont;
}

bool
coerce_string_to_file(struct workspace *wk, obj string, obj *res)
{
	const char *p = get_cstr(wk, string);

	obj s2;
	if (path_is_absolute(p)) {
		s2 = string;
	} else {
		char path[PATH_MAX];
		if (!path_join(path, PATH_MAX, get_cstr(wk, current_project(wk)->cwd), p)) {
			return false;
		}

		s2 = make_str(wk, path);
	}

	make_obj(wk, res, obj_file);
	*get_obj_file(wk, *res) = s2;
	return true;
}

static enum iteration_result
coerce_into_files_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct coerce_into_files_ctx *ctx = _ctx;
	enum obj_type t = get_obj_type(wk, val);

	switch (t) {
	case obj_string: {
		obj file;
		char buf[PATH_MAX];

		switch (ctx->mode) {
		case mode_input:
			if (!coerce_string_to_file(wk, val, &file)) {
				return ir_err;
			}
			break;
		case mode_output:
			if (!path_is_basename(get_cstr(wk, val))) {
				interp_error(wk, ctx->node, "output file '%s' contains path seperators", get_cstr(wk, val));
				return ir_err;
			}

			if (!path_join(buf, PATH_MAX, ctx->output_dir, get_cstr(wk, val))) {
				return ir_err;
			}

			make_obj(wk, &file, obj_file);
			*get_obj_file(wk, file) = make_str(wk, buf);
			break;
		default:
			assert(false);
			return ir_err;
		}

		obj_array_push(wk, ctx->arr, file);
		break;
	}
	case obj_custom_target: {
		if (ctx->mode == mode_output) {
			goto type_error;
		}

		if (!obj_array_foreach(wk, get_obj_custom_target(wk, val)->output,
			ctx, coerce_custom_target_output_iter)) {
			return ir_err;
		}
		break;
	}
	case obj_build_target: {
		if (ctx->mode == mode_output) {
			goto type_error;
		}

		struct obj_build_target *tgt = get_obj_build_target(wk, val);

		char path[PATH_MAX];
		if (!path_join(path, PATH_MAX, get_cstr(wk, tgt->build_dir), get_cstr(wk, tgt->build_name))) {
			return ir_err;
		}

		obj file;
		make_obj(wk, &file, obj_file);
		*get_obj_file(wk, file) = make_str(wk, path);
		obj_array_push(wk, ctx->arr, file);
		break;
	}
	case obj_file:
		if (ctx->mode == mode_output) {
			goto type_error;
		}
		obj_array_push(wk, ctx->arr, val);
		break;
	default:
type_error:
		interp_error(wk, ctx->node, "unable to coerce object with type %s into %s",
			obj_type_to_s(t), ctx->type);
		return ir_err;
	}

	return ir_cont;
}

static bool
_coerce_files(struct workspace *wk, uint32_t node, obj val, obj *res,
	const char *type_name, exists_func exists, enum coerce_into_files_mode mode,
	const char *output_dir)
{
	make_obj(wk, res, obj_array);

	struct coerce_into_files_ctx ctx = {
		.node = node,
		.arr = *res,
		.type = type_name,
		.exists = exists,
		.mode = mode,
		.output_dir = output_dir,
	};

	switch (get_obj_type(wk, val)) {
	case obj_array:
		return obj_array_foreach_flat(wk, val, &ctx, coerce_into_files_iter);
	default:
		switch (coerce_into_files_iter(wk, &ctx, val)) {
		case ir_err:
			return false;
		default:
			return true;
		}
	}
}

bool
coerce_output_files(struct workspace *wk, uint32_t node, obj val, const char *output_dir, obj *res)
{
	return _coerce_files(wk, node, val, res, "output file", NULL, mode_output, output_dir);
}

bool
coerce_files(struct workspace *wk, uint32_t node, obj val, obj *res)
{
	return _coerce_files(wk, node, val, res, "file", fs_file_exists, mode_input, 0);
}

bool
coerce_dirs(struct workspace *wk, uint32_t node, obj val, obj *res)
{
	return _coerce_files(wk, node, val, res, "directory", fs_dir_exists, mode_input, 0);
}

struct include_directories_iter_ctx {
	uint32_t node;
	obj res;
	bool is_system;
};

static enum iteration_result
include_directories_iter(struct workspace *wk, void *_ctx, obj v)
{
	struct include_directories_iter_ctx *ctx = _ctx;
	enum obj_type t = get_obj_type(wk, v);

	if (t == obj_include_directory) {
		obj_array_push(wk, ctx->res, v);
		return ir_cont;
	} else if (t != obj_string) {
		interp_error(wk, ctx->node, "unable to coerce %o to include_directory", v);
		return ir_err;
	}

	obj path = v;
	char buf1[PATH_MAX], buf2[PATH_MAX];
	const char *p = get_cstr(wk, path);

	if (!path_is_absolute(p)) {
		if (!path_join(buf1, PATH_MAX, get_cstr(wk, current_project(wk)->cwd), p)) {
			return ir_err;
		}

		path = make_str(wk, buf1);
	}

	p = get_cstr(wk, path);

	if (!fs_dir_exists(p)) {
		interp_error(wk, ctx->node, "directory '%s' does not exist", get_cstr(wk, path));
		return ir_err;
	}

	obj inc;
	struct obj_include_directory *d;

	if (path_is_subpath(wk->source_root, p)) {
		if (!path_relative_to(buf1, PATH_MAX, wk->source_root, p)) {
			return ir_err;
		} else if (!path_join(buf2, PATH_MAX, wk->build_root, buf1)) {
			return ir_err;
		}

		make_obj(wk, &inc, obj_include_directory);
		d = get_obj_include_directory(wk, inc);
		d->path = make_str(wk, buf2);
		d->is_system = ctx->is_system;
		obj_array_push(wk, ctx->res, inc);
	}

	make_obj(wk, &inc, obj_include_directory);
	d = get_obj_include_directory(wk, inc);
	d->path = path;
	d->is_system = ctx->is_system;
	obj_array_push(wk, ctx->res, inc);

	return ir_cont;
}

bool
coerce_include_dirs(struct workspace *wk, uint32_t node, obj val, bool is_system, obj *res)
{
	struct include_directories_iter_ctx ctx = {
		.node = node,
		.is_system = is_system,
	};

	make_obj(wk, &ctx.res, obj_array);
	if (!obj_array_foreach_flat(wk, val, &ctx, include_directories_iter)) {
		return false;
	}

	*res = ctx.res;
	return true;
}

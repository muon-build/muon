#include "posix.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "embedded.h"
#include "error.h"
#include "lang/interpreter.h"
#include "lang/workspace.h"
#include "log.h"
#include "options.h"
#include "platform/path.h"

static const char *build_option_type_to_s[build_option_type_count] = {
	[op_string] = "string",
	[op_boolean] = "boolean",
	[op_combo] = "combo",
	[op_integer] = "integer",
	[op_array] = "array",
	[op_feature] = "feature",
};

static bool set_option(struct workspace *wk, uint32_t node, obj opt, obj new_val,
	enum option_value_source source, bool coerce);

static bool
parse_config_string(struct workspace *wk, const struct str *ss, struct option_override *oo)
{
	if (str_has_null(ss)) {
		LOG_E("option cannot contain NUL");
		return false;
	}

	struct str subproject = { 0 }, key = { 0 }, val = { 0 }, cur = { 0 };

	cur.s = ss->s;
	bool reading_key = true, have_subproject = false;

	uint32_t i;
	for (i = 0; i < ss->len; ++i) {
		if (reading_key) {
			if (ss->s[i] == ':') {
				if (have_subproject) {
					LOG_E("multiple ':' in option '%s'", ss->s);
					return false;
				}

				have_subproject = true;
				subproject = cur;
				cur.s = &ss->s[i + 1];
				cur.len = 0;
				continue;
			} else if (ss->s[i] == '=') {
				key = cur;
				cur.s = &ss->s[i + 1];
				cur.len = 0;
				reading_key = false;
				continue;
			}
		}

		++cur.len;
	}

	val = cur;

	if (!val.len) {
		LOG_E("expected '=' in option '%s'", ss->s);
		return false;
	} else if (!key.len) {
		LOG_E("missing option name in option '%s'", ss->s);
		return false;
	} else if (have_subproject && !subproject.len) {
		LOG_E("missing subproject in option '%s'", ss->s);
		return false;
	}

	oo->name = make_strn(wk, key.s, key.len);
	oo->val = make_strn(wk, val.s, val.len);
	if (have_subproject) {
		oo->proj = make_strn(wk, subproject.s, subproject.len);
		obj_fprintf(wk, log_file(), "subproject option override: %o:%o=%o\n", oo->proj, oo->name, oo->val);
	}

	return true;
}

static bool
subproj_name_matches(struct workspace *wk, const char *name, const char *test)
{
	if (test) {
		return name && strcmp(test, name) == 0;
	} else {
		return !name;
	}
}

static const char *
option_override_to_s(struct workspace *wk, struct option_override *oo)
{
	static char buf[BUF_SIZE_2k + 1] = { 0 };
	char buf1[BUF_SIZE_2k / 2];

	const char *val;

	if (oo->obj_value) {
		obj_to_s(wk, oo->val, buf1, BUF_SIZE_2k / 2);
		val = buf1;
	} else {
		val = get_cstr(wk, oo->val);
	}

	snprintf(buf, BUF_SIZE_2k, "%s%s%s=%s",
		oo->proj ? get_cstr(wk, oo->proj) : "",
		oo->proj ? ":" : "",
		get_cstr(wk, oo->name),
		val
		);

	return buf;
}

bool
check_invalid_subproject_option(struct workspace *wk)
{
	uint32_t i, j;
	struct option_override *oo;
	struct project *proj;
	bool found, ret = true;

	for (i = 0; i < wk->option_overrides.len; ++i) {
		oo = darr_get(&wk->option_overrides, i);
		if (!oo->proj) {
			continue;
		}

		found = false;

		for (j = 1; j < wk->projects.len; ++j) {
			proj = darr_get(&wk->projects, j);

			if (strcmp(get_cstr(wk, proj->subproject_name), get_cstr(wk, oo->proj)) == 0) {
				found = true;
				break;
			}
		}

		if (!found) {
			LOG_E("invalid option: '%s' (no such subproject)", option_override_to_s(wk, oo));
			ret = false;
		}
	}

	return ret;
}

struct check_array_opt_ctx {
	obj choices;
	uint32_t node;
};

static enum iteration_result
check_array_opt_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct check_array_opt_ctx *ctx = _ctx;

	if (!obj_array_in(wk, ctx->choices, val)) {
		interp_error(wk, ctx->node, "array element %o is not one of %o", val, ctx->choices);
		return ir_err;
	}

	return ir_cont;
}

static bool
coerce_feature_opt(struct workspace *wk, uint32_t node, const struct str *val, obj *res)
{
	enum feature_opt_state f;

	if (str_eql(val, &WKSTR("auto"))) {
		f = feature_opt_auto;
	} else if (str_eql(val, &WKSTR("enabled"))) {
		f = feature_opt_enabled;
	} else if (str_eql(val, &WKSTR("disabled"))) {
		f = feature_opt_disabled;
	} else {
		interp_error(wk, node, "unable to coerce '%s' into a feature", val->s);
		return false;
	}

	make_obj(wk, res, obj_feature_opt);
	set_obj_feature_opt(wk, *res, f);
	return true;
}

struct check_deprecated_option_ctx {
	struct obj_option *opt;
	obj *val;
	obj sval;
	uint32_t err_node;
};

static enum iteration_result
check_deprecated_option_iter(struct workspace *wk, void *_ctx, obj old, obj new)
{
	struct check_deprecated_option_ctx *ctx = _ctx;

	switch (ctx->opt->type) {
	case op_array: {
		uint32_t idx;
		if (obj_array_index_of(wk, *ctx->val, old, &idx)) {
			interp_warning(wk, ctx->err_node, "option value %o is deprecated", old);

			if (new) {
				obj_array_set(wk, *ctx->val, idx, new);
			}
		}
		break;
	}
	default:
		if (str_eql(get_str(wk, ctx->sval), get_str(wk, old))) {
			interp_warning(wk, ctx->err_node, "option value %o is deprecated", old);

			if (new) {
				*ctx->val = new;
			}
		}
	}

	return ir_cont;
}

static bool
check_deprecated_option(struct workspace *wk, uint32_t err_node,
	struct obj_option *opt, obj sval, obj *val)
{
	struct check_deprecated_option_ctx ctx = {
		.val = val,
		.sval = sval,
		.opt = opt,
		.err_node = err_node
	};

	switch (get_obj_type(wk, opt->deprecated)) {
	case obj_bool:
		if (get_obj_bool(wk, opt->deprecated)) {
			interp_warning(wk, err_node, "option %o is deprecated", ctx.opt->name);
		}
		break;
	case obj_string: {
		struct project *cur_proj = current_project(wk);

		interp_warning(wk, err_node, "option %o is deprecated to %o", opt->name, opt->deprecated);

		obj newopt;
		if (get_option(wk, cur_proj, get_str(wk, opt->deprecated), &newopt)) {
			set_option(wk, err_node, newopt, sval, option_value_source_deprecated_rename, true);
		} else {
			struct option_override oo = {
				.proj = current_project(wk)->cfg.name,
				.name = opt->deprecated,
				.val = sval,
				.source = option_value_source_deprecated_rename,
			};

			darr_push(&wk->option_overrides, &oo);
		}
		break;
	}
	case obj_dict:
	case obj_array:
		obj_iterable_foreach(wk, opt->deprecated, &ctx, check_deprecated_option_iter);
		break;
	default:
		UNREACHABLE;
	}

	return true;
}

static bool
coerce_option_override(struct workspace *wk, uint32_t node, struct obj_option *opt, obj sval, obj *res)
{
	const struct str *val = get_str(wk, sval);
	*res = 0;
	if (opt->type == op_array) {
		// coerce array early so that its elements may be checked for deprecation
		if (!val->len) {
			// make -Doption= equivalent to an empty list
			make_obj(wk, res, obj_array);
		} else {
			*res = str_split(wk, val, &WKSTR(","));
		}
	}

	if (opt->deprecated) {
		if (!check_deprecated_option(wk, node, opt, sval, res)) {
			return false;
		}

		if (*res) {
			if (get_obj_type(wk, *res) == obj_string) {
				sval = *res;
				val = get_str(wk, *res);
			} else {
				return true;
			}
		}
	}

	switch (opt->type) {
	case op_combo:
	case op_string:
		*res = sval;
		break;
	case op_boolean: {
		bool b;
		if (str_eql(val, &WKSTR("true"))) {
			b = true;
		} else if (str_eql(val, &WKSTR("false"))) {
			b = false;
		} else {
			interp_error(wk, node, "unable to coerce '%s' into a boolean", val->s);
			return false;
		}

		make_obj(wk, res, obj_bool);
		set_obj_bool(wk, *res, b);
		break;
	}
	case op_integer: {
		int64_t num;
		char *endptr;
		num = strtol(val->s, &endptr, 10);

		if (!val->len || *endptr) {
			interp_error(wk, node, "unable to coerce '%s' into a number", val->s);
			return false;
		}

		make_obj(wk, res, obj_number);
		set_obj_number(wk, *res, num);
		break;
	}
	case op_array: {
		// do nothing, array values were already coerced above
		break;
	}
	case op_feature:
		return coerce_feature_opt(wk, node, val, res);
	default:
		UNREACHABLE;
	}

	return true;
}


static bool
typecheck_opt(struct workspace *wk, uint32_t err_node, obj val, enum build_option_type type, obj *res)
{
	enum obj_type expected_type;

	if (type == op_feature && get_obj_type(wk, val) == obj_string) {
		if (!coerce_feature_opt(wk, err_node, get_str(wk, val), res)) {
			return false;
		}

		val = *res;
	}

	switch (type) {
	case op_feature: expected_type = obj_feature_opt; break;
	case op_string: expected_type = obj_string; break;
	case op_boolean: expected_type = obj_bool; break;
	case op_combo: expected_type = obj_string; break;
	case op_integer: expected_type = obj_number; break;
	case op_array: expected_type = obj_array; break;
	default:
		UNREACHABLE_RETURN;
	}

	if (!typecheck(wk, err_node, val, expected_type)) {
		return false;
	}

	*res = val;
	return true;
}

static bool
set_option(struct workspace *wk, uint32_t node, obj opt, obj new_val,
	enum option_value_source source, bool coerce)
{
	struct obj_option *o = get_obj_option(wk, opt);

	// Only set options that haven't set from a source with higher
	// precedence.  This means that option precedence doesn't have to rely
	// on the order in which options are set.  This means that e.g.
	// command-line set options can be set before the main meson.build file
	// has even been parsed.
	//
	// This mostly follows meson's behavior, except that deprecated options
	// cannot override command line options.

	/* { */
	/* 	const char *sourcenames[] = { */
	/* 		[option_value_source_unset] = "unset", */
	/* 		[option_value_source_default] = "default", */
	/* 		[option_value_source_default_options] = "default_options", */
	/* 		[option_value_source_yield] = "yield", */
	/* 		[option_value_source_commandline] = "commandline", */
	/* 		[option_value_source_deprecated_rename] = "deprecated rename", */
	/* 	}; */
	/* 	obj_fprintf(wk, log_file(), */
	/* 		"%s option %o to %o from %s, last set by %s\n", */
	/* 		o->source > source */
	/* 		? "\033[31mnot setting\033[0m" */
	/* 		: "\033[32msetting\033[0m", */
	/* 		o->name, new_val, sourcenames[source], sourcenames[o->source]); */
	/* } */

	if (o->source > source) {
		return true;
	}
	o->source = source;

	if (get_obj_type(wk, o->deprecated) == obj_bool && get_obj_bool(wk, o->deprecated)) {
		interp_warning(wk, node, "option %o is deprecated", o->name);
	}

	if (coerce) {
		obj coerced;
		if (!coerce_option_override(wk, node, o, new_val, &coerced)) {
			return false;
		}
		new_val = coerced;
	}

	if (!typecheck_opt(wk, node, new_val, o->type, &new_val)) {
		return false;
	}

	switch (o->type) {
	case op_combo: {
		if (!obj_array_in(wk, o->choices, new_val)) {
			interp_error(wk, node, "'%o' is not one of %o", new_val, o->choices);
			return false;
		}
		break;
	}
	case op_integer: {
		int64_t num = get_obj_number(wk, new_val);

		if ((o->max && num > get_obj_number(wk, o->max))
		    || (o->min && num < get_obj_number(wk, o->min)) ) {
			interp_error(wk, node, "value %" PRId64 " is out of range (%" PRId64 "..%" PRId64 ")",
				get_obj_number(wk, new_val),
				(o->min ? get_obj_number(wk, o->min) : INT64_MIN),
				(o->max ? get_obj_number(wk, o->max) : INT64_MAX)
				);
			return false;
		}
		break;
	}
	case op_array: {
		if (o->choices) {
			if (!obj_array_foreach(wk, new_val, &(struct check_array_opt_ctx) {
					.choices = o->choices,
					.node = node,
				}, check_array_opt_iter)) {
				return false;
			}
		}
		break;
	}
	case op_string:
	case op_feature:
	case op_boolean:
		break;
	default:
		UNREACHABLE_RETURN;
	}

	o->val = new_val;
	return true;
}

bool
create_option(struct workspace *wk, uint32_t node, obj opts, obj opt, obj val)
{
	if (!set_option(wk, node, opt, val, option_value_source_default, false)) {
		return false;
	}

	struct obj_option *o = get_obj_option(wk, opt);
	obj _;
	struct project *proj = NULL;
	if (wk->projects.len) {
		proj = current_project(wk);
	}

	const struct str *name = get_str(wk, o->name);
	if (str_has_null(name)
	    || strchr(name->s, ':')) {
		interp_error(wk, node, "invalid option name %o", o->name);
		return false;
	} else if (get_option(wk, proj, name, &_)) {
		interp_error(wk, node, "duplicate option %o", o->name);
		return false;
	}

	obj_dict_set(wk, opts, o->name, opt);
	return true;
}

bool
get_option(struct workspace *wk, const struct project *proj, const struct str *name, obj *res)
{
	if (proj && obj_dict_index_strn(wk, proj->opts, name->s, name->len, res)) {
		return true;
	} else if (obj_dict_index_strn(wk, wk->global_opts, name->s, name->len, res)) {
		return true;
	} else {
		return false;
	}
}

void
get_option_value(struct workspace *wk, const struct project *proj, const char *name, obj *res)
{
	obj opt;
	if (!get_option(wk, proj, &WKSTR(name), &opt)) {
		LOG_E("attempted to get unknown option '%s'", name);
		UNREACHABLE;
	}

	struct obj_option *o = get_obj_option(wk, opt);
	*res = o->val;
}

static void
set_compile_opt_from_env(struct workspace *wk, const char *name, const char *flags, const char *ldflags)
{
#ifndef MUON_BOOTSTRAPPED
	return;
#endif
	obj opt;
	if (!get_option(wk, NULL, &WKSTR(name), &opt)) {
		UNREACHABLE;
	}

	struct obj_option *o = get_obj_option(wk, opt);

	flags = getenv(flags);
	ldflags = ldflags ? getenv(ldflags) : NULL;
	if (flags && *flags) {
		o->val = str_split(wk, &WKSTR(flags), NULL);

		if (ldflags && *ldflags) {
			obj_array_extend(wk, o->val, str_split(wk, &WKSTR(ldflags), NULL));
		}
	} else if (ldflags && *ldflags) {
		o->val = str_split(wk, &WKSTR(ldflags), NULL);
	}
}

static bool
init_builtin_options(struct workspace *wk, const char *script, const char *fallback)
{
	const char *opts;
	if (!(opts = embedded_get(script))) {
		opts = fallback;
	}

	enum language_mode old_mode = wk->lang_mode;
	wk->lang_mode = language_opts;
	obj _;
	bool ret = eval_str(wk, opts, &_);
	wk->lang_mode = old_mode;
	return ret;
}

static bool
init_per_project_options(struct workspace *wk)
{
	return init_builtin_options(wk, "per_project_options.meson",
		"option('default_library', type: 'string', value: 'static')\n"
		"option('warning_level', type: 'string', value: '3')\n"
		"option('c_std', type: 'string', value: 'c99')\n"
		);
}

static enum iteration_result
set_yielding_project_options_iter(struct workspace *wk, void *_ctx, obj _k, obj opt)
{
	struct project *parent_proj = _ctx;
	struct obj_option *o = get_obj_option(wk, opt), *po;
	if (!o->yield) {
		return ir_cont;
	}

	obj parent_opt;
	if (!get_option(wk, parent_proj, get_str(wk, o->name), &parent_opt)) {
		return ir_cont;
	}

	po = get_obj_option(wk, parent_opt);
	if (po->type != o->type) {
		interp_warning(wk, 0,
			"option %o cannot yield to parent option due to a type mismatch (%s != %s)",
			o->name,
			build_option_type_to_s[po->type],
			build_option_type_to_s[o->type]
			);
		return ir_cont;
	}

	if (!set_option(wk, 0, opt, po->val, option_value_source_yield, false)) {
		return ir_err;
	}
	return ir_cont;
}

bool
setup_project_options(struct workspace *wk, const char *cwd)
{
	if (!init_per_project_options(wk)) {
		return false;
	}

	char meson_opts[PATH_MAX];
	if (!path_join(meson_opts, PATH_MAX, cwd, "meson_options.txt")) {
		return false;
	}

	if (fs_file_exists(meson_opts)) {
		enum language_mode old_mode = wk->lang_mode;
		wk->lang_mode = language_opts;
		if (!wk->eval_project_file(wk, meson_opts)) {
			return false;
		}
		wk->lang_mode = old_mode;
	}

	bool is_master_project = wk->cur_project == 0;

	if (!is_master_project) {
		if (!obj_dict_foreach(wk, current_project(wk)->opts,
			darr_get(&wk->projects, 0),
			set_yielding_project_options_iter)) {
			return false;
		}
	}

	bool ret = true;
	uint32_t i;
	struct option_override *oo;

	for (i = 0; i < wk->option_overrides.len; ++i) {
		oo = darr_get(&wk->option_overrides, i);

		if (!subproj_name_matches(wk, get_cstr(wk, current_project(wk)->subproject_name), get_cstr(wk, oo->proj))) {
			continue;
		}

		const struct str *name = get_str(wk, oo->name);
		obj opt;
		if (obj_dict_index_strn(wk, current_project(wk)->opts, name->s, name->len, &opt)
		    || (is_master_project && obj_dict_index_strn(wk, wk->global_opts, name->s, name->len, &opt))) {
			if (!set_option(wk, 0, opt, oo->val, oo->source, !oo->obj_value)) {
				ret = false;
			}
		} else {
			LOG_E("invalid option: '%s'", option_override_to_s(wk, oo));
			ret = false;
		}
	}

	return ret;
}

bool
init_global_options(struct workspace *wk)
{
	if (!init_builtin_options(wk, "global_options.meson",
		"option('buildtype', type: 'string', value: 'debugoptimized')\n"
		"option('prefix', type: 'string', value: '/usr/local')\n"
		"option('bindir', type: 'string', value: 'bin')\n"
		"option('mandir', type: 'string', value: 'share/man')\n"
		"option('wrap_mode', type: 'string', value: 'nopromote')\n"
		"option('force_fallback_for', type: 'array', value: [])\n"
		"option('pkg_config_path', type: 'string', value: '')\n"
		)) {
		return false;
	}

	set_compile_opt_from_env(wk, "c_args", "CFLAGS", NULL);
	set_compile_opt_from_env(wk, "c_link_args", "CFLAGS", "LDFLAGS");
	set_compile_opt_from_env(wk, "cpp_args", "CXXFLAGS", NULL);
	set_compile_opt_from_env(wk, "cpp_link_args", "CXXFLAGS", "LDFLAGS");
	return true;
}

bool
parse_and_set_cmdline_option(struct workspace *wk, char *lhs)
{
	struct option_override oo = { .source = option_value_source_commandline };
	if (!parse_config_string(wk, &WKSTR(lhs), &oo)) {
		return false;
	}

	darr_push(&wk->option_overrides, &oo);
	return true;
}

struct parse_and_set_default_options_ctx {
	uint32_t node;
	obj project_name;
	bool subproject;
};

static enum iteration_result
parse_and_set_default_options_iter(struct workspace *wk, void *_ctx, obj v)
{
	struct parse_and_set_default_options_ctx *ctx = _ctx;

	struct option_override oo = { .source = option_value_source_default_options };
	if (!parse_config_string(wk, get_str(wk, v), &oo)) {
		interp_error(wk, ctx->node, "invalid option string");
		return ir_err;
	}

	bool oo_for_subproject = true;
	if (!oo.proj) {
		oo_for_subproject = false;
		oo.proj = ctx->project_name;
	}

	if (ctx->subproject || oo_for_subproject) {
		oo.source = option_value_source_subproject_default_options;
		darr_push(&wk->option_overrides, &oo);
		return ir_cont;
	}

	obj opt;
	if (get_option(wk, current_project(wk), get_str(wk, oo.name), &opt)) {
		if (!set_option(wk, ctx->node, opt, oo.val, option_value_source_default_options, true)) {
			return ir_err;
		}
	} else {
		LOG_E("invalid option: '%s'", option_override_to_s(wk, &oo));
		return ir_err;
	}

	return ir_cont;
}

bool
parse_and_set_default_options(struct workspace *wk, uint32_t err_node, obj arr, obj project_name, bool is_subproject)
{
	struct parse_and_set_default_options_ctx ctx = {
		.node = err_node,
		.project_name = project_name,
		.subproject = is_subproject,
	};

	if (!obj_array_foreach(wk, arr, &ctx, parse_and_set_default_options_iter)) {
		return false;
	}

	return true;
}

/* helper functions */

enum wrap_mode
get_option_wrap_mode(struct workspace *wk)
{
	obj opt;
	get_option_value(wk, current_project(wk), "wrap_mode", &opt);

	const char *s = get_cstr(wk, opt);

	const char *names[] = {
		[wrap_mode_nopromote] = "nopromote",
		[wrap_mode_nodownload] = "nodownload",
		[wrap_mode_nofallback] = "nofallback",
		[wrap_mode_forcefallback] = "forcefallback",
		NULL,
	};

	uint32_t i;
	for (i = 0; names[i]; ++i) {
		if (strcmp(names[i], s) == 0) {
			return i;
		}
	}

	assert(false && "invalid wrap_mode set");
	return 0;
}

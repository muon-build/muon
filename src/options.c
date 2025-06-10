/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "backend/output.h"
#include "buf_size.h"
#include "embedded.h"
#include "error.h"
#include "functions/modules/subprojects.h"
#include "lang/analyze.h"
#include "lang/object_iterators.h"
#include "lang/serial.h"
#include "lang/typecheck.h"
#include "log.h"
#include "options.h"
#include "platform/assert.h"
#include "platform/filesystem.h"
#include "platform/os.h"
#include "platform/path.h"
#include "wrap.h"

bool initializing_builtin_options = false;

const char *build_option_type_to_s[build_option_type_count] = {
	[op_string] = "string",
	[op_boolean] = "boolean",
	[op_combo] = "combo",
	[op_integer] = "integer",
	[op_array] = "array",
	[op_feature] = "feature",
	[op_shell_array] = "shell_array",
};

static bool
parse_config_string(struct workspace *wk, const struct str *ss, struct option_override *oo, bool key_only)
{
	if (str_has_null(ss)) {
		LOG_E("option cannot contain NUL");
		return false;
	} else if (!key_only && !memchr(ss->s, '=', ss->len)) {
		LOG_E("option must contain =, got %s", ss->s);
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

	if (!reading_key) {
		val = cur;
	} else {
		key = cur;
	}

	if (have_subproject && !subproject.len) {
		LOG_E("missing subproject in option '%s'", ss->s);
		return false;
	}

	if (!key.len) {
		LOG_E("missing key in option '%s'", ss->s);
		return false;
	} else if (key_only && val.len) {
		LOG_E("unexpected '=' in option '%s'", ss->s);
		return false;
	}

	oo->name = make_strn(wk, key.s, key.len);
	if (!key_only) {
		oo->val = make_strn(wk, val.s, val.len);
	}
	if (have_subproject) {
		oo->proj = make_strn(wk, subproject.s, subproject.len);
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

static void
log_option_override(enum log_level lvl, struct workspace *wk, struct option_override *oo)
{
	log_plain(lvl, "'");
	if (oo->proj) {
		log_plain(lvl, "%s:", get_cstr(wk, oo->proj));
	}

	obj_lprintf(wk, lvl, "%s=%#o", get_cstr(wk, oo->name), oo->val);
	log_plain(lvl, "'");
}

bool
check_invalid_subproject_option(struct workspace *wk)
{
	uint32_t i, j;
	struct option_override *oo;
	struct project *proj;
	bool found, ret = true;

	for (i = 0; i < wk->option_overrides.len; ++i) {
		oo = arr_get(&wk->option_overrides, i);
		if (!oo->proj || oo->source < option_value_source_commandline) {
			continue;
		}

		found = false;

		for (j = 1; j < wk->projects.len; ++j) {
			proj = arr_get(&wk->projects, j);
			if (proj->not_ok) {
				continue;
			}

			if (strcmp(get_cstr(wk, proj->subproject_name), get_cstr(wk, oo->proj)) == 0) {
				found = true;
				break;
			}
		}

		if (!found) {
			LLOG_E("invalid option: ");
			log_option_override(log_error, wk, oo);
			log_plain(log_error, " (no such subproject)\n");
			ret = false;
		}
	}

	return ret;
}

static bool
coerce_feature_opt(struct workspace *wk, uint32_t ip, const struct str *val, obj *res)
{
	enum feature_opt_state f;

	if (str_eql(val, &STR("auto"))) {
		f = feature_opt_auto;
	} else if (str_eql(val, &STR("enabled"))) {
		f = feature_opt_enabled;
	} else if (str_eql(val, &STR("disabled"))) {
		f = feature_opt_disabled;
	} else {
		vm_error_at(wk, ip, "unable to coerce '%s' into a feature", val->s);
		return false;
	}

	*res = make_obj(wk, obj_feature_opt);
	set_obj_feature_opt(wk, *res, f);
	return true;
}

struct check_deprecated_option_ctx {
	struct obj_option *opt;
	obj *val;
	obj sval;
};

static enum iteration_result
check_deprecated_option_iter(struct workspace *wk, void *_ctx, obj old, obj new)
{
	struct check_deprecated_option_ctx *ctx = _ctx;

	switch (ctx->opt->type) {
	case op_shell_array: {
		break;
	}
	case op_array: {
		uint32_t idx;
		if (obj_array_index_of(wk, *ctx->val, old, &idx)) {
			vm_warning_at(wk, ctx->opt->ip, "option value %o is deprecated", old);

			if (new) {
				obj_array_set(wk, *ctx->val, idx, new);
			}
		}
		break;
	}
	default:
		if (str_eql(get_str(wk, ctx->sval), get_str(wk, old))) {
			vm_warning_at(wk, ctx->opt->ip, "option value %o is deprecated", old);

			if (new) {
				*ctx->val = new;
			}
		}
	}

	return ir_cont;
}

static bool
check_deprecated_option(struct workspace *wk, struct obj_option *opt, obj sval, obj *val)
{
	struct check_deprecated_option_ctx ctx = {
		.val = val,
		.sval = sval,
		.opt = opt,
	};

	switch (get_obj_type(wk, opt->deprecated)) {
	case obj_bool:
		if (get_obj_bool(wk, opt->deprecated)) {
			vm_warning_at(wk, opt->ip, "option %o is deprecated", ctx.opt->name);
		}
		break;
	case obj_string: {
		struct project *cur_proj = current_project(wk);

		vm_warning_at(wk, opt->ip, "option %o is deprecated to %o", opt->name, opt->deprecated);

		obj newopt;
		if (get_option(wk, cur_proj, get_str(wk, opt->deprecated), &newopt)) {
			set_option(wk, newopt, sval, option_value_source_deprecated_rename, true);
		} else {
			struct option_override oo = {
				.proj = current_project(wk)->cfg.name,
				.name = opt->deprecated,
				.val = sval,
				.source = option_value_source_deprecated_rename,
			};

			arr_push(&wk->option_overrides, &oo);
		}
		break;
	}
	case obj_dict:
	case obj_array: obj_iterable_foreach(wk, opt->deprecated, &ctx, check_deprecated_option_iter); break;
	default: UNREACHABLE;
	}

	return true;
}

static bool
coerce_option_override(struct workspace *wk, struct obj_option *opt, obj sval, obj *res)
{
	const struct str *val = get_str(wk, sval);
	*res = 0;
	if (opt->type == op_array || opt->type == op_shell_array) {
		// coerce array early so that its elements may be checked for deprecation
		if (!val->len) {
			// make -Doption= equivalent to an empty list
			*res = make_obj(wk, obj_array);
		} else if (val->s[0] == '[') {
			if (!eval_str(wk, val->s, eval_mode_repl, res)) {
				LOG_E("malformed array option value '%s'", val->s);
				return false;
			}
		} else {
			if (opt->type == op_shell_array) {
				*res = str_shell_split(wk, val, shell_type_for_host_machine());
			} else {
				*res = str_split(wk, val, &STR(","));
			}
		}
	}

	if (opt->deprecated) {
		if (!check_deprecated_option(wk, opt, sval, res)) {
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
	case op_string: *res = sval; break;
	case op_boolean: {
		bool b;
		if (str_eql(val, &STR("true"))) {
			b = true;
		} else if (str_eql(val, &STR("false"))) {
			b = false;
		} else {
			vm_error_at(wk, opt->ip, "unable to coerce '%s' into a boolean", val->s);
			return false;
		}

		*res = make_obj_bool(wk, b);
		break;
	}
	case op_integer: {
		int64_t num;
		char *endptr;
		num = strtoll(val->s, &endptr, 10);

		if (!val->len || *endptr) {
			vm_error(wk, "unable to coerce '%s' into a number", val->s);
			return false;
		}

		*res = make_obj(wk, obj_number);
		set_obj_number(wk, *res, num);
		break;
	}
	case op_shell_array:
	case op_array: {
		// do nothing, array values were already coerced above
		break;
	}
	case op_feature: return coerce_feature_opt(wk, opt->ip, val, res);
	default: UNREACHABLE;
	}

	return true;
}

static bool
typecheck_opt(struct workspace *wk, uint32_t node, obj val, enum build_option_type type, obj name, obj *res)
{
	enum obj_type expected_type;

	if (type == op_feature && get_obj_type(wk, val) == obj_string) {
		if (!coerce_feature_opt(wk, node, get_str(wk, val), res)) {
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
	case op_shell_array: expected_type = obj_array; break;
	default: UNREACHABLE_RETURN;
	}

	char msg[256];
	snprintf(msg, sizeof(msg), "expected type %%s for option %s, got %%s", get_cstr(wk, name));

	if (!typecheck_custom(wk, node, val, expected_type, msg)) {
		return false;
	}

	*res = val;
	return true;
}

struct prefix_dir_opts_ctx {
	const struct str *prefix;
};

static enum iteration_result
prefix_dir_opts_iter(struct workspace *wk, void *_ctx, obj _k, obj v)
{
	struct prefix_dir_opts_ctx *ctx = _ctx;

	struct obj_option *opt = get_obj_option(wk, v);

	if (opt->kind != build_option_kind_prefixed_dir) {
		return ir_cont;
	}

	const char *path = get_cstr(wk, opt->val);
	if (!path_is_absolute(path)) {
		return ir_cont;
	}

	TSTR(new_path);
	if (path_is_subpath(ctx->prefix->s, path)) {
		path_relative_to(wk, &new_path, ctx->prefix->s, path);
	} else {
		path_join(wk, &new_path, ctx->prefix->s, path);
	}

	opt->val = tstr_into_str(wk, &new_path);
	return ir_cont;
}

bool
prefix_dir_opts(struct workspace *wk)
{
	obj prefix;
	get_option_value(wk, NULL, "prefix", &prefix);

	return obj_dict_foreach(wk,
		wk->global_opts,
		&(struct prefix_dir_opts_ctx){
			.prefix = get_str(wk, prefix),
		},
		prefix_dir_opts_iter);
}

static void
extend_array_option(struct workspace *wk, obj opt, obj new_val, enum option_value_source source)
{
	struct obj_option *o = get_obj_option(wk, opt);

	if (o->source > source) {
		return;
	}
	o->source = source;

	obj_array_extend_nodup(wk, o->val, new_val);
}

bool
set_option(struct workspace *wk, obj opt, obj new_val, enum option_value_source source, bool coerce)
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
	/* 		[option_value_source_environment] = "environment", */
	/* 		[option_value_source_default_options] = "default_options", */
	/* 		[option_value_source_subproject_default_options] = "subproject_default_options", */
	/* 		[option_value_source_yield] = "yield", */
	/* 		[option_value_source_commandline] = "commandline", */
	/* 		[option_value_source_deprecated_rename] = "deprecated rename", */
	/* 		[option_value_source_override_options] = "override_options", */
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
		vm_warning_at(wk, o->ip, "option %o is deprecated", o->name);
	}

	if (coerce) {
		obj coerced;
		if (!coerce_option_override(wk, o, new_val, &coerced)) {
			return false;
		}
		new_val = coerced;
	}

	if (!typecheck_opt(wk, o->ip, new_val, o->type, o->name, &new_val)) {
		return false;
	}

	switch (o->type) {
	case op_combo: {
		if (!obj_array_in(wk, o->choices, new_val)) {
			vm_error_at(wk, o->ip, "'%o' is not one of %o", new_val, o->choices);
			return false;
		}

		const struct str *s = get_str(wk, new_val);
		new_val = make_strn_enum(wk, s->s, s->len, o->choices);
		break;
	}
	case op_integer: {
		int64_t num = get_obj_number(wk, new_val);

		if ((o->max && num > get_obj_number(wk, o->max)) || (o->min && num < get_obj_number(wk, o->min))) {
			vm_error_at(wk,
				o->ip,
				"value %" PRId64 " is out of range (%" PRId64 "..%" PRId64 ")",
				get_obj_number(wk, new_val),
				(o->min ? get_obj_number(wk, o->min) : INT64_MIN),
				(o->max ? get_obj_number(wk, o->max) : INT64_MAX));
			return false;
		}
		break;
	}
	case op_array: {
		if (o->choices) {
			obj val;
			obj_array_for(wk, new_val, val) {
				if (!obj_array_in(wk, o->choices, val)) {
					vm_error_at(wk, o->ip, "array element %o is not one of %o", val, o->choices);
					return false;
				}
			}
		}
		break;
	}
	case op_shell_array:
	case op_string:
	case op_feature:
	case op_boolean: break;
	default: UNREACHABLE_RETURN;
	}

	o->val = new_val;
	return true;
}

bool
create_option(struct workspace *wk, obj opts, obj opt, obj val)
{
	if (!set_option(wk, opt, val, option_value_source_default, false)) {
		return false;
	}

	struct obj_option *o = get_obj_option(wk, opt);

	if (initializing_builtin_options) {
		o->builtin = true;
	}

	obj _;
	struct project *proj = NULL;
	if (wk->projects.len) {
		proj = current_project(wk);
	}

	const struct str *name = get_str(wk, o->name);
	if (str_has_null(name) || strchr(name->s, ':')) {
		vm_error(wk, "invalid option name %o", o->name);
		return false;
	} else if (get_option(wk, proj, name, &_)) {
		vm_error(wk, "duplicate option %o", o->name);
		return false;
	}

	obj_dict_set(wk, opts, o->name, opt);
	return true;
}

bool
get_option_overridable(struct workspace *wk, const struct project *proj, obj overrides, const struct str *name, obj *res)
{
	if (overrides && obj_dict_index_strn(wk, overrides, name->s, name->len, res)) {
		return true;
	} else if (proj && obj_dict_index_strn(wk, proj->opts, name->s, name->len, res)) {
		return true;
	} else if (obj_dict_index_strn(wk, wk->global_opts, name->s, name->len, res)) {
		return true;
	} else {
		return false;
	}
}

bool
get_option(struct workspace *wk, const struct project *proj, const struct str *name, obj *res)
{
	return get_option_overridable(wk, proj, 0, name, res);
}

void
get_option_value_overridable(struct workspace *wk, const struct project *proj, obj overrides, const char *name, obj *res)
{
	obj opt;
	if (!get_option_overridable(wk, proj, overrides, &STRL(name), &opt)) {
		LOG_E("attempted to get unknown option '%s'", name);
		UNREACHABLE;
	}

	struct obj_option *o = get_obj_option(wk, opt);
	*res = o->val;
}

void
get_option_value(struct workspace *wk, const struct project *proj, const char *name, obj *res)
{
	get_option_value_overridable(wk, proj, 0, name, res);
}

static void
set_binary_from_env(struct workspace *wk, const char *envvar, const char *dest)
{
	obj opt;
	if (!get_option(wk, NULL, &STRL(dest), &opt)) {
		UNREACHABLE;
	}

	const char *v;
	if (!(v = os_get_env(envvar)) || !*v) {
		return;
	}

	obj cmd = str_shell_split(wk, &STRL(v), shell_type_for_host_machine());
	set_option(wk, opt, cmd, option_value_source_environment, false);
}

static void
set_compile_opt_from_env(struct workspace *wk, const char *name, const char *flags)
{
	obj opt;
	if (!get_option(wk, NULL, &STRL(name), &opt)) {
		UNREACHABLE;
	}

	if ((flags = os_get_env(flags)) && *flags) {
		extend_array_option(wk,
			opt,
			str_shell_split(wk, &STRL(flags), shell_type_for_host_machine()),
			option_value_source_environment);
	}
}

static void
set_str_opt_from_env(struct workspace *wk, const char *env_name, const char *opt_name)
{
	obj opt;
	if (!get_option(wk, NULL, &STRL(opt_name), &opt)) {
		UNREACHABLE;
	}

	const char *env_val;
	if ((env_val = os_get_env(env_name)) && *env_val) {
		set_option(wk, opt, make_str(wk, env_val), option_value_source_environment, false);
	}
}

static bool
init_builtin_options(struct workspace *wk, const char *script)
{
	struct source src;
	if (!embedded_get(script, &src)) {
		return false;
	}

	enum language_mode old_mode = wk->vm.lang_mode;
	wk->vm.lang_mode = language_opts;
	obj _;
	initializing_builtin_options = true;
	bool ret = eval(wk, &src, build_language_meson, 0, &_);
	initializing_builtin_options = false;
	wk->vm.lang_mode = old_mode;
	return ret;
}

static bool
init_per_project_options(struct workspace *wk)
{
	return init_builtin_options(wk, "options/per_project.meson");
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
		vm_warning_at(wk,
			0,
			"option %o cannot yield to parent option due to a type mismatch (%s != %s)",
			o->name,
			build_option_type_to_s[po->type],
			build_option_type_to_s[o->type]);
		return ir_cont;
	}

	if (!set_option(wk, opt, po->val, option_value_source_yield, false)) {
		return ir_err;
	}
	return ir_cont;
}

static bool
determine_option_file(struct workspace *wk, const char *cwd, struct tstr *out)
{
	const char *option_file_names[] = {
		"meson.options",
		"meson_options.txt",
	};

	bool exists = false;
	uint32_t i;
	for (i = 0; i < ARRAY_LEN(option_file_names); ++i) {
		path_join(wk, out, cwd, option_file_names[i]);

		if (fs_file_exists(out->buf)) {
			exists = true;
			break;
		}
	}

	return exists;
}

void
best_fuzzy_match_in_dict(struct workspace *wk, obj dict, const struct str *input, int32_t *min_d, obj *min)
{
	obj k, v;
	obj_dict_for(wk, dict, k, v) {
		(void)v;
		int32_t d;
		if (!str_fuzzy_match(input, get_str(wk, k), &d)) {
			continue;
		}
		if (d < *min_d) {
			*min_d = d;
			*min = k;
		}
	}
}

bool
setup_project_options(struct workspace *wk, const char *cwd)
{
	if (!init_per_project_options(wk)) {
		return false;
	}

	if (!cwd) {
		return true;
	}

	TSTR(meson_opts);
	bool exists = determine_option_file(wk, cwd, &meson_opts);

	if (exists) {
		enum language_mode old_mode = wk->vm.lang_mode;
		wk->vm.lang_mode = language_opts;
		if (!wk->vm.behavior.eval_project_file(wk, meson_opts.buf, build_language_meson, 0, 0)) {
			wk->vm.lang_mode = old_mode;
			return false;
		}
		wk->vm.lang_mode = old_mode;
	}

	bool is_master_project = wk->cur_project == 0;

	if (!is_master_project) {
		if (!obj_dict_foreach(wk,
			    current_project(wk)->opts,
			    arr_get(&wk->projects, 0),
			    set_yielding_project_options_iter)) {
			return false;
		}
	}

	bool ret = true;
	struct option_override *oo;

	uint32_t i;
	for (i = 0; i < wk->option_overrides.len; ++i) {
		oo = arr_get(&wk->option_overrides, i);

		if (!subproj_name_matches(
			    wk, get_cstr(wk, current_project(wk)->subproject_name), get_cstr(wk, oo->proj))) {
			continue;
		}

		const struct str *name = get_str(wk, oo->name);
		obj opt;
		if (obj_dict_index_strn(wk, current_project(wk)->opts, name->s, name->len, &opt)
			|| (is_master_project && obj_dict_index_strn(wk, wk->global_opts, name->s, name->len, &opt))) {
			if (!set_option(wk, opt, oo->val, oo->source, !oo->obj_value)) {
				ret = false;
			}
		} else {
			int32_t min_d = INT32_MAX;
			obj min_k = 0;
			best_fuzzy_match_in_dict(wk, wk->global_opts, name, &min_d, &min_k);
			best_fuzzy_match_in_dict(wk, current_project(wk)->opts,name, &min_d, &min_k);

			LLOG_E("invalid option: ");
			log_option_override(log_error, wk, oo);
			if (min_k) {
				log_plain(log_error, ", did you mean '%s'?", get_str(wk, min_k)->s);
			}
			log_plain(log_error, "\n");
			ret = false;

		}
	}

	return ret;
}

static void
make_compiler_option(struct workspace *wk, obj name)
{
	obj opt = make_obj(wk, obj_option);
	struct obj_option *o = get_obj_option(wk, opt);
	o->name = name;
	o->type = op_shell_array;
	o->ip = -1; // ?
	o->builtin = true;

	if (!create_option(wk, wk->global_opts, opt, make_obj(wk, obj_array))) {
		UNREACHABLE;
	}
}

static void
make_compiler_env_option(struct workspace *wk, enum compiler_language lang, enum toolchain_component comp)
{
	const char *env_opt = toolchain_component_option_name[lang][comp];
	if (!env_opt) {
		return;
	}

	make_compiler_option(wk, make_str(wk, env_opt));

	const char *env_var = strchr(env_opt, '.') + 1;
	set_binary_from_env(wk, env_var, env_opt);
}

const char *toolchain_component_option_name[compiler_language_count][toolchain_component_count] = {
	[compiler_language_c] = { "env.CC", "env.CC_LD" },
	[compiler_language_cpp] = { "env.CXX", "env.CXX_LD" },
	[compiler_language_objc] = { "env.OBJC", "env.OBJC_LD" },
	[compiler_language_objcpp] = { "env.OBJCXX", "env.OBJCXX_LD" },
	[compiler_language_nasm] = { "env.NASM" },
};

bool
init_global_options(struct workspace *wk)
{
	if (!init_builtin_options(wk, "options/global.meson")) {
		return false;
	}

	static struct {
		enum compiler_language l;
		const char *name;
	} langs[] = {
#define TOOLCHAIN_ENUM(lang) { compiler_language_##lang, #lang },
		FOREACH_COMPILER_EXPOSED_LANGUAGE(TOOLCHAIN_ENUM)
#undef TOOLCHAIN_ENUM
	};

	static const char *compile_opt_env_var[compiler_language_count][toolchain_component_count] = {
		[compiler_language_c] = { "CFLAGS", "LDFLAGS" },
		[compiler_language_cpp] = { "CXXFLAGS", "LDFLAGS" },
		[compiler_language_objc] = { "OBJCFLAGS", "LDFLAGS" },
		[compiler_language_objcpp] = { "OBJCXXFLAGS", "LDFLAGS" },
	};

	uint32_t i;
	for (i = 0; i < ARRAY_LEN(langs); ++i) {
		obj args = make_strf(wk, "%s_args", langs[i].name);
		make_compiler_option(wk, args);
		if (compile_opt_env_var[langs[i].l][toolchain_component_compiler]) {
			set_compile_opt_from_env(wk,
				get_str(wk, args)->s,
				compile_opt_env_var[langs[i].l][toolchain_component_compiler]);
		}

		obj link_args = make_strf(wk, "%s_link_args", langs[i].name);
		make_compiler_option(wk, link_args);
		if (compile_opt_env_var[langs[i].l][toolchain_component_linker]) {
			set_compile_opt_from_env(
				wk, get_str(wk, args)->s, compile_opt_env_var[langs[i].l][toolchain_component_linker]);
		}

		make_compiler_env_option(wk, langs[i].l, toolchain_component_compiler);
		make_compiler_env_option(wk, langs[i].l, toolchain_component_linker);
	}

	set_str_opt_from_env(wk, "PKG_CONFIG_PATH", "pkg_config_path");

	set_binary_from_env(wk, "AR", "env.AR");
	set_binary_from_env(wk, "NINJA", "env.NINJA");
	set_binary_from_env(wk, "PKG_CONFIG", "env.PKG_CONFIG");

	return true;
}

bool
parse_and_set_cmdline_option(struct workspace *wk, char *lhs)
{
	struct option_override oo = { .source = option_value_source_commandline };
	if (!parse_config_string(wk, &STRL(lhs), &oo, false)) {
		return false;
	}

	arr_push(&wk->option_overrides, &oo);
	return true;
}

enum parse_and_set_option_flag {
	parse_and_set_option_flag_override = 1 << 0,
	parse_and_set_option_flag_for_subproject = 1 << 1,
	parse_and_set_option_flag_have_value = 1 << 2,
};

struct parse_and_set_option_params {
	uint32_t node;
	obj project_name;
	obj opt;
	obj value;
	obj opts;
	enum parse_and_set_option_flag flags;
};

static bool
parse_and_set_option(struct workspace *wk, const struct parse_and_set_option_params *params)
{
	struct option_override oo = { 0 };

	if (params->flags & parse_and_set_option_flag_override) {
		oo.source = option_value_source_override_options;
	} else {
		oo.source = option_value_source_default_options;
	}

	bool key_only = false;
	if (params->flags & parse_and_set_option_flag_have_value) {
		key_only = true;
		oo.val = params->value;
		oo.obj_value = true;
	}

	if (!parse_config_string(wk, get_str(wk, params->opt), &oo, key_only)) {
		vm_error_at(wk, params->node, "invalid option string");
		return false;
	}

	if (params->flags & parse_and_set_option_flag_override) {
		if (oo.proj) {
			vm_error_at(wk, params->node, "subproject options may not be set in override_options");
			return false;
		}
	} else {
		bool oo_for_subproject = true;
		if (!oo.proj) {
			oo_for_subproject = false;
			oo.proj = params->project_name;
		}

		if ((params->flags & parse_and_set_option_flag_for_subproject) || oo_for_subproject) {
			oo.source = option_value_source_subproject_default_options;
			arr_push(&wk->option_overrides, &oo);
			return true;
		}
	}

	obj opt;
	if (!get_option(wk, current_project(wk), get_str(wk, oo.name), &opt)) {
		vm_error_at(wk, params->node, "invalid option %o", oo.name);
		return false;
	}

	if (params->flags & parse_and_set_option_flag_override) {
		obj newopt;
		newopt = make_obj(wk, obj_option);
		struct obj_option *o = get_obj_option(wk, newopt);
		*o = *get_obj_option(wk, opt);
		opt = newopt;
	}

	if (!set_option(wk, opt, oo.val, oo.source, !oo.obj_value)) {
		return false;
	}

	if (params->flags & parse_and_set_option_flag_override) {
		if (obj_dict_in(wk, params->opts, oo.name)) {
			vm_error_at(wk, params->node, "duplicate option %o in override_options", oo.name);
			return false;
		}

		obj_dict_set(wk, params->opts, oo.name, opt);
	}

	return true;
}

static bool
parse_and_set_options(struct workspace *wk, struct parse_and_set_option_params *params, obj opts)
{
	enum obj_type t = get_obj_type(wk, opts);

	switch (t) {
	case obj_dict: {
		params->flags |= parse_and_set_option_flag_have_value;

		obj k, v;
		obj_dict_for(wk, opts, k, v) {
			params->opt = k;
			params->value = v;
			if (!parse_and_set_option(wk, params)) {
				return false;
			}
		}
		break;
	}
	case obj_string:
		params->opt = opts;
		if (!parse_and_set_option(wk, params)) {
			return false;
		}
		break;
	case obj_array: {
		obj v;
		obj_array_for(wk, opts, v) {
			params->opt = v;
			if (!parse_and_set_option(wk, params)) {
				return false;
			}
		}
		break;
	}
	default: UNREACHABLE;
	}

	return true;
}

bool
parse_and_set_default_options(struct workspace *wk, uint32_t err_node, obj opts, obj project_name, bool for_subproject)
{
	struct parse_and_set_option_params params = {
		.node = err_node,
		.project_name = project_name,
	};

	if (for_subproject) {
		params.flags |= parse_and_set_option_flag_for_subproject;
	}

	return parse_and_set_options(wk, &params, opts);
}

bool
parse_and_set_override_options(struct workspace *wk, uint32_t err_node, obj opts, obj *res)
{
	struct parse_and_set_option_params params = {
		.node = err_node,
		.flags = parse_and_set_option_flag_override,
	};

	params.opts = make_obj(wk, obj_dict);
	*res = params.opts;

	return parse_and_set_options(wk, &params, opts);
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

	UNREACHABLE_RETURN;
}

enum tgt_type
get_option_default_library(struct workspace *wk)
{
	obj opt;
	get_option_value(wk, current_project(wk), "default_library", &opt);

	if (str_eql(get_str(wk, opt), &STR("static"))) {
		return tgt_static_library;
	} else if (str_eql(get_str(wk, opt), &STR("shared"))) {
		return tgt_dynamic_library;
	} else if (str_eql(get_str(wk, opt), &STR("both"))) {
		return tgt_dynamic_library | tgt_static_library;
	} else {
		UNREACHABLE_RETURN;
	}
}

enum default_both_libraries
get_option_default_both_libraries(struct workspace *wk, const struct project *proj, obj overrides)
{
	obj opt;
	get_option_value_overridable(wk, proj, overrides, "default_both_libraries", &opt);
	const struct str *s = get_str(wk, opt);

	if (str_eql(s, &STR("auto"))) {
		return default_both_libraries_auto;
	} else if (str_eql(s, &STR("static"))) {
		return default_both_libraries_static;
	} else if (str_eql(s, &STR("shared"))) {
		return default_both_libraries_shared;
	}

	UNREACHABLE_RETURN;
}

bool
get_option_bool(struct workspace *wk, obj overrides, const char *name, bool fallback)
{
	obj opt;
	if (get_option_overridable(wk, current_project(wk), overrides, &STRL(name), &opt)) {
		return get_obj_bool(wk, get_obj_option(wk, opt)->val);
	} else {
		return fallback;
	}
}

enum backend
get_option_backend(struct workspace *wk)
{
	obj backend_opt;
	get_option_value(wk, 0, "backend", &backend_opt);
	const struct str *backend_str = get_str(wk, backend_opt);
	if (str_eql(backend_str, &STR("ninja"))) {
		return backend_ninja;
	} else if (str_eql(backend_str, &STR("xcode"))) {
		return backend_xcode;
	} else {
		UNREACHABLE;
	}
}

/* options listing subcommand */

struct make_option_choices_ctx {
	obj selected;
	const char *val_clr, *sel_clr, *no_clr;
	uint32_t i, len;
	obj res;
};

static enum iteration_result
make_option_choices_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct make_option_choices_ctx *ctx = _ctx;

	const struct str *ss = get_str(wk, val);

	const char *clr = ctx->val_clr;
	if (ctx->selected && obj_array_in(wk, ctx->selected, val)) {
		clr = ctx->sel_clr;
	}

	str_app(wk, &ctx->res, clr);
	str_appn(wk, &ctx->res, ss->s, ss->len);
	str_app(wk, &ctx->res, ctx->no_clr);

	if (ctx->i < ctx->len - 1) {
		str_app(wk, &ctx->res, "|");
	}

	++ctx->i;
	return ir_cont;
}

struct list_options_ctx {
	bool show_builtin;
	const struct list_options_opts *list_opts;
	const char *subproject_name;
};

static bool
is_option_filtered(struct workspace *wk, struct obj_option *opt, bool show_builtin, bool only_modified)
{
	if (opt->builtin != show_builtin) {
		return true;
	} else if (only_modified
		   && (opt->source == option_value_source_default || opt->source == option_value_source_yield)) {
		return true;
	}

	return false;
}

static enum iteration_result
list_options_iter(struct workspace *wk, void *_ctx, obj key, obj val)
{
	struct list_options_ctx *ctx = _ctx;
	struct obj_option *opt = get_obj_option(wk, val);

	if (is_option_filtered(wk, opt, ctx->show_builtin, ctx->list_opts->only_modified)) {
		return true;
	}

	/* const char *option_type_names[] = { */
	/* 	[op_string] = "string", */
	/* 	[op_boolean] = "boolean", */
	/* 	[op_combo] = "combo", */
	/* 	[op_integer] = "integer", */
	/* 	[op_array] = "array", */
	/* 	[op_feature] = "feature", */
	/* }; */

	const char *key_clr = "", *val_clr = "", *sel_clr = "", *no_clr = "";

	if (fs_is_a_tty(stdout)) {
		key_clr = CLR(c_bold, c_blue);
		val_clr = CLR(c_bold, c_white);
		sel_clr = CLR(c_bold, c_green);
		no_clr = CLR(0);
	}

	{
		const char *subp = ctx->subproject_name;
		obj_lprintf(wk, log_info, "  -D %s%s%s%#o%s=", subp ? subp : "", subp ? ":" : "", key_clr, key, no_clr);
	}

	obj choices = 0;
	obj selected = 0;

	if (opt->type == op_combo) {
		choices = opt->choices;
		selected = make_obj(wk, obj_array);
		obj_array_push(wk, selected, opt->val);
	} else if (opt->type == op_array && opt->choices) {
		choices = opt->choices;
		selected = opt->val;
	} else {
		choices = make_obj(wk, obj_array);
		switch (opt->type) {
		case op_string: obj_array_push(wk, choices, make_str(wk, "string")); break;
		case op_boolean:
			obj_array_push(wk, choices, make_str(wk, "true"));
			obj_array_push(wk, choices, make_str(wk, "false"));
			selected = make_obj(wk, obj_array);
			obj_array_push(wk, selected, make_str(wk, get_obj_bool(wk, opt->val) ? "true" : "false"));
			break;
		case op_feature:
			obj_array_push(wk, choices, make_str(wk, "enabled"));
			obj_array_push(wk, choices, make_str(wk, "disabled"));
			obj_array_push(wk, choices, make_str(wk, "auto"));
			selected = make_obj(wk, obj_array);
			obj_array_push(wk,
				selected,
				make_str(wk,
					(char *[]){
						[feature_opt_enabled] = "enabled",
						[feature_opt_disabled] = "disabled",
						[feature_opt_auto] = "auto",
					}[get_obj_feature_opt(wk, opt->val)]));
			break;
		case op_combo:
		case op_array:
		case op_shell_array:
		case op_integer: break;
		default: UNREACHABLE;
		}
	}

	if (choices) {
		struct make_option_choices_ctx ctx = {
			.len = get_obj_array(wk, choices)->len,
			.val_clr = val_clr,
			.no_clr = no_clr,
			.sel_clr = sel_clr,
			.selected = selected,
			.res = make_str(wk, ""),
		};

		obj_array_foreach(wk, choices, &ctx, make_option_choices_iter);
		choices = ctx.res;
	}

	switch (opt->type) {
	case op_boolean:
	case op_combo:
	case op_feature: {
		obj_lprintf(wk, log_info, "<%s>", get_cstr(wk, choices));
		break;
	}
	case op_string: {
		if (opt->source == option_value_source_default) {
			const struct str *def = get_str(wk, opt->val);
			obj_lprintf(wk,
				log_info,
				"<%s>, default: %s%s%s",
				get_cstr(wk, choices),
				sel_clr,
				def->len ? def->s : "''",
				no_clr);
		} else {
			obj_lprintf(wk, log_info, "%s%o%s", sel_clr, opt->val, no_clr);
		}
		break;
	}
	case op_integer:
		if (opt->source == option_value_source_default) {
			log_plain(log_info, "<%sN%s>", val_clr, no_clr);
			if (opt->min || opt->max) {
				log_plain(log_info, " where ");
				if (opt->min) {
					obj_lprintf(wk, log_info, "%o <= ", opt->min);
				}
				log_plain(log_info, "%sN%s", val_clr, no_clr);
				if (opt->max) {
					obj_lprintf(wk, log_info, " <= %o", opt->max);
				}
			}
			obj_lprintf(wk, log_info, ", default: %s%o%s", sel_clr, opt->val, no_clr);
		} else {
			obj_lprintf(wk, log_info, "%s%o%s", sel_clr, opt->val, no_clr);
		}
		break;
	case op_shell_array:
	case op_array:
		if (opt->source == option_value_source_default) {
			if (opt->type == op_shell_array) {
				log_plain(log_info, "<shell array>");
			} else {
				log_plain(log_info, "<array>");
			}
			if (opt->choices) {
				obj_lprintf(wk, log_info, " where value in %s", get_cstr(wk, choices));
			}

			obj_lprintf(wk, log_info, ", default: %s%o%s", sel_clr, opt->val, no_clr);
		} else {
			obj_lprintf(wk, log_info, "%s%o%s", sel_clr, opt->val, no_clr);
		}
		break;
	default: UNREACHABLE;
	}

	const char *source_name = 0;
	switch (opt->source) {
	case option_value_source_unset:
	case option_value_source_default: break;
	case option_value_source_environment: source_name = "environment"; break;
	case option_value_source_yield: break;
	case option_value_source_default_options: source_name = "default_options"; break;
	case option_value_source_subproject_default_options: source_name = "subproject default_options"; break;
	case option_value_source_override_options: source_name = "override option"; break;
	case option_value_source_deprecated_rename: source_name = "deprecated rename"; break;
	case option_value_source_commandline: source_name = "commandline"; break;
	}

	if (source_name) {
		obj_lprintf(wk, log_info, "*");
	}

	if (opt->description) {
		obj_lprintf(wk, log_info, " - %#o", opt->description);
	}

	if (source_name && ctx->list_opts->list_all) {
		obj_lprintf(wk, log_info, "\n     using value from: %s", source_name);
	}

	log_plain(log_info, "\n");
	return ir_cont;
}

static enum iteration_result
list_options_for_subproject(struct workspace *wk, struct subprojects_common_ctx *ctx, const char *path)
{
	TSTR(cwd);

	struct wrap wrap = { 0 };
	if (!wrap_parse(wk, get_str(wk, current_project(wk)->subprojects_dir)->s, path, &wrap)) {
		return ir_err;
	}

	TSTR(meson_opts);
	bool exists = determine_option_file(wk, wrap.dest_dir.buf, &meson_opts);

	make_project(wk, &wk->cur_project, wrap.name.buf, wrap.dest_dir.buf, "");
	current_project(wk)->cfg.name = make_str(wk, wrap.name.buf);

	if (exists) {
		if (!wk->vm.behavior.eval_project_file(wk, meson_opts.buf, build_language_meson, 0, 0)) {
			goto cont;
		}
	} else {
		current_project(wk)->opts = make_obj(wk, obj_dict);
	}

cont:
	wrap_destroy(&wrap);
	wk->cur_project = 0;

	return ir_cont;
}

bool
list_options(const struct list_options_opts *list_opts)
{
	bool ret = false;
	struct workspace wk = { 0 };
	workspace_init_bare(&wk);
	workspace_init_runtime(&wk);
	wk.vm.lang_mode = language_opts;

	bool load_from_build_dir = false;

	if (fs_file_exists("meson.build")) {
		load_from_build_dir = false;
	} else {
		TSTR(option_info);
		path_join(&wk, &option_info, output_path.private_dir, output_path.option_info);
		if (!fs_file_exists(option_info.buf)) {
			LOG_I("this command must be run from a build directory or the project root");
			goto ret;
		}

		load_from_build_dir = true;
	}

	if (!load_from_build_dir && list_opts->list_all) {
		if (!init_global_options(&wk)) {
			UNREACHABLE;
		}
	}

	uint32_t idx;
	make_project(&wk, &idx, "dummy", path_cwd(), "");

	if (!load_from_build_dir) {
		TSTR(meson_opts);
		bool exists = determine_option_file(&wk, ".", &meson_opts);

		if (exists) {
			if (!wk.vm.behavior.eval_project_file(&wk, meson_opts.buf, build_language_meson, 0, 0)) {
				goto ret;
			}
		} else {
			current_project(&wk)->opts = make_obj(&wk, obj_dict);
		}

		{
			TSTR(subprojects_dir);
			struct workspace az_wk = { 0 };
			analyze_project_call(&az_wk);
			path_make_absolute(
				&wk, &subprojects_dir, get_cstr(&az_wk, current_project(&az_wk)->subprojects_dir));

			obj name = current_project(&az_wk)->cfg.name;
			if (name) {
				current_project(&wk)->cfg.name = make_str(&wk, get_cstr(&az_wk, name));
			}

			current_project(&wk)->subprojects_dir = tstr_into_str(&wk, &subprojects_dir);

			workspace_destroy(&az_wk);

			subprojects_foreach(&wk, 0, 0, list_options_for_subproject);
		}

		if (list_opts->list_all) {
			if (!init_per_project_options(&wk)) {
				goto ret;
			}
		}
	} else {
		obj arr;
		if (!serial_load_from_private_dir(&wk, &arr, output_path.option_info)) {
			goto ret;
		}

		uint32_t i = 0;
		obj v;
		obj_array_for(&wk, arr, v) {
			if (i == 0) {
				wk.global_opts = v;
			} else {
				uint32_t proj_i = (i - 1) / 2;
				if (proj_i >= wk.projects.len) {
					make_project(&wk, &proj_i, 0, "", "");
				}

				struct project *proj = arr_get(&wk.projects, proj_i);
				if ((i - 1) & 1) {
					proj->cfg.name = v;
				} else {
					proj->opts = v;
				}
			}

			++i;
		}
		wk.global_opts = obj_array_index(&wk, arr, 0);
		current_project(&wk)->opts = obj_array_index(&wk, arr, 1);
	}

	struct list_options_ctx ctx = { .list_opts = list_opts };

	bool had_project_options = false;

	uint32_t i;
	for (i = 0; i < wk.projects.len; ++i) {
		struct project *proj = arr_get(&wk.projects, i);

		uint32_t builtin_count = 0, non_builtin_count = 0;

		obj k, v;
		obj_dict_for(&wk, proj->opts, k, v) {
			(void)k;
			struct obj_option *opt = get_obj_option(&wk, v);

			if (opt->builtin) {
				if (is_option_filtered(&wk, opt, true, list_opts->only_modified)) {
					continue;
				}

				if (list_opts->list_all) {
					++builtin_count;
				}
			} else {
				if (is_option_filtered(&wk, opt, false, list_opts->only_modified)) {
					continue;
				}

				++non_builtin_count;
			}
		}

		if (non_builtin_count || builtin_count) {
			had_project_options = true;

			const char *name = get_cstr(&wk, proj->cfg.name);
			ctx.subproject_name = i == 0 ? 0 : name;

			if (non_builtin_count || builtin_count) {
				log_plain(log_info, "%s options:\n", name);
			}

			if (non_builtin_count) {
				obj_dict_foreach(&wk, proj->opts, &ctx, list_options_iter);

				if (!builtin_count) {
					log_plain(log_info, "\n");
				}
			}

			if (builtin_count) {
				ctx.show_builtin = true;
				log_plain(log_info, "  builtin options:\n");
				obj_dict_foreach(&wk, proj->opts, &ctx, list_options_iter);
				ctx.show_builtin = false;

				log_plain(log_info, "\n");
			}

			ctx.subproject_name = 0;
		}
	}

	if (!had_project_options && !list_opts->list_all) {
		log_plain(log_info, "no project options defined\n");
	}

	if (list_opts->list_all) {
		ctx.show_builtin = true;
		log_plain(log_info, "builtin global options:\n");
		obj_dict_foreach(&wk, wk.global_opts, &ctx, list_options_iter);
	}

	ret = true;
ret:
	workspace_destroy(&wk);
	return ret;
}

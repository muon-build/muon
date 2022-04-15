#include "posix.h"

#include <string.h>

#include "buf_size.h"
#include "functions/array.h"
#include "functions/boolean.h"
#include "functions/both_libs.h"
#include "functions/build_target.h"
#include "functions/common.h"
#include "functions/compiler.h"
#include "functions/configuration_data.h"
#include "functions/custom_target.h"
#include "functions/default.h"
#include "functions/dependency.h"
#include "functions/dict.h"
#include "functions/disabler.h"
#include "functions/environment.h"
#include "functions/external_library.h"
#include "functions/external_program.h"
#include "functions/feature_opt.h"
#include "functions/file.h"
#include "functions/generator.h"
#include "functions/machine.h"
#include "functions/meson.h"
#include "functions/modules.h"
#include "functions/number.h"
#include "functions/run_result.h"
#include "functions/string.h"
#include "functions/subproject.h"
#include "lang/interpreter.h"
#include "log.h"

// HACK: this is pretty terrible, but the least intrusive way to handle
// disablers in function arguments as they are currently implemented.  When
// interp_args sees a disabler, it sets this flag, and "fails".  In the
// function error handler we check this flag and don't raise an error if it is
// set but instead return disabler.
static bool disabler_among_args = false;
// HACK: we also need this for the is_disabler() function :(
bool disabler_among_args_immunity = false;

// HACK: This works like disabler_among_args kind of.  These opts should only
// ever be set by analyze_function().
static struct {
	bool do_analyze;
	bool pure_function;
	bool encountered_error;
} analyze_function_opts;

static bool
interp_args_interp_node(struct workspace *wk, uint32_t arg_node, obj *res)
{
	bool was_immune = disabler_among_args_immunity;
	disabler_among_args_immunity = false;

	if (!wk->interp_node(wk, arg_node, res)) {
		return false;
	}

	disabler_among_args_immunity = was_immune;
	return true;
}

static bool
next_arg(struct ast *ast, uint32_t *arg_node, uint32_t *kwarg_node, const char **kw, struct node **args)
{
	if (!*args || (*args)->type == node_empty) {
		return false;
	}

	assert((*args)->type == node_argument);

	if ((*args)->subtype == arg_kwarg) {
		*kw = get_node(ast, (*args)->l)->dat.s;
		*kwarg_node = (*args)->l;
		*arg_node = (*args)->r;
	} else {
		*kw = NULL;
		*arg_node = (*args)->l;
	}

	/* L("got arg %s:%s", *kw, node_to_s(*arg)); */

	if ((*args)->chflg & node_child_c) {
		*args = get_node(ast, (*args)->c);
	} else {
		*args = NULL;
	}

	return true;
}

static const char *
arity_to_s(struct args_norm positional_args[],
	struct args_norm optional_positional_args[],
	struct args_kw keyword_args[])
{
	static char buf[BUF_SIZE_2k + 1] = { 0 };

	uint32_t i, bufi = 0;

	bufi += snprintf(&buf[bufi], BUF_SIZE_2k - bufi, "(signature: ");

	if (positional_args) {
		bool glob = false;

		for (i = 0; positional_args[i].type != ARG_TYPE_NULL; ++i) {
			if (positional_args[i].type & ARG_TYPE_GLOB) {
				glob = true;
				break;
			}
		}

		if (i) {
			bufi += snprintf(&buf[bufi], BUF_SIZE_2k - bufi, "%d positional", i);
		}

		if (glob) {
			if (i) {
				bufi += snprintf(&buf[bufi], BUF_SIZE_2k - bufi, ", ");
			}
			bufi += snprintf(&buf[bufi], BUF_SIZE_2k - bufi, "varargs");
		}
	}

	if (optional_positional_args) {
		for (i = 0; optional_positional_args[i].type != ARG_TYPE_NULL; ++i) {
		}

		if (positional_args) {
			bufi += snprintf(&buf[bufi], BUF_SIZE_2k - bufi, ", ");
		}
		bufi += snprintf(&buf[bufi], BUF_SIZE_2k - bufi, "%d optional", i);
	}

	if (keyword_args) {
		for (i = 0; keyword_args[i].key; ++i) {
		}

		if (positional_args || optional_positional_args) {
			bufi += snprintf(&buf[bufi], BUF_SIZE_2k - bufi, ", ");
		}
		bufi += snprintf(&buf[bufi], BUF_SIZE_2k - bufi, "%d keyword", i);
	}

	if (!positional_args && !optional_positional_args && !keyword_args) {
		bufi += snprintf(&buf[bufi], BUF_SIZE_2k - bufi, "0 arguments");
	}

	bufi += snprintf(&buf[bufi], BUF_SIZE_2k - bufi, ")");

	return buf;
}

struct typecheck_function_arg_ctx {
	uint32_t err_node;
	obj arr;
	enum obj_type type;
};

static enum iteration_result
typecheck_function_arg_iter(struct workspace *wk, void *_ctx, obj val)
{
	struct typecheck_function_arg_ctx *ctx = _ctx;

	if (!typecheck(wk, ctx->err_node, val, ctx->type)) {
		return ir_err;
	}

	obj_array_push(wk, ctx->arr, val);

	return ir_cont;
}

static enum iteration_result
typecheck_function_arg_check_disabler_iter(struct workspace *wk, void *_ctx, obj val)
{
	bool *among = _ctx;

	if (val == disabler_id) {
		*among = true;
		return ir_done;
	}
	return ir_cont;
}

static bool
typecheck_function_arg(struct workspace *wk, uint32_t err_node, obj *val, enum obj_type type)
{
	if (!disabler_among_args_immunity) {
		if (*val == disabler_id) {
			disabler_among_args = true;
			return false;
		} else if (get_obj_type(wk, *val) == obj_array) {
			bool among = false;
			obj_array_foreach_flat(wk, *val, &among, typecheck_function_arg_check_disabler_iter);
			if (among) {
				disabler_among_args = true;
				return false;
			}
		}
	}

	bool array_of = false;
	if (type & ARG_TYPE_ARRAY_OF) {
		array_of = true;
		type &= ~ARG_TYPE_ARRAY_OF;
	}

	assert((type & obj_typechecking_type_tag) || type < obj_type_count);

	// If obj_file or tc_file is requested, and the arugment is an array of
	// length 1, try to unpack it.
	if (!array_of
	    && (type == obj_file || (type & tc_file) == tc_file)
	    && get_obj_type(wk, *val) == obj_array
	    && get_obj_array(wk, *val)->len == 1) {
		obj i0;
		obj_array_index(wk, *val, 0, &i0);
		if (get_obj_type(wk, i0) == obj_file) {
			*val = i0;
		}
	}

	if (!array_of) {
		return typecheck(wk, err_node, *val, type);
	}

	struct typecheck_function_arg_ctx ctx = {
		.err_node = err_node,
		.type = type,
	};
	make_obj(wk, &ctx.arr, obj_array);

	if (get_obj_type(wk, *val) == obj_array) {
		if (!obj_array_foreach_flat(wk, *val, &ctx, typecheck_function_arg_iter)) {
			return false;
		}
	} else {
		if (!typecheck_function_arg_iter(wk, &ctx, *val)) {
			return false;
		}
	}

	*val = ctx.arr;
	return true;
}

#define ARITY arity_to_s(positional_args, optional_positional_args, keyword_args)

static bool
process_kwarg(struct workspace *wk, uint32_t kwarg_node, uint32_t arg_node, struct args_kw *keyword_args, const char *kw, obj val)
{
	uint32_t i;
	for (i = 0; keyword_args[i].key; ++i) {
		if (strcmp(kw, keyword_args[i].key) == 0) {
			break;
		}
	}

	if (!keyword_args[i].key) {
		interp_error(wk, kwarg_node, "invalid kwarg: '%s'", kw);
		return false;
	}

	if (!typecheck_function_arg(wk, arg_node, &val, keyword_args[i].type)) {
		return false;
	} else if (keyword_args[i].set) {
		interp_error(wk, arg_node, "keyword argument '%s' set twice", keyword_args[i].key);
		return false;
	}

	keyword_args[i].val = val;
	keyword_args[i].node = arg_node;
	keyword_args[i].set = true;

	return true;
}

struct process_kwarg_dict_ctx {
	uint32_t kwarg_node;
	uint32_t arg_node;
	struct args_kw *keyword_args;
};

static enum iteration_result
process_kwarg_dict_iter(struct workspace *wk, void *_ctx, obj key, obj val)
{
	struct process_kwarg_dict_ctx *ctx = _ctx;

	if (!process_kwarg(wk, ctx->kwarg_node, ctx->arg_node, ctx->keyword_args, get_cstr(wk, key), val)) {
		return ir_err;
	}

	return ir_cont;
}

bool
interp_args(struct workspace *wk, uint32_t args_node,
	struct args_norm positional_args[],
	struct args_norm optional_positional_args[],
	struct args_kw keyword_args[])
{
	const char *kw;
	uint32_t arg_node, kwarg_node;
	uint32_t i, stage;
	struct args_norm *an[2] = { positional_args, optional_positional_args };
	struct node *args = get_node(wk->ast, args_node);

	for (stage = 0; stage < 2; ++stage) {
		if (!an[stage]) {
			continue;
		}

		for (i = 0; an[stage][i].type != ARG_TYPE_NULL; ++i) {
			if (an[stage][i].type & ARG_TYPE_GLOB) {
				assert(stage == 0 && "glob args must not be optional");
				assert(!optional_positional_args && "glob args cannot be followed by optional args");
				assert(an[stage][i + 1].type == ARG_TYPE_NULL && "glob args must come last");
				assert(!(an[stage][i].type & ARG_TYPE_ARRAY_OF) && "glob args are implicitly ARG_TYPE_ARRAY_OF");

				an[stage][i].type &= ~ARG_TYPE_GLOB;

				bool set_arg_node = false;

				make_obj(wk, &an[stage][i].val, obj_array);
				an[stage][i].set = true;

				while (next_arg(wk->ast, &arg_node, &kwarg_node, &kw, &args)) {
					if (kw) {
						goto kwargs;
					}

					if (!set_arg_node) {
						an[stage][i].node = arg_node;
						set_arg_node = true;
					}

					obj val;
					if (!interp_args_interp_node(wk, arg_node, &val)) {
						return false;
					}

					// If we get an array, but that isn't a valid type here, flatten it.
					if ((get_obj_type(wk, val) == obj_array
					     || (get_obj_type(wk, val) == obj_typeinfo
						 && (get_obj_typeinfo(wk, val)->type & tc_array) == tc_array))
					    && !(an[stage][i].type == obj_any
						 || an[stage][i].type == obj_array
						 || (an[stage][i].type & tc_array) == tc_array)
					    ) {
						if (get_obj_type(wk, val) == obj_typeinfo) {
							// TODO typecheck subtype
						} else {
							if (!typecheck_function_arg(wk, arg_node, &val, ARG_TYPE_ARRAY_OF | an[stage][i].type)) {
								return false;
							}

							obj_array_extend_nodup(wk, an[stage][i].val, val);
						}
					} else {
						if (!typecheck_function_arg(wk, arg_node, &val, an[stage][i].type)) {
							return false;
						}
						obj_array_push(wk, an[stage][i].val, val);
					}
				}

				if (!set_arg_node) {
					an[stage][i].node = args_node;
				}
				continue;
			}

			if (!next_arg(wk->ast, &arg_node, &kwarg_node, &kw, &args)) {
				if (stage == 0) { // required
					interp_error(wk, args_node, "missing arguments %s", ARITY);
					return false;
				} else if (stage == 1) { // optional
					goto end;
				}
			}

			if (kw) {
				if (stage == 0) {
					interp_error(wk, kwarg_node, "unexpected kwarg before required arguments %s", ARITY);
					return false;
				}

				goto kwargs;
			}

			if (!interp_args_interp_node(wk, arg_node, &an[stage][i].val)) {
				return false;
			}

			if (!typecheck_function_arg(wk, arg_node, &an[stage][i].val, an[stage][i].type)) {
				return false;
			}

			an[stage][i].node = arg_node;
			an[stage][i].set = true;
		}
	}

	if (keyword_args) {
		while (next_arg(wk->ast, &arg_node, &kwarg_node, &kw, &args)) {
			goto process_kwarg;
kwargs:
			if (!keyword_args) {
				interp_error(wk, args_node, "this function does not accept kwargs %s", ARITY);
				return false;
			}
process_kwarg:
			if (!kw) {
				interp_error(wk, arg_node, "non-kwarg after kwargs %s", ARITY);
				return false;
			}

			obj val;
			if (!interp_args_interp_node(wk, arg_node, &val)) {
				return false;
			}

			if (strcmp(kw, "kwargs") == 0) {
				if (!typecheck(wk, arg_node, val, obj_dict)) {
					return false;
				}

				struct process_kwarg_dict_ctx ctx = {
					.kwarg_node = kwarg_node,
					.arg_node = arg_node,
					.keyword_args = keyword_args
				};

				if (!obj_dict_foreach(wk, val, &ctx, process_kwarg_dict_iter)) {
					return false;
				}
			} else {
				if (!process_kwarg(wk, kwarg_node, arg_node, keyword_args, kw, val)) {
					return false;
				}
			}
		}

		for (i = 0; keyword_args[i].key; ++i) {
			if (keyword_args[i].required && !keyword_args[i].set) {
				interp_error(wk, args_node, "missing required kwarg: %s", keyword_args[i].key);
				return false;
			}
		}
	} else if (next_arg(wk->ast, &arg_node, &kwarg_node, &kw, &args)) {
		if (kw) {
			interp_error(wk, kwarg_node, "this function does not accept kwargs %s", ARITY);
		} else {
			interp_error(wk, arg_node, "too many arguments %s", ARITY);
		}

		return false;
	}

end:
	if (analyze_function_opts.do_analyze) {
		// if we are analyzing arguments only return false to halt the
		// function
		bool typeinfo_among_args = false;

		for (stage = 0; stage < 2; ++stage) {
			if (!an[stage]) {
				continue;
			}
			for (i = 0; an[stage][i].type != ARG_TYPE_NULL; ++i) {
				if (!an[stage][i].set) {
					continue;
				}

				if (get_obj_type(wk, an[stage][i].val) == obj_typeinfo) {
					typeinfo_among_args = true;
					break;
				}
			}
		}

		if (!typeinfo_among_args && keyword_args) {
			for (i = 0; keyword_args[i].key; ++i) {
				if (!keyword_args[i].set) {
					continue;
				}

				if (get_obj_type(wk, keyword_args[i].val) == obj_typeinfo) {
					typeinfo_among_args = true;
					break;
				}
			}
		}

		if (typeinfo_among_args) {
			analyze_function_opts.pure_function = false;
		}

		if (analyze_function_opts.pure_function) {
			return true;
		}

		analyze_function_opts.encountered_error = false;
		return false;
	}

	return true;
}

bool
todo(struct workspace *wk, obj rcvr_id, uint32_t args_node, obj *res)
{
	if (rcvr_id) {
		LOG_E("method on %s not implemented", obj_type_to_s(get_obj_type(wk, rcvr_id)));
	} else {
		LOG_E("function not implemented");
	}
	return false;
}

const struct func_impl_name *func_tbl[obj_type_count][language_mode_count] = {
	[obj_default] = { impl_tbl_default, impl_tbl_default_external, impl_tbl_default_opts },
	[obj_meson] = { impl_tbl_meson, },
	[obj_subproject] = { impl_tbl_subproject },
	[obj_number] = { impl_tbl_number, impl_tbl_number, },
	[obj_dependency] = { impl_tbl_dependency },
	[obj_machine] = { impl_tbl_machine, impl_tbl_machine },
	[obj_compiler] = { impl_tbl_compiler },
	[obj_feature_opt] = { impl_tbl_feature_opt },
	[obj_run_result] = { impl_tbl_run_result, impl_tbl_run_result },
	[obj_string] = { impl_tbl_string, impl_tbl_string },
	[obj_dict] = { impl_tbl_dict, impl_tbl_dict },
	[obj_external_program] = { impl_tbl_external_program, impl_tbl_external_program },
	[obj_external_library] = { impl_tbl_external_library },
	[obj_configuration_data] = { impl_tbl_configuration_data },
	[obj_custom_target] = { impl_tbl_custom_target },
	[obj_file] = { impl_tbl_file, impl_tbl_file },
	[obj_bool] = { impl_tbl_boolean, impl_tbl_boolean },
	[obj_array] = { impl_tbl_array, impl_tbl_array },
	[obj_build_target] = { impl_tbl_build_target },
	[obj_environment] = { impl_tbl_environment, impl_tbl_environment },
	[obj_disabler] = { impl_tbl_disabler, impl_tbl_disabler },
	[obj_generator] = { impl_tbl_generator, },
	[obj_both_libs] = { impl_tbl_both_libs, },
};

const struct func_impl_name *
func_lookup(const struct func_impl_name *impl_tbl, const char *name)
{
	uint32_t i;
	for (i = 0; impl_tbl[i].name; ++i) {
		if (strcmp(impl_tbl[i].name, name) == 0) {
			return &impl_tbl[i];
		}
	}

	return NULL;
}

bool
builtin_run(struct workspace *wk, bool have_rcvr, obj rcvr_id, uint32_t node_id, obj *res)
{
	const char *name;

	enum obj_type rcvr_type;
	uint32_t args_node, name_node;
	struct node *n = get_node(wk->ast, node_id);

	if (have_rcvr && !rcvr_id) {
		interp_error(wk, n->r, "tried to call function on null");
		return false;
	}

	if (have_rcvr) {
		name_node = n->r;
		args_node = n->c;
		rcvr_type = get_obj_type(wk, rcvr_id);
	} else {
		assert(n->chflg & node_child_l);
		name_node = n->l;
		args_node = n->r;
		rcvr_type = obj_default;
	}

	const struct func_impl_name *fi;
	name = get_node(wk->ast, name_node)->dat.s;

	if (rcvr_type == obj_module) {
		struct obj_module *m = get_obj_module(wk, rcvr_id);
		enum module mod = m->module;

		if (!m->found && strcmp(name, "found") != 0) {
			interp_error(wk, name_node, "invalid attempt to use missing module");
			return false;
		} else if (!(fi = module_func_lookup(name, mod))) {
			interp_error(wk, name_node, "function %s not found in module %s", name, module_names[mod]);
			return false;
		}
	} else {
		const struct func_impl_name *impl_tbl = func_tbl[rcvr_type][wk->lang_mode];

		if (!impl_tbl) {
			interp_error(wk, name_node,  "method on %s not found", obj_type_to_s(rcvr_type));
			return false;
		}

		if (!(fi = func_lookup(impl_tbl, name))) {
			if (rcvr_type == obj_disabler) {
				*res = disabler_id;
				return true;
			}

			interp_error(wk, name_node, "function %s not found", name);
			return false;
		}
	}

	if (!fi->func(wk, rcvr_id, args_node, res)) {
		if (disabler_among_args) {
			*res = disabler_id;
			disabler_among_args = false;
			return true;
		} else {
			if (rcvr_type == obj_default) {
				interp_error(wk, name_node, "in function %s",  name);
			} else {
				interp_error(wk, name_node, "in method %s.%s", obj_type_to_s(rcvr_type), name);
			}
			return false;
		}
	}
	return true;
}

bool
analyze_function(struct workspace *wk, func_impl func, uint32_t args_node, bool pure, obj *res)
{
	*res = 0;
	analyze_function_opts.do_analyze = true;
	// pure_function can be set to false even if it was true in the case
	// that any of its arguments are of type obj_typeinfo
	analyze_function_opts.pure_function = pure;
	analyze_function_opts.encountered_error = true;

	bool func_ret = func(wk, 0, args_node, res);

	if (analyze_function_opts.pure_function) {
		return func_ret;
	} else {
		return !analyze_function_opts.encountered_error;
	}
}

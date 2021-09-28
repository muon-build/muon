#include "posix.h"

#include <string.h>

#include "buf_size.h"
#include "functions/array.h"
#include "functions/boolean.h"
#include "functions/build_target.h"
#include "functions/common.h"
#include "functions/compiler.h"
#include "functions/configuration_data.h"
#include "functions/custom_target.h"
#include "functions/default.h"
#include "functions/dependency.h"
#include "functions/dict.h"
#include "functions/environment.h"
#include "functions/external_library.h"
#include "functions/external_program.h"
#include "functions/feature_opt.h"
#include "functions/file.h"
#include "functions/machine.h"
#include "functions/meson.h"
#include "functions/modules.h"
#include "functions/number.h"
#include "functions/run_result.h"
#include "functions/string.h"
#include "functions/subproject.h"
#include "lang/interpreter.h"
#include "log.h"

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

	if (!positional_args && !optional_positional_args && !keyword_args) {
		return "no args";
	}

	bufi += snprintf(&buf[bufi], BUF_SIZE_2k - bufi, "(signature: ");

	if (positional_args) {
		for (i = 0; positional_args[i].type != ARG_TYPE_NULL; ++i) {
		}

		bufi += snprintf(&buf[bufi], BUF_SIZE_2k - bufi, "%d positional", i);
	}

	if (optional_positional_args) {
		for (i = 0; optional_positional_args[i].type != ARG_TYPE_NULL; ++i) {
		}

		if (bufi) {
			bufi += snprintf(&buf[bufi], BUF_SIZE_2k - bufi, ", ");
		}
		bufi += snprintf(&buf[bufi], BUF_SIZE_2k - bufi, "%d optional", i);
	}

	if (keyword_args) {
		for (i = 0; keyword_args[i].key; ++i) {
		}

		if (bufi) {
			bufi += snprintf(&buf[bufi], BUF_SIZE_2k - bufi, ", ");
		}
		bufi += snprintf(&buf[bufi], BUF_SIZE_2k - bufi, "%d keyword", i);
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
typecheck_function_arg_iter(struct workspace *wk, void *_ctx, uint32_t val)
{
	struct typecheck_function_arg_ctx *ctx = _ctx;

	if (!typecheck(wk, ctx->err_node, val, ctx->type)) {
		return ir_err;
	}

	obj_array_push(wk, ctx->arr, val);

	return ir_cont;
}

static bool
typecheck_function_arg(struct workspace *wk, uint32_t err_node, uint32_t *val, enum obj_type type)
{
	bool array_of = false;
	if (type & ARG_TYPE_ARRAY_OF) {
		array_of = true;
		type &= ~ARG_TYPE_ARRAY_OF;
	}

	assert(type < obj_type_count);

	if (!array_of) {
		return typecheck(wk, err_node, *val, type);
	}

	struct typecheck_function_arg_ctx ctx = {
		.err_node = err_node,
		.type = type,
	};
	make_obj(wk, &ctx.arr, obj_array);

	if (get_obj(wk, *val)->type == obj_array) {
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
			if (an[stage][i].type == ARG_TYPE_GLOB) {
				assert(stage == 0 && "glob args must not be optional");
				assert(!optional_positional_args && "glob args cannot be followed by optional args");
				assert(an[stage][i + 1].type == ARG_TYPE_NULL && "glob args must come last");

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

					uint32_t val;
					if (!interp_node(wk, arg_node, &val)) {
						return false;
					}

					obj_array_push(wk, an[stage][i].val, val);
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
					return true;
				}
			}

			if (kw) {
				if (stage == 0) {
					interp_error(wk, kwarg_node, "unexpected kwarg before required arguments %s", ARITY);
					return false;
				}

				goto kwargs;
			}

			if (!interp_node(wk, arg_node, &an[stage][i].val)) {
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
			if (!interp_node(wk, arg_node, &val)) {
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

	return true;
}

bool
todo(struct workspace *wk, uint32_t rcvr_id, uint32_t args_node, uint32_t *obj)
{
	if (rcvr_id) {
		struct obj *rcvr = get_obj(wk, rcvr_id);
		LOG_E("method on %s not implemented", obj_type_to_s(rcvr->type));
	} else {
		LOG_E("function not implemented");
	}
	return false;
}

static const struct func_impl_name *func_tbl[obj_type_count][language_mode_count] = {
	[obj_default] = { impl_tbl_default, impl_tbl_default_external, impl_tbl_default_opts },
	[obj_meson] = { impl_tbl_meson, },
	[obj_subproject] = { impl_tbl_subproject },
	[obj_number] = { impl_tbl_number, impl_tbl_number, },
	[obj_dependency] = { impl_tbl_dependency },
	[obj_machine] = { impl_tbl_machine },
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
};

bool
func_lookup(const struct func_impl_name *impl_tbl, const char *name, func_impl *res)
{
	uint32_t i;
	for (i = 0; impl_tbl[i].name; ++i) {
		if (strcmp(impl_tbl[i].name, name) == 0) {
			*res = impl_tbl[i].func;
			return true;
		}
	}

	return false;
}

bool
builtin_run(struct workspace *wk, bool have_rcvr, uint32_t rcvr_id, uint32_t node_id, uint32_t *obj)
{
	const char *name;

	enum obj_type recvr_type;
	uint32_t args_node, name_node;
	struct node *n = get_node(wk->ast, node_id);

	if (have_rcvr && !rcvr_id) {
		interp_error(wk, n->r, "tried to call function on null");
		return false;
	}

	if (have_rcvr) {
		struct obj *rcvr = get_obj(wk, rcvr_id);
		name_node = n->r;
		args_node = n->c;
		recvr_type = rcvr->type;
	} else {
		assert(n->chflg & node_child_l);
		name_node = n->l;
		args_node = n->r;
		recvr_type = obj_default;
	}

	/* L("calling %s.%s", obj_type_to_s(recvr_type), name); */

	func_impl func;
	name = get_node(wk->ast, name_node)->dat.s;

	if (recvr_type == obj_module) {
		enum module mod = get_obj(wk, rcvr_id)->dat.module;

		if (!module_lookup_func(name, mod, &func)) {
			interp_error(wk, name_node, "function %s not found in module %s", name, module_names[mod]);
			return false;
		}
	} else {
		const struct func_impl_name *impl_tbl = func_tbl[recvr_type][wk->lang_mode];

		if (!impl_tbl) {
			interp_error(wk, name_node,  "method on %s not found", obj_type_to_s(recvr_type));
			return false;
		}

		if (!func_lookup(impl_tbl, name, &func)) {
			interp_error(wk, name_node, "function %s not found", name);
			return false;
		}
	}

	if (!func(wk, rcvr_id, args_node, obj)) {
		if (recvr_type == obj_default) {
			interp_error(wk, name_node, "in builtin function %s",  name);
		} else {
			interp_error(wk, name_node, "in builtin function %s.%s", obj_type_to_s(recvr_type), name);
		}
		return false;
	}
	/* L("finished calling %s.%s", obj_type_to_s(recvr_type), name); */
	return true;
}

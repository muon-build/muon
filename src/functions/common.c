#include "posix.h"

#include <string.h>

#include "functions/common.h"
#include "functions/compiler.h"
#include "functions/configuration_data.h"
#include "functions/default.h"
#include "functions/dependency.h"
#include "functions/external_program.h"
#include "functions/feature_opt.h"
#include "functions/machine.h"
#include "functions/meson.h"
#include "functions/number.h"
#include "functions/run_result.h"
#include "functions/string.h"
#include "functions/subproject.h"
#include "interpreter.h"
#include "log.h"

bool
check_lang(struct workspace *wk, uint32_t n_id, uint32_t id)
{
	if (strcmp("c", wk_objstr(wk, id)) != 0) {
		interp_error(wk, n_id, "only c is supported");
		return false;
	}

	return true;
}

static bool
next_arg(struct ast *ast, uint32_t *arg_node, uint32_t *kwarg_node, const char **kw, struct node **args)
{
	if (!*args || (*args)->type == node_empty) {
		return false;
	}

	assert((*args)->type == node_argument);

	if ((*args)->data == arg_kwarg) {
		*kw = get_node(ast, (*args)->l)->tok->dat.s;
		*kwarg_node = (*args)->l;
		*arg_node = (*args)->r;
	} else {
		*kw = NULL;
		*arg_node = (*args)->l;
	}

	/* L(log_interp, "got arg %s:%s", *kw, node_to_s(*arg)); */

	if ((*args)->chflg & node_child_c) {
		*args = get_node(ast, (*args)->c);
	} else {
		*args = NULL;
	}

	return true;
}

#define BUF_SIZE 2048

const char *
arity_to_s(struct args_norm positional_args[],
	struct args_norm optional_positional_args[],
	struct args_kw keyword_args[])
{
	static char buf[BUF_SIZE + 1] = { 0 };

	uint32_t i, bufi = 0;

	if (!positional_args && !optional_positional_args && !keyword_args) {
		return "no args";
	}

	bufi += snprintf(&buf[bufi], BUF_SIZE - bufi, "(signature: ");

	if (positional_args) {
		for (i = 0; positional_args[i].type != ARG_TYPE_NULL; ++i) {
		}

		bufi += snprintf(&buf[bufi], BUF_SIZE - bufi, "%d positional", i);
	}

	if (optional_positional_args) {
		for (i = 0; optional_positional_args[i].type != ARG_TYPE_NULL; ++i) {
		}

		if (bufi) {
			bufi += snprintf(&buf[bufi], BUF_SIZE - bufi, ", ");
		}
		bufi += snprintf(&buf[bufi], BUF_SIZE - bufi, "%d optional", i);
	}

	if (keyword_args) {
		for (i = 0; keyword_args[i].key; ++i) {
		}

		if (bufi) {
			bufi += snprintf(&buf[bufi], BUF_SIZE - bufi, ", ");
		}
		bufi += snprintf(&buf[bufi], BUF_SIZE - bufi, "%d keyword", i);
	}

	bufi += snprintf(&buf[bufi], BUF_SIZE - bufi, ")");

	return buf;
}

#define ARITY arity_to_s(positional_args, optional_positional_args, keyword_args)

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

				make_obj(wk, &an[stage][i].val, obj_array);
				an[stage][i].node = arg_node;
				an[stage][i].set = true;

				while (next_arg(wk->ast, &arg_node, &kwarg_node, &kw, &args)) {
					if (kw) {
						goto kwargs;
					}

					uint32_t val;
					if (!interp_node(wk, arg_node, &val)) {
						return false;
					}

					obj_array_push(wk, an[stage][i].val, val);
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

			if (!typecheck(wk, arg_node, an[stage][i].val, an[stage][i].type)) {
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

			for (i = 0; keyword_args[i].key; ++i) {
				if (strcmp(kw, keyword_args[i].key) == 0) {
					if (!interp_node(wk, arg_node, &keyword_args[i].val)) {
						return false;
					}

					if (!typecheck(wk, arg_node, keyword_args[i].val, keyword_args[i].type)) {
						return false;
					}

					keyword_args[i].node = arg_node;
					keyword_args[i].set = true;
					break;
				}
			}

			if (!keyword_args[i].key) {
				interp_error(wk, kwarg_node, "invalid kwarg");
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
		LOG_W(log_misc, "method on %s not implemented", obj_type_to_s(rcvr->type));
	} else {
		LOG_W(log_misc, "function not implemented");
	}
	return false;
}

static const struct func_impl_name *func_tbl[obj_type_count][language_mode_count] = {
	[obj_default]     = { impl_tbl_default,     impl_tbl_default,     impl_tbl_default_opts },
	[obj_meson]       = { impl_tbl_meson,       impl_tbl_meson,       NULL,                 },
	[obj_subproject]  = { impl_tbl_subproject,  impl_tbl_subproject,  NULL,                 },
	[obj_number]      = { impl_tbl_number,      impl_tbl_number,      NULL,                 },
	[obj_dependency]  = { impl_tbl_dependency,  impl_tbl_dependency,  NULL,                 },
	[obj_machine]     = { impl_tbl_machine,     impl_tbl_machine,     NULL,                 },
	[obj_compiler]    = { impl_tbl_compiler,    impl_tbl_compiler,    NULL,                 },
	[obj_feature_opt] = { impl_tbl_feature_opt, impl_tbl_feature_opt, NULL,                 },
	[obj_external_program] = { impl_tbl_external_program, impl_tbl_external_program, NULL, },
	[obj_run_result]  = { impl_tbl_run_result,  impl_tbl_run_result,  NULL, },
	[obj_string]      = { impl_tbl_string,      impl_tbl_string,      NULL, },
	[obj_configuration_data] = { impl_tbl_configuration_data, impl_tbl_configuration_data, NULL, },
};

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

	/* L(log_misc, "calling %s.%s", obj_type_to_s(recvr_type), name); */

	name = get_node(wk->ast, name_node)->tok->dat.s;

	const struct func_impl_name *impl_tbl = func_tbl[recvr_type][wk->lang_mode];

	if (!impl_tbl) {
		interp_error(wk, name_node,  "method on %s not found", obj_type_to_s(recvr_type));
		return false;
	}

	uint32_t i;
	for (i = 0; impl_tbl[i].name; ++i) {
		if (strcmp(impl_tbl[i].name, name) == 0) {
			if (!impl_tbl[i].func(wk, rcvr_id, args_node, obj)) {
				if (recvr_type == obj_default) {
					interp_error(wk, name_node, "in builtin function %s",  name);
				} else {
					interp_error(wk, name_node, "in builtin function %s.%s", obj_type_to_s(recvr_type), name);
				}
				return false;
			}
			/* L(log_misc, "finished calling %s.%s", obj_type_to_s(recvr_type), name); */
			return true;
		}
	}

	interp_error(wk, name_node, "function not found: %s", name);
	return false;
}

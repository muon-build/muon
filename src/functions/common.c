#include "posix.h"

#include <string.h>

#include "functions/common.h"
#include "functions/compiler.h"
#include "functions/default.h"
#include "functions/meson.h"
#include "functions/number.h"
#include "functions/subproject.h"
#include "interpreter.h"
#include "log.h"

bool
check_lang(struct workspace *wk, uint32_t id)
{
	if (strcmp("c", wk_objstr(wk, id)) != 0) {
		LOG_W(log_interp, "only c is supported");
		return false;
	}

	return true;
}

static bool
next_arg(struct ast *ast, struct node **arg, const char **kw, struct node **args)
{
	if (!*args || (*args)->type == node_empty) {
		return false;
	}

	assert((*args)->type == node_argument);

	if ((*args)->data == arg_kwarg) {
		*kw = get_node(ast, (*args)->l)->tok->dat.s;
		*arg = get_node(ast, (*args)->r);
	} else {
		*kw = NULL;
		*arg = get_node(ast, (*args)->l);
	}

	/* L(log_interp, "got arg %s:%s", *kw, node_to_s(*arg)); */

	if ((*args)->chflg & node_child_c) {
		*args = get_node(ast, (*args)->c);
	} else {
		*args = NULL;
	}

	return true;
}

bool
interp_args(struct ast *ast, struct workspace *wk,
	struct node *args,
	struct args_norm positional_args[],
	struct args_norm optional_positional_args[],
	struct args_kw keyword_args[])
{
	const char *kw;
	struct node *arg;
	uint32_t i, stage;
	struct args_norm *an[2] = { positional_args, optional_positional_args };

	for (stage = 0; stage < 2; ++stage) {
		if (!an[stage]) {
			continue;
		}

		for (i = 0; an[stage][i].type != ARG_TYPE_NULL; ++i) {
			if (!next_arg(ast, &arg, &kw, &args)) {
				if (stage == 0) { // required
					LOG_W(log_interp, "missing positional arguments.  Only got %d", i);
					return false;
				} else if (stage == 1) { // optional
					return true;
				}
			}

			if (kw) {
				if (stage == 0) {
					LOG_W(log_interp, "unexpected kwarg: '%s'", kw);
					return false;
				}

				goto kwargs;
			}

			if (!interp_node(ast, wk, arg, &an[stage][i].val)) {
				return false;
			}

			if (!typecheck(get_obj(wk, an[stage][i].val), an[stage][i].type)) {
				LOG_W(log_interp, "%s arg %d", stage ? "optional" : "positional", i);
				return false;
			}

			an[stage][i].set = true;
		}
	}

	if (keyword_args) {
		while (next_arg(ast, &arg, &kw, &args)) {
			goto process_kwarg;
kwargs:
			if (!keyword_args) {
				LOG_W(log_interp, "this function does not accept kwargs");
				return false;
			}
process_kwarg:
			if (!kw) {
				LOG_W(log_interp, "non-kwarg after kwargs");
				return false;
			}

			for (i = 0; keyword_args[i].key; ++i) {
				if (strcmp(kw, keyword_args[i].key) == 0) {
					if (!interp_node(ast, wk, arg, &keyword_args[i].val)) {
						return false;
					}

					if (!typecheck(get_obj(wk, keyword_args[i].val), keyword_args[i].type)) {
						LOG_W(log_interp, "kwarg %s", keyword_args[i].key);
						return false;
					}

					keyword_args[i].set = true;
					break;
				}
			}

			if (!keyword_args[i].key) {
				LOG_W(log_interp, "invalid kwarg: '%s'", kw);
				return false;
			}
		}


	} else if (next_arg(ast, &arg, &kw, &args)) {
		if (positional_args || optional_positional_args) {
			LOG_W(log_interp, "this function does not accept kwargs");
		} else {
			LOG_W(log_interp, "this function does not accept args");
		}
		return false;
	}

	return true;
}

bool
todo(struct ast *ast, struct workspace *wk, uint32_t rcvr_id, struct node *args, uint32_t *obj)
{
	if (rcvr_id) {
		struct obj *rcvr = get_obj(wk, rcvr_id);
		LOG_W(log_misc, "method on %s not implemented", obj_type_to_s(rcvr->type));
	} else {
		LOG_W(log_misc, "function not implemented");
	}
	return false;
}

bool
builtin_run(struct ast *ast, struct workspace *wk, uint32_t rcvr_id, struct node *n, uint32_t *obj)
{
	const char *name;

	enum obj_type recvr_type;
	struct node *args;

	if (rcvr_id) {
		struct obj *rcvr = get_obj(wk, rcvr_id);
		name = get_node(ast, n->r)->tok->dat.s;
		args = get_node(ast, n->c);
		recvr_type = rcvr->type;
	} else {
		name = get_node(ast, n->l)->tok->dat.s;
		args = get_node(ast, n->r);
		recvr_type = obj_default;
	}

	/* L(log_misc, "calling %s.%s", obj_type_to_s(recvr_type), name); */

	if (recvr_type == obj_null) {
		LOG_W(log_misc, "tried to call %s on null", name);
		return false;
	}

	const struct func_impl_name *impl_tbl;

	switch (recvr_type) {
	case obj_default:
		impl_tbl = impl_tbl_default;
		break;
	case obj_meson:
		impl_tbl = impl_tbl_meson;
		break;
	case obj_compiler:
		impl_tbl = impl_tbl_compiler;
		break;
	case obj_subproject:
		impl_tbl = impl_tbl_subproject;
		break;
	case obj_number:
		impl_tbl = impl_tbl_number;
		break;
	default:
		LOG_W(log_misc, "reciever %s does not have any methods", obj_type_to_s(recvr_type));
		return false;
	}

	uint32_t i;
	for (i = 0; impl_tbl[i].name; ++i) {
		if (strcmp(impl_tbl[i].name, name) == 0) {
			if (!impl_tbl[i].func(ast, wk, rcvr_id, args, obj)) {
				LOG_W(log_interp, "error in %s(), %s", name, source_location(ast, n->l));
				return false;
			}
			/* L(log_misc, "finished calling %s.%s", obj_type_to_s(recvr_type), name); */
			return true;
		}
	}

	LOG_W(log_misc, "function not found: %s", name);
	return false;
}

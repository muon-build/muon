/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "functions/modules/getopt.h"
#include "lang/object_iterators.h"
#include "lang/typecheck.h"
#include "log.h"
#include "opts.h"
#include "platform/assert.h"
#include "platform/run_cmd.h"

struct getopt_handler {
	bool required;
	bool seen;
	obj action;
	const struct str *desc;
	char c;
	bool has_value;
};

static bool
getopt_handler_requires_optarg(struct workspace *wk, const struct getopt_handler *handler)
{
	const struct obj_capture *capture = get_obj_capture(wk, handler->action);
	return capture->func->nargs == 1;
}

static void
func_module_getopt_usage(struct workspace *wk, const char *argv0, obj handlers, int exitcode)
{
	obj handler;
	if (obj_dict_index_strn(wk, handlers, "h", 1, &handler)) {
		obj capture_res;
		obj action = 0;
		if (!vm_eval_capture(wk, action, 0, 0, &capture_res)) {
			exitcode = 1;
		}
	} else {
		printf("usage: %s [options]\n", argv0);
		printf("options:\n");

		obj k, v;
		obj_dict_for(wk, handlers, k, v) {
			struct getopt_handler handler = { 0 };
			vm_obj_to_struct(wk, getopt_handler, v, &handler);

			printf("  -%s%s - %s%s\n",
				get_cstr(wk, k),
				getopt_handler_requires_optarg(wk, &handler) ? " <value>" : "",
				handler.desc->s,
				handler.required ? " (required)" : "");
		}
		printf("  -h - show this message\n");
	}

	exit(exitcode);
}

FUNC_IMPL(module_getopt, getopt, tc_array, .desc = "Parse command line arguments using getopt.  Returns the array of trailing positional args." )
{
	struct args_norm an[] = {
		{ TYPE_TAG_LISTIFY | tc_string, .desc = "the array of arguments to parse" },
		{ make_complex_type(wk, complex_type_nested, tc_dict, tc_dict),
			.desc
			= "A dict of `opt` -> `handler`.\n\n"
			  "- `opt` must be a single character.\n"
			  "- `handler` is a dict that may contain the following keys:\n"
			  "\n"
			  "  - `required` - defaults to false, causes this option to be required\n"
			  "  - `action` - required, a function that will be called to handle this option\n"
			  "\n"
			  "    If the function accepts a single argument then the option will be required to supply a value\n"
			  "  - `desc` - required, a string to show in the help message.\n" },
		ARG_TYPE_NULL,
	};
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	if (vm_struct(wk, getopt_handler)) {
		vm_struct_member(wk, getopt_handler, required, vm_struct_type_bool);
		vm_struct_member(wk, getopt_handler, seen, vm_struct_type_bool);
		vm_struct_member(wk, getopt_handler, action, vm_struct_type_obj);
		vm_struct_member(wk, getopt_handler, desc, vm_struct_type_str);
	}

	obj handlers = an[1].val;
	struct arr unpacked_handlers;
	arr_init(wk->a_scratch, &unpacked_handlers, 16, struct getopt_handler);
	{
		obj k, v;
		obj_dict_for(wk, handlers, k, v) {
			const struct str *s = get_str(wk, k);
			if (s->len != 1) {
				vm_error(wk, "option %o invalid, must be a single character", k);
				return false;
			}

			struct getopt_handler handler = { 0 };
			if (!vm_obj_to_struct(wk, getopt_handler, v, &handler)) {
				LOG_E("option %s has an invalid handler", get_cstr(wk, k));
				return false;
			}

			if (!typecheck_custom(wk, 0, handler.action, tc_capture, 0)) {
				vm_error(wk, "action for %o is not a function", k);
				return false;
			}

			struct obj_capture *capture = get_obj_capture(wk, handler.action);
			if (capture->func->nkwargs) {
				vm_error(wk, "handler for %o must not accept kwargs", k);
				return false;
			} else if (capture->func->nargs > 1) {
				vm_error(wk, "handler for %o can only accept at most 1 posarg", k);
				return false;
			}

			if (getopt_handler_requires_optarg(wk, &handler)) {
				handler.has_value = true;
			}

			handler.c = *s->s;

			arr_push(wk->a_scratch, &unpacked_handlers, &handler);
		}
	}

	char *const *argv;
	uint32_t argc;
	{
		const char *joined;
		join_args_argstr(wk, &joined, &argc, an[0].val);
		argstr_to_argv(wk, joined, argc, 0, &argv);
	}

	uint32_t argi = 0;
	opt_for(-1) {
		for (uint32_t i = 0; i < unpacked_handlers.len; ++i) {
			struct getopt_handler *handler = arr_get(&unpacked_handlers, i);

			if (opt_match(handler->c, handler->desc->s, handler->has_value ? "value" : "")) {
				handler->seen = true;

				obj capture_res;
				struct args_norm capture_an[] = { { ARG_TYPE_NULL }, ARG_TYPE_NULL };
				if (handler->has_value) {
					capture_an[0].node = 0;
					capture_an[0].type = tc_string;
					capture_an[0].val = make_str(wk, opt_ctx.optarg);
				}

				if (!vm_eval_capture(wk, handler->action, capture_an, 0, &capture_res)) {
					return false;
				}
			}
		}
	}

#if 0
		{
			obj v;
			char opt_as_str[] = { opt, 0 };
			if (!obj_dict_index_strn(wk, handlers, opt_as_str, 1, &v)) {
				if (opt == '?' || opt == 'h') {
					func_module_getopt_usage(wk, argv[0], handlers, opt == '?' ? 1 : 0);
				}

				vm_error(wk, "no handler defined for -%s", opt_as_str);
				return false;
			}

			vm_obj_to_struct(wk, getopt_handler, v, &handler);

			if (handler.required) {
				obj_dict_set(wk, v, make_str(wk, "seen"), obj_bool_true);
			}
		}
#endif

	*res = make_obj(wk, obj_array);
	for (uint32_t i = *opt_ctx.argi; i < argc; ++i) {
		obj_array_push(wk, *res, make_str(wk, argv[i]));
	}

	for (uint32_t i = 0; i < unpacked_handlers.len; ++i) {
		const struct getopt_handler *handler = arr_get(&unpacked_handlers, i);
		if (handler->required && !handler->seen) {
			obj_lprintf(wk, log_info, "missing required option %c\n", handler->c);
			func_module_getopt_usage(wk, argv[0], handlers, 1);
		}
	}

	return true;
}

FUNC_REGISTER(module_getopt)
{
	if (lang_mode == language_internal) {
		FUNC_IMPL_REGISTER(module_getopt, getopt);
	}
}

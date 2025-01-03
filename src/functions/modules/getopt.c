/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "functions/modules/getopt.h"
#include "lang/object_iterators.h"
#include "lang/typecheck.h"
#include "platform/os.h"
#include "platform/run_cmd.h"

static void
func_module_getopt_usage(struct workspace *wk, const char *argv0, obj handlers, int exitcode)
{
	obj handler_arr;
	if (obj_dict_index_strn(wk, handlers, "h", 1, &handler_arr)) {
		obj handler, capture_res;
		obj_array_index(wk, handler_arr, 1, &handler);
		if (!vm_eval_capture(wk, handler, 0, 0, &capture_res)) {
			exitcode = 1;
		}
	} else {
		printf("usage: %s [options]\n", argv0);
		printf("options:\n");

		obj k, v, desc;
		obj_dict_for(wk, handlers, k, v) {
			obj_array_index(wk, v, 0, &desc);

			printf("  -%s - %s\n", get_cstr(wk, k), get_cstr(wk, desc));
		}
		printf("  -h - show this message\n");
	}

	exit(exitcode);
}

static bool
func_module_getopt_getopt(struct workspace *wk, obj self, obj *res)
{
	struct args_norm an[] = {
		{ TYPE_TAG_LISTIFY | tc_string },
		{ make_complex_type(wk, complex_type_nested, tc_dict, tc_array) },
		ARG_TYPE_NULL,
	};
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	obj handlers = an[1].val;
	char optstring[256] = { 0 };
	{
		const uint32_t optstring_max = sizeof(optstring) - 3;
		uint32_t optstring_i = 0;

		obj k, v, desc, handler;
		obj_dict_for(wk, handlers, k, v) {
			const struct str *s = get_str(wk, k);

			if (s->len != 1) {
				vm_error(wk, "option %o invalid, must be a single character", k);
				return false;
			} else if (get_obj_array(wk, v)->len != 2) {
				vm_error(wk, "handler for %o must be of length 2", k);
				return false;
			} else if (optstring_i >= optstring_max) {
				vm_error(wk, "too many options");
				return false;
			}

			obj_array_index(wk, v, 0, &desc);
			obj_array_index(wk, v, 1, &handler);

			if (!typecheck_custom(wk, 0, desc, tc_string, 0)) {
				vm_error(wk, "handler description for %o is not a string", k);
				return false;
			} else if (!typecheck_custom(wk, 0, handler, tc_capture, 0)) {
				vm_error(wk, "handler for %o is not a function", k);
				return false;
			}

			struct obj_capture *capture = get_obj_capture(wk, handler);
			if (capture->func->nkwargs) {
				vm_error(wk, "handler for %o must not accept kwargs", k);
				return false;
			} else if (capture->func->nargs > 1) {
				vm_error(wk, "handler for %o can only accept at most 1 posarg", k);
				return false;
			}

			optstring[optstring_i] = *s->s;
			++optstring_i;
			if (capture->func->nargs) {
				optstring[optstring_i] = ':';
				++optstring_i;
			}
		}

		if (!strchr(optstring, 'h')) {
			optstring[optstring_i] = 'h';
		}
	}

	char *const *argv;
	uint32_t argc;
	{
		const char *joined;
		join_args_argstr(wk, &joined, &argc, an[0].val);
		argstr_to_argv(joined, argc, 0, &argv);
	}

	signed char opt;
	opterr = 1;
	optind = 1;
	optarg = 0;
	while ((opt = os_getopt(argc, argv, optstring)) != -1) {
		obj handler = 0;
		{
			obj handler_arr;
			char opt_as_str[] = { opt, 0 };
			if (!obj_dict_index_strn(wk, handlers, opt_as_str, 1, &handler_arr)) {
				if (opt == '?' || opt == 'h') {
					func_module_getopt_usage(wk, argv[0], handlers, opt == '?' ? 1 : 0);
				}

				vm_error(wk, "no handler defined for -%s", opt_as_str);
				return false;
			}

			obj_array_index(wk, handler_arr, 1, &handler);
		}

		{
			obj capture_res;
			struct args_norm capture_an[] = { { ARG_TYPE_NULL }, ARG_TYPE_NULL };
			if (optarg) {
				capture_an[0].node = 0;
				capture_an[0].type = tc_string;
				capture_an[0].val = make_str(wk, optarg);
			}

			if (!vm_eval_capture(wk, handler, capture_an, 0, &capture_res)) {
				return false;
			}
		}

		optarg = 0;
	}

	make_obj(wk, res, obj_array);
	for (uint32_t i = optind; i < argc; ++i) {
		obj_array_push(wk, *res, make_str(wk, argv[i]));
	}

	return true;
}

const struct func_impl impl_tbl_module_getopt[] = {
	{ "getopt", func_module_getopt_getopt },
	{ NULL, NULL },
};

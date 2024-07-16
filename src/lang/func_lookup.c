/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "functions/array.h"
#include "functions/boolean.h"
#include "functions/both_libs.h"
#include "functions/build_target.h"
#include "functions/compiler.h"
#include "functions/configuration_data.h"
#include "functions/custom_target.h"
#include "functions/dependency.h"
#include "functions/dict.h"
#include "functions/disabler.h"
#include "functions/environment.h"
#include "functions/external_program.h"
#include "functions/feature_opt.h"
#include "functions/file.h"
#include "functions/generator.h"
#include "functions/kernel.h"
#include "functions/machine.h"
#include "functions/meson.h"
#include "functions/modules.h"
#include "functions/modules/python.h"
#include "functions/number.h"
#include "functions/run_result.h"
#include "functions/source_configuration.h"
#include "functions/source_set.h"
#include "functions/string.h"
#include "functions/subproject.h"
#include "lang/analyze.h"
#include "lang/func_lookup.h"
#include "lang/typecheck.h"
#include "log.h"

/******************************************************************************
 * function tables
 ******************************************************************************/

// clang-format off
struct func_impl_group func_impl_groups[obj_type_count][language_mode_count] = {
	[0]                        = { { impl_tbl_kernel },               { impl_tbl_kernel_internal },
				       { impl_tbl_kernel_opts }                                            },
	[obj_meson]                = { { impl_tbl_meson },                { impl_tbl_meson_internal }      },
	[obj_subproject]           = { { impl_tbl_subproject },           { 0 }                            },
	[obj_number]               = { { impl_tbl_number },               { impl_tbl_number, }             },
	[obj_dependency]           = { { impl_tbl_dependency },           { 0 }                            },
	[obj_machine]              = { { impl_tbl_machine },              { impl_tbl_machine }             },
	[obj_compiler]             = { { impl_tbl_compiler },             { impl_tbl_compiler_internal }   },
	[obj_feature_opt]          = { { impl_tbl_feature_opt },          { 0 }                            },
	[obj_run_result]           = { { impl_tbl_run_result },           { impl_tbl_run_result }          },
	[obj_string]               = { { impl_tbl_string },               { impl_tbl_string }              },
	[obj_dict]                 = { { impl_tbl_dict },                 { impl_tbl_dict_internal }       },
	[obj_external_program]     = { { impl_tbl_external_program },     { impl_tbl_external_program }    },
	[obj_python_installation]  = { { impl_tbl_python_installation },  { impl_tbl_python_installation } },
	[obj_configuration_data]   = { { impl_tbl_configuration_data },   { impl_tbl_configuration_data }  },
	[obj_custom_target]        = { { impl_tbl_custom_target },        { 0 }                            },
	[obj_file]                 = { { impl_tbl_file },                 { impl_tbl_file }                },
	[obj_bool]                 = { { impl_tbl_boolean },              { impl_tbl_boolean }             },
	[obj_array]                = { { impl_tbl_array },                { impl_tbl_array_internal }      },
	[obj_build_target]         = { { impl_tbl_build_target },         { 0 }                            },
	[obj_environment]          = { { impl_tbl_environment },          { impl_tbl_environment }         },
	[obj_disabler]             = { { impl_tbl_disabler },             { impl_tbl_disabler }            },
	[obj_generator]            = { { impl_tbl_generator },            { 0 }                            },
	[obj_both_libs]            = { { impl_tbl_both_libs },            { 0 }                            },
	[obj_source_set]           = { { impl_tbl_source_set },           { 0 }                            },
	[obj_source_configuration] = { { impl_tbl_source_configuration }, { 0 }                            },
	[obj_module]               = { { impl_tbl_module },               { impl_tbl_module  }             },
};
// clang-format on

struct func_impl native_funcs[512];

static void
copy_func_impl_group(struct func_impl_group *group, uint32_t *off)
{
	if (!group->impls) {
		return;
	}

	group->off = *off;
	for (group->len = 0; group->impls[group->len].name; ++group->len) {
		assert(group->off + group->len < ARRAY_LEN(native_funcs) && "bump native_funcs size");
		native_funcs[group->off + group->len] = group->impls[group->len];
	}
	*off += group->len;
}

void
build_func_impl_tables(void)
{
	uint32_t off = 0;
	enum module m;
	enum obj_type t;
	enum language_mode lang_mode;

	both_libs_build_impl_tbl();
	python_build_impl_tbl();

	for (t = 0; t < obj_type_count; ++t) {
		for (lang_mode = 0; lang_mode < language_mode_count; ++lang_mode) {
			copy_func_impl_group(&func_impl_groups[t][lang_mode], &off);
		}
	}

	for (m = 0; m < module_count; ++m) {
		for (lang_mode = 0; lang_mode < language_mode_count; ++lang_mode) {
			copy_func_impl_group(&module_func_impl_groups[m][lang_mode], &off);
		}
	}

	copy_func_impl_group(&az_func_impl_group, &off);
}

/******************************************************************************
 * function lookup
 ******************************************************************************/

static bool
func_lookup_for_mode(const struct func_impl_group *impl_group, const char *name, uint32_t *idx)
{
	if (!impl_group->impls) {
		return false;
	}

	uint32_t i;
	for (i = 0; impl_group->impls[i].name; ++i) {
		if (strcmp(impl_group->impls[i].name, name) == 0) {
			*idx = impl_group->off + i;
			return true;
		}
	}

	return false;
}

bool
func_lookup_for_group(const struct func_impl_group impl_group[],
	enum language_mode mode,
	const char *name,
	uint32_t *idx)
{
	if (mode == language_extended) {
		if (func_lookup_for_mode(&impl_group[language_internal], name, idx)) {
			return true;
		}

		return func_lookup_for_mode(&impl_group[language_external], name, idx);
	} else {
		return func_lookup_for_mode(&impl_group[mode], name, idx);
	}

	return false;
}

const char *
func_name_str(enum obj_type t, const char *name)
{
	static char buf[256];
	if (t) {
		snprintf(buf, ARRAY_LEN(buf), "method %s.%s()", obj_type_to_s(t), name);
	} else {
		snprintf(buf, ARRAY_LEN(buf), "function %s()", name);
	}

	return buf;
}

bool
func_lookup(struct workspace *wk, obj self, const char *name, uint32_t *idx, obj *func)
{
	enum obj_type t;
	struct func_impl_group *impl_group;
	struct obj_module *m;

	t = get_obj_type(wk, self);
	impl_group = func_impl_groups[t];

	if (t == obj_module) {
		if (func_lookup_for_group(impl_group, wk->vm.lang_mode, name, idx)) {
			return true;
		}

		m = get_obj_module(wk, self);

		if (!m->found && strcmp(name, "found") != 0) {
			vm_error(wk, "module %s was not found", module_info[m->module].name);
			return false;
		}

		if (m->exports) {
			if (!obj_dict_index_str(wk, m->exports, name, func)) {
				vm_error(wk, "%s not found in module", name);
				return false;
			}
			return true;
		}

		if (!module_func_lookup(wk, name, m->module, idx)) {
			if (!m->has_impl) {
				vm_error(wk,
					"module '%s' is unimplemented,\n"
					"  If you would like to make your build files portable to muon, use"
					" `import('%s', required: false)`, and then check"
					" the .found() method before use.",
					module_info[m->module].name,
					module_info[m->module].name);
				return false;
			} else {
				vm_error(wk,
					"%s not found in module %s",
					func_name_str(0, name),
					module_info[m->module].name);
				return false;
			}
		}
		return true;
	}

	return func_lookup_for_group(impl_group, wk->vm.lang_mode, name, idx);
}

/******************************************************************************
 * function signature dumping
 ******************************************************************************/

struct function_signature {
	const char *name, *posargs, *varargs, *optargs, *kwargs, *returns;
	bool is_method;

	const struct func_impl *impl;
};

struct {
	struct arr sigs;
} function_sig_dump;

static const char *
dump_type(struct workspace *wk, type_tag type)
{
	obj types = typechecking_type_to_arr(wk, type);
	obj typestr, sep = make_str(wk, "|");
	obj_array_join(wk, false, types, sep, &typestr);

	if (type & TYPE_TAG_LISTIFY) {
		obj_array_push(wk, types, make_strf(wk, "list[%s]", get_cstr(wk, typestr)));
		obj sorted;
		obj_array_sort(wk, NULL, types, obj_array_sort_by_str, &sorted);
		obj_array_join(wk, false, sorted, sep, &typestr);
	}

	return get_cstr(wk, typestr);
}

static int32_t
arr_sort_by_string(const void *a, const void *b, void *_ctx)
{
	return strcmp(*(const char **)a, *(const char **)b);
}

void
dump_function_signature(struct workspace *wk, struct args_norm posargs[], struct args_kw kwargs[])
{
	uint32_t i;

	struct function_signature *sig = arr_get(&function_sig_dump.sigs, function_sig_dump.sigs.len - 1);

	obj s, opt_s = 0;
	if (posargs) {
		s = make_str(wk, "");
		for (i = 0; posargs[i].type != ARG_TYPE_NULL; ++i) {
			if (posargs[i].type & TYPE_TAG_GLOB) {
				sig->varargs = get_cstr(wk, make_strf(wk, "    %s\n", dump_type(wk, posargs[i].type)));
				continue;
			}

			if (posargs[i].optional) {
				if (!opt_s) {
					opt_s = make_str(wk, "");
				}

				str_appf(wk, &opt_s, "    %s\n", dump_type(wk, posargs[i].type));
			} else {
				str_appf(wk, &s, "    %s\n", dump_type(wk, posargs[i].type));
			}
		}

		const char *ts = get_cstr(wk, s);
		if (*ts) {
			sig->posargs = ts;
		}
	}

	if (opt_s) {
		sig->optargs = get_cstr(wk, opt_s);
	}

	if (kwargs) {
		struct arr kwargs_list;
		arr_init(&kwargs_list, 8, sizeof(char *));

		for (i = 0; kwargs[i].key; ++i) {
			const char *v = get_cstr(
				wk, make_strf(wk, "    %s: %s\n", kwargs[i].key, dump_type(wk, kwargs[i].type)));
			arr_push(&kwargs_list, &v);
		}

		arr_sort(&kwargs_list, NULL, arr_sort_by_string);

		s = make_str(wk, "");
		for (i = 0; i < kwargs_list.len; ++i) {
			str_app(wk, &s, *(const char **)arr_get(&kwargs_list, i));
		}
		sig->kwargs = get_cstr(wk, s);

		arr_destroy(&kwargs_list);
	}
}

static int32_t
function_sig_sort(const void *a, const void *b, void *_ctx)
{
	const struct function_signature *sa = a, *sb = b;

	if ((sa->is_method && sb->is_method) || (!sa->is_method && !sb->is_method)) {
		return strcmp(sa->name, sb->name);
	} else if (sa->is_method) {
		return 1;
	} else {
		return -1;
	}
}

void
dump_function_signatures(struct workspace *wk)
{
	wk->vm.dbg_state.dump_signature = true;

	arr_init(&function_sig_dump.sigs, 64, sizeof(struct function_signature));
	struct function_signature *sig, empty = { 0 };
	struct func_impl_group *g;

	uint32_t i;
	{
		enum obj_type t;
		for (t = 0; t < obj_type_count; ++t) {
			g = &func_impl_groups[t][wk->vm.lang_mode];
			if (!g->impls) {
				continue;
			}

			for (i = 0; g->impls[i].name; ++i) {
				sig = arr_get(&function_sig_dump.sigs, arr_push(&function_sig_dump.sigs, &empty));
				sig->impl = &g->impls[i];
				sig->is_method = t != 0;
				sig->name = get_cstr(wk,
					make_strf(wk,
						"%s%s%s",
						t ? obj_type_to_s(t) : "",
						t ? "." : "",
						g->impls[i].name));
				sig->returns = typechecking_type_to_s(wk, g->impls[i].return_type);
				g->impls[i].func(wk, 0, 0);
			}
		}
	}

	for (i = 0; i < module_count; ++i) {
		g = &module_func_impl_groups[i][wk->vm.lang_mode];
		if (!g->impls) {
			continue;
		}

		uint32_t j;
		for (j = 0; g->impls[j].name; ++j) {
			sig = arr_get(&function_sig_dump.sigs, arr_push(&function_sig_dump.sigs, &empty));
			sig->impl = &g->impls[j];
			sig->is_method = true;
			sig->name
				= get_cstr(wk, make_strf(wk, "import('%s').%s", module_info[i].name, g->impls[j].name));
			sig->returns = typechecking_type_to_s(wk, g->impls[j].return_type);
			g->impls[j].func(wk, 0, 0);
		}
	}

	arr_sort(&function_sig_dump.sigs, NULL, function_sig_sort);

	for (i = 0; i < function_sig_dump.sigs.len; ++i) {
		sig = arr_get(&function_sig_dump.sigs, i);

		if (sig->impl->extension) {
			printf("extension:");
		}

		printf("%s\n", sig->name);
		if (sig->posargs) {
			printf("  posargs:\n%s", sig->posargs);
		}
		if (sig->varargs) {
			printf("  varargs:\n%s", sig->varargs);
		}
		if (sig->optargs) {
			printf("  optargs:\n%s", sig->optargs);
		}
		if (sig->kwargs) {
			printf("  kwargs:\n%s", sig->kwargs);
		}
		printf("  returns:\n    %s\n", sig->returns);
	}

	arr_destroy(&function_sig_dump.sigs);
}

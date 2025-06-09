/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "buf_size.h"
#include "embedded.h"
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
#include "lang/object_iterators.h"
#include "lang/typecheck.h"
#include "platform/assert.h"
#include "version.h"

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
	[obj_machine]              = { { impl_tbl_machine },              { impl_tbl_machine_internal }    },
	[obj_compiler]             = { { impl_tbl_compiler },             { impl_tbl_compiler_internal }   },
	[obj_feature_opt]          = { { impl_tbl_feature_opt },          { 0 }                            },
	[obj_run_result]           = { { impl_tbl_run_result },           { impl_tbl_run_result }          },
	[obj_string]               = { { impl_tbl_string },               { impl_tbl_string_internal }     },
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
	if (func) {
		*func = 0;
	}

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
			if (!wk->vm.in_analyzer) {
				vm_error(wk, "module %s was not found", module_info[m->module].name);
			}
			return false;
		}

		if (m->exports) {
			if (!obj_dict_index_str(wk, m->exports, name, func)) {
				return false;
			}

			if (!typecheck(wk, 0, *func, tc_capture)) {
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
 * dynamic kwarg handling
 ******************************************************************************/

static void
kwargs_arr_push_sentinel(struct workspace *wk, struct arr *arr)
{
	arr_push(arr, &(struct args_kw) { 0 });
}

static void
kwargs_arr_init(struct workspace *wk, struct arr *arr)
{
	arr_init(arr, 8, sizeof(struct args_kw));
	kwargs_arr_push_sentinel(wk, arr);
}

void
kwargs_arr_destroy(struct workspace *wk, struct arr *arr)
{
	arr_destroy(arr);
}

void
kwargs_arr_push(struct workspace *wk, struct arr *arr, const struct args_kw *kw)
{
	*(struct args_kw *)arr_get(arr, arr->len - 1) = *kw;
	kwargs_arr_push_sentinel(wk, arr);
}

static uint32_t
kwargs_arr_index(struct workspace *wk, struct arr *arr, const char *name)
{
	uint32_t i;
	for (i = 0; i < arr->len; ++i) {
		struct args_kw *kw = arr_get(arr, i);
		if (strcmp(kw->key, name) == 0) {
			return i;
		}
	}

	UNREACHABLE;
}

void
kwargs_arr_del(struct workspace *wk, struct arr *arr, const char *name)
{
	arr_del(arr, arr->len - 1);
	arr_del(arr, kwargs_arr_index(wk, arr, name));
	kwargs_arr_push_sentinel(wk, arr);
}

struct args_kw *
kwargs_arr_get(struct workspace *wk, struct arr *arr, const char *name)
{
	return arr_get(arr, kwargs_arr_index(wk, arr, name));
}

static struct func_kwargs_lookup_ctx {
	struct arr *kwargs;
} func_kwargs_lookup_ctx  = { 0 };

static bool
func_kwargs_lookup_cb(struct workspace *wk, struct args_norm posargs[], struct args_kw kwargs[])
{
	if (!kwargs) {
		return false;
	}

	uint32_t i;
	for (i = 0; kwargs[i].key; ++i) {
		kwargs_arr_push(wk, func_kwargs_lookup_ctx.kwargs, &kwargs[i]);
	}

	return false;
}

void
func_kwargs_lookup(struct workspace *wk, obj self, const char *name, struct arr *kwargs_arr)
{
	uint32_t idx;
	{
		obj _func;
		bool ok;

		stack_push(&wk->stack, wk->vm.lang_mode, language_external);
		ok = func_lookup(wk, self, name, &idx, &_func);
		stack_pop(&wk->stack, wk->vm.lang_mode);

		assert(ok && "function not found");
		assert(!_func && "only native functions supported");
	}

	kwargs_arr_init(wk, kwargs_arr);

	func_kwargs_lookup_ctx.kwargs = kwargs_arr;

	stack_push(&wk->stack, wk->vm.behavior.pop_args, func_kwargs_lookup_cb);
	native_funcs[idx].func(wk, 0, 0);
	stack_pop(&wk->stack, wk->vm.behavior.pop_args);
}

/******************************************************************************
 * function signature dumping
 ******************************************************************************/

struct function_signature {
	const char *name, *posargs, *varargs, *optargs, *kwargs, *returns, *description;
	bool is_method;

	const struct func_impl *impl;
};

struct {
	struct arr sigs;
	obj func;
	struct meson_doc_entry_func *meson_doc_entry;

	enum {
		function_sig_dump_special_func_none,
		function_sig_dump_special_func_build_tgt,
	} special_func;
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

static bool
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

	return false;
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

static void
dump_function_signatures_prepare(struct workspace *wk)
{
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
}

void
dump_function_signatures(struct workspace *wk)
{
	wk->vm.behavior.pop_args = dump_function_signature;

	dump_function_signatures_prepare(wk);

	uint32_t i;
	struct function_signature *sig;
	for (i = 0; i < function_sig_dump.sigs.len; ++i) {
		sig = arr_get(&function_sig_dump.sigs, i);

		if (sig->impl->flags & func_impl_flag_extension) {
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

/******************************************************************************
 * docs generation
 ******************************************************************************/

struct meson_doc_entry_common {
	const char *name, *description, *type;
};

struct meson_doc_entry_func {
	struct meson_doc_entry_common common;
	uint32_t posargs_start, posargs_len;
	uint32_t kwargs_start, kwargs_len;
};

struct meson_doc_entry_arg {
	struct meson_doc_entry_common common;
	bool optional, glob;
};

#ifdef HAVE_MESON_DOCS_H
#include "meson_docs.h"
#else
struct meson_doc_entry_func *meson_doc_root[obj_type_count] = { 0 };
struct meson_doc_entry_arg meson_doc_posargs[] = { 0 };
struct meson_doc_entry_arg meson_doc_kwargs[] = { 0 };
#endif

struct meson_doc_entry_arg *
meson_doc_lookup_function_kwarg(const struct meson_doc_entry_func *meson_doc_entry, const char *key)
{
	struct meson_doc_entry_arg *args = &meson_doc_kwargs[meson_doc_entry->kwargs_start];
	uint32_t i;
	for (i = 0; i < meson_doc_entry->kwargs_len; ++i) {
		if (strcmp(args[i].common.name, key) == 0) {
			return &args[i];
		}
	}

	return 0;
}

struct meson_doc_entry_func *
meson_doc_lookup_function(enum obj_type t, const char *name)
{
	uint32_t j;
	for (j = 0; meson_doc_root[t][j].common.name; ++j) {
		if (strcmp(meson_doc_root[t][j].common.name, name) == 0) {
			return &meson_doc_root[t][j];
		}
	}

	return 0;
}

static obj
dump_function_arg(struct workspace *wk, struct args_norm *an, uint32_t an_idx, struct args_kw *kw)
{
	obj dict;
	dict = make_obj(wk, obj_dict);

	obj name = 0;
	const char *desc = 0;
	bool extension = false;
	if (an) {
		if (an->desc) {
			desc = an->desc;
		} else if (function_sig_dump.meson_doc_entry) {
			if (an_idx >= function_sig_dump.meson_doc_entry->posargs_len) {
				LOG_W("missing documentation for %s posarg %d",
					function_sig_dump.meson_doc_entry->common.name,
					an_idx);
			} else {
				struct meson_doc_entry_arg *arg
					= &meson_doc_posargs[function_sig_dump.meson_doc_entry->posargs_start + an_idx];
				desc = arg->common.description;
			}
		}

		name = make_strf(wk, "%d", an_idx);
	} else {
		if (kw->desc) {
			desc = kw->desc;
		} else if (function_sig_dump.meson_doc_entry) {
			struct meson_doc_entry_arg *arg;

			arg = meson_doc_lookup_function_kwarg(function_sig_dump.meson_doc_entry, kw->key);

			if (!arg && function_sig_dump.special_func == function_sig_dump_special_func_build_tgt) {
				arg = meson_doc_lookup_function_kwarg(
					meson_doc_lookup_function(obj_null, "build_target"), kw->key);
			}

			if (arg) {
				desc = arg->common.description;
			} else {
				LOG_W("missing documentation for %s kwarg %s",
					function_sig_dump.meson_doc_entry->common.name,
					kw->key);
			}
		}

		name = make_str(wk, kw->key);
		extension = kw->extension;
	}

	obj_dict_set(wk, dict, make_str(wk, "name"), name);
	obj_dict_set(wk, dict, make_str(wk, "type"), typechecking_type_to_str(wk, an ? an->type : kw->type));
	if (desc) {
		obj_dict_set(wk, dict, make_str(wk, "desc"), make_str(wk, desc));
	}

	if (extension) {
		obj_dict_set(wk, dict, make_str(wk, "extension"), obj_bool_true);
	}

	return dict;
}

static bool
dump_function_args_cb(struct workspace *wk, struct args_norm posargs[], struct args_kw kwargs[])
{
	uint32_t i;

	obj res = function_sig_dump.func;

	if (posargs) {
		obj arr;
		arr = make_obj(wk, obj_array);

		for (i = 0; posargs[i].type != ARG_TYPE_NULL; ++i) {
			obj_array_push(wk, arr, dump_function_arg(wk, &posargs[i], i, 0));
		}

		obj_dict_set(wk, res, make_str(wk, "posargs"), arr);
	}

	if (kwargs) {
		obj arr;
		arr = make_obj(wk, obj_array);

		struct arr kwargs_list;
		struct kwargs_list_elem {
			const char *key;
			uint32_t i;
		};
		arr_init(&kwargs_list, 8, sizeof(struct kwargs_list_elem));

		for (i = 0; kwargs[i].key; ++i) {
			arr_push(&kwargs_list,
				&(struct kwargs_list_elem){
					.key = kwargs[i].key,
					.i = i,
				});
		}

		arr_sort(&kwargs_list, NULL, arr_sort_by_string);

		obj dict;
		dict = make_obj(wk, obj_dict);

		for (i = 0; i < kwargs_list.len; ++i) {
			struct kwargs_list_elem *elem = arr_get(&kwargs_list, i);
			struct args_kw kwarg = kwargs[elem->i];
			char buf[256];

			// This is a hack to convert e.g. cpp_args to <lang>_args
			if (function_sig_dump.special_func == function_sig_dump_special_func_build_tgt) {
#define E(lang, s)              \
	{                       \
		#lang #s, #lang \
	}
#define x(lang) E(lang, _args), E(lang, _static_args), E(lang, _shared_args), E(lang, _pch),
				const struct {
					const char *kw, *lang;
				} lang_kws[] = { FOREACH_COMPILER_EXPOSED_LANGUAGE(x) };
#undef x
#undef E

				bool found = false;
				uint32_t i;
				for (i = 0; i < ARRAY_LEN(lang_kws); ++i) {
					if (strcmp(kwarg.key, lang_kws[i].kw) == 0) {
						found = true;
						break;
					}
				}

				if (found) {
					snprintf(buf, sizeof(buf), "<lang>%s", kwarg.key + strlen(lang_kws[i].lang));
					kwarg.key = buf;
				}
			}

			obj _res;
			if (obj_dict_index_str(wk, dict, kwarg.key, &_res)) {
				continue;
			}

			obj_array_push(wk, arr, dump_function_arg(wk, 0, 0, &kwarg));
			obj_dict_set(wk, dict, make_str(wk, kwarg.key), obj_bool_true);
		}
		arr_destroy(&kwargs_list);

		obj_dict_set(wk, res, make_str(wk, "kwargs"), arr);
	}

	return false;
}

struct dump_function_opts {
	const char *module;
	const struct func_impl *impl;
	obj capture;
	enum obj_type rcvr_t;
	bool module_func;
};

static obj
dump_function(struct workspace *wk, struct dump_function_opts *opts)
{
	function_sig_dump.func = make_obj(wk, obj_dict);
	obj res = function_sig_dump.func;

	obj_dict_set(wk, res, make_str(wk, "name"), make_str(wk, opts->impl->name));
	if (opts->module_func) {
		obj_dict_set(wk, res, make_str(wk, "module"), make_str(wk, opts->module));
	} else if (opts->rcvr_t) {
		obj_dict_set(wk, res, make_str(wk, "rcvr"), make_str(wk, obj_type_to_s(opts->rcvr_t)));
	}
	obj_dict_set(wk, res, make_str(wk, "type"), typechecking_type_to_str(wk, opts->impl->return_type));

	const char *desc = 0;
	if (opts->impl->desc) {
		desc = opts->impl->desc;
	} else if (function_sig_dump.meson_doc_entry) {
		desc = function_sig_dump.meson_doc_entry->common.description;
	}

	if (desc) {
		obj_dict_set(wk, res, make_str(wk, "desc"), make_str(wk, desc));
	} else {
		LOG_W("missing documentation for %s.%s",
			opts->module_func ? opts->module : obj_type_to_s(opts->rcvr_t),
			opts->impl->name);
	}

	if (opts->impl->flags & func_impl_flag_extension) {
		obj_dict_set(wk, res, make_str(wk, "extension"), obj_bool_true);
	}

	stack_push(&wk->stack, wk->vm.behavior.pop_args, dump_function_args_cb);
	if (opts->impl->func) {
		opts->impl->func(wk, 0, 0);
	} else {
		obj _res;
		vm_eval_capture(wk, opts->capture, 0, 0, &_res);
	}
	stack_pop(&wk->stack, wk->vm.behavior.pop_args);

	return res;
}

obj
dump_function_native(struct workspace *wk, enum obj_type t, const struct func_impl *impl)
{
	if (meson_doc_root[t] && wk->vm.lang_mode == language_external) {
		function_sig_dump.meson_doc_entry = meson_doc_lookup_function(t, impl->name);
	}

	if (strcmp(impl->name, "executable") || strcmp(impl->name, "build_target")
		|| strcmp(impl->name, "shared_library") || strcmp(impl->name, "static_library")
		|| strcmp(impl->name, "both_libraries")) {
		function_sig_dump.special_func = function_sig_dump_special_func_build_tgt;
	}

	obj res = dump_function(wk,
		&(struct dump_function_opts){
			.rcvr_t = t,
			.impl = impl,
		});

	function_sig_dump.meson_doc_entry = 0;
	function_sig_dump.special_func = function_sig_dump_special_func_none;

	return res;
}

obj
dump_module_function_native(struct workspace *wk, enum module module, const struct func_impl *impl)
{
	return dump_function(wk,
		&(struct dump_function_opts){
			.module_func = true,
			.module = module_info[module].path,
			.impl = impl,
		});
}

obj
dump_module_function_capture(struct workspace *wk, const char *module, obj name, obj o)
{
	struct obj_capture *capture = get_obj_capture(wk, o);

	struct func_impl impl = {
		.name = get_cstr(wk, name),
		.desc = capture->func->desc,
		.return_type = capture->func->return_type,
	};

	return dump_function(wk,
		&(struct dump_function_opts){
			.module_func = true,
			.module = module,
			.impl = &impl,
			.capture = o,
		});
}

static void
dump_function_docs_json(struct workspace *wk, struct tstr *sb)
{
	obj doc;
	doc = make_obj(wk, obj_array);

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
				obj_array_push(wk, doc, dump_function_native(wk, t, &g->impls[i]));
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
			obj_array_push(wk, doc, dump_module_function_native(wk, i, &g->impls[j]));
		}
	}

	// get docs for script modules.
	if (wk->vm.lang_mode == language_external) {
		uint32_t i, embedded_len;
		const struct embedded_file *files = embedded_file_list(&embedded_len);
		const struct str *prefix = &STR("modules/"), *str;

		for (i = 0; i < embedded_len; ++i) {
			str = &STRL(files[i].name);
			if (!str_startswith(str, prefix)) {
				continue;
			} else if (str_eql(str, &STR("modules/_test.meson"))) {
				continue;
			}

			TSTR(mod_name);
			tstr_pushs(wk, &mod_name, files[i].name + prefix->len);
			*strchr(mod_name.buf, '.') = 0;

			struct obj_module *m;
			{
				obj mod;
				if (!module_import(wk, mod_name.buf, true, &mod)) {
					UNREACHABLE;
				}

				m = get_obj_module(wk, mod);
				assert(m->found);
			}

			TSTR(mod_path);
			tstr_pushf(wk, &mod_path, "public/%s", mod_name.buf);

			obj k, v;
			obj_dict_for(wk, m->exports, k, v) {
				obj_array_push(wk, doc, dump_module_function_capture(wk, mod_path.buf, k, v));
			}
		}
	}

	if (!obj_to_json(wk, doc, sb)) {
		UNREACHABLE;
	}
}

void
dump_function_docs(struct workspace *wk)
{
	TSTR(docs_external);
	TSTR(docs_internal);
	dump_function_docs_json(wk, &docs_external);
	wk->vm.lang_mode = language_internal;
	dump_function_docs_json(wk, &docs_internal);

	struct source src;
	if (!embedded_get("html/docs.html", &src)) {
		UNREACHABLE;
	}

	char version_json[512];
	snprintf(version_json,
		sizeof(version_json),
		"{\"version\": \"%s\", \"vcs_tag\": \"%s\", \"meson_compat\": \"%s\" }",
		muon_version.version,
		muon_version.vcs_tag,
		muon_version.meson_compat);

	fprintf(stdout, src.src, docs_external.buf, docs_internal.buf, version_json);
	/* LOG_I("wrote html output to %s", abs.buf); */
}

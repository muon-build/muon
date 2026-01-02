/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "buf_size.h"
#include "embedded.h"
#include "functions/array.h"
#include "functions/bool.h"
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
#include "functions/include_directory.h"
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
#include "platform/path.h"

/******************************************************************************
 * function tables
 ******************************************************************************/

// Every native function gets copied into this array so the vm can refer to
// functions by index.
//
// TODO: There is currently a lot of duplication, maybe that could be cleaned
// up?
struct func_impl native_funcs[768];

struct func_impl_group func_impl_groups[obj_type_count][language_mode_count] = { 0 };

static const func_impl_register_proto func_impl_register_funcs[obj_type_count] = {
	[obj_null] = func_impl_register_kernel,
	[obj_disabler] = func_impl_register_disabler,
	[obj_meson] = func_impl_register_meson,
	[obj_module] = func_impl_register_module,
	[obj_bool] = func_impl_register_bool,
	[obj_file] = func_impl_register_file,
	[obj_feature_opt] = func_impl_register_feature_opt,
	[obj_machine] = func_impl_register_machine,
	[obj_number] = func_impl_register_number,
	[obj_string] = func_impl_register_string,
	[obj_array] = func_impl_register_array,
	[obj_dict] = func_impl_register_dict,
	[obj_compiler] = func_impl_register_compiler,
	[obj_build_target] = func_impl_register_build_target,
	[obj_custom_target] = func_impl_register_custom_target,
	[obj_subproject] = func_impl_register_subproject,
	[obj_dependency] = func_impl_register_dependency,
	[obj_external_program] = func_impl_register_external_program,
	[obj_python_installation] = func_impl_register_python_installation,
	[obj_run_result] = func_impl_register_run_result,
	[obj_configuration_data] = func_impl_register_configuration_data,
	[obj_environment] = func_impl_register_environment,
	[obj_include_directory] = func_impl_register_include_directory,
	[obj_generator] = func_impl_register_generator,
	[obj_both_libs] = func_impl_register_both_libs,
	[obj_source_set] = func_impl_register_source_set,
	[obj_source_configuration] = func_impl_register_source_configuration,
};

void
func_impl_register(FUNC_IMPL_REGISTER_ARGS, const struct func_impl *src, const char *alias)
{
	assert(*added < cap);
	struct func_impl *impl = &dest[*added];
	*impl = *src;

	if (alias) {
		impl->name = alias;
	}

	if (impl->deferred_return_type) {
		const struct str drt = STRL(impl->deferred_return_type);
		if (str_startswith(&drt, &STR("enum "))) {
			impl->return_type = complex_type_enum_get_(wk, impl->deferred_return_type);
		} else {
			UNREACHABLE;
		}
	}

	++*added;
}

void
func_impl_register_inherit(func_impl_register_proto reg,
	func_impl_self_transform self_transform,
	FUNC_IMPL_REGISTER_ARGS)
{
	uint32_t i, len, base = *added;

	reg(FUNC_IMPL_REGISTER_ARGS_FWD);

	len = *added - base;
	for (i = 0; i < len; ++i) {
		dest[base + i].self_transform = self_transform;
	}
}

static void
copy_func_impl_group(
	struct workspace *wk,
	struct func_impl_group *group,
	uint32_t *off,
	enum language_mode lang_mode,
	func_impl_register_proto reg)
{
	uint32_t len = 0;
	reg(wk, lang_mode, native_funcs + *off, ARRAY_LEN(native_funcs) - *off, &len);

	*group = (struct func_impl_group){
		.impls = &native_funcs[*off],
		.off = *off,
		.len = len,
	};
	*off += len;
}

void
build_func_impl_tables(struct workspace *wk)
{
	uint32_t off = 0;
	enum module m;
	enum obj_type t;
	enum language_mode lang_mode;

	// Only kernel registers functions
	copy_func_impl_group(wk, &func_impl_groups[0][language_opts], &off, language_opts, func_impl_register_funcs[0]);

	for (t = 0; t < obj_type_count; ++t) {
		if (func_impl_register_funcs[t]) {
			for (lang_mode = 0; lang_mode <= language_internal; ++lang_mode) {
				copy_func_impl_group(wk,
					&func_impl_groups[t][lang_mode], &off, lang_mode, func_impl_register_funcs[t]);
			}
		}
	}

	for (m = 0; m < module_count; ++m) {
		if (func_impl_register_module_funcs[m]) {
			for (lang_mode = 0; lang_mode <= language_internal; ++lang_mode) {
				copy_func_impl_group(wk,
					&module_func_impl_groups[m][lang_mode], &off, lang_mode, func_impl_register_module_funcs[m]);
			}
		}
	}

	copy_func_impl_group(wk, &az_func_impl_group, &off, language_external, func_impl_register_analyzer);
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
	for (i = 0; i < impl_group->len; ++i) {
		if (strcmp(impl_group->impls[i].name, name) == 0) {
			*idx = impl_group->off + i;
			return true;
		}
	}

	return false;
}

const struct func_impl_group *
func_lookup_group(enum obj_type t)
{
	return func_impl_groups[t];
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
	arr_push(wk->a_scratch, arr, &(struct args_kw){ 0 });
}

void
kwargs_arr_init(struct workspace *wk, struct arr *arr)
{
	arr_init(wk->a_scratch, arr, 8, struct args_kw);
	kwargs_arr_push_sentinel(wk, arr);
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

	UNREACHABLE_RETURN;
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
} func_kwargs_lookup_ctx = { 0 };

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

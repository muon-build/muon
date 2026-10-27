/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <stddef.h>

#include "buf_size.h"
#include "functions/kernel/options.h"
#include "functions/modules/toolchain.h"
#include "lang/analyze.h"
#include "lang/object_iterators.h"
#include "lang/typecheck.h"
#include "log.h"
#include "options.h"
#include "toolchains.h"

FUNC_IMPL(module_toolchain,
	create,
	tc_compiler,
	.desc
	= "Creates a new compiler object that can be passed in to the `toolchain` keyword on `add_languages` or inherited from when creating a new toolchain.  The toolchain object is reffered to as a `compiler` for historical reasons, although it also contains other information required to compile programs such as linker metadata.")
{
	enum kwargs {
		kw_inherit,
		kw_inherit_compiler,
		kw_inherit_linker,
		kw_inherit_archiver,
		kw_inherit_static_linker,
	};
	struct args_kw akw[] = {
		[kw_inherit] = { "inherit", tc_compiler, .desc = "A toolchain to inherit from" },
		[kw_inherit_compiler] = { "inherit_compiler",
			tc_string | tc_compiler,
			.desc
			= "The compiler component to inherit from.  Can be either a compiler object or compiler type name." },
		[kw_inherit_linker] = { "inherit_linker",
			tc_string | tc_compiler,
			.desc
			= "The linker component to inherit from.  Can be either a compiler object or linker type name." },
		[kw_inherit_archiver] = { "inherit_archiver",
			tc_string | tc_compiler,
			.desc
			= "The archiver component to inherit from.  Can be either a compiler object or static linker type name." },
		// TODO: tag keywords as deprecated?
		[kw_inherit_static_linker] = { "inherit_static_linker",
			tc_string | tc_compiler,
			.desc = "Deprecated.  Please use inherit_archiver instead" },
		0,
	};

	if (!pop_args(wk, NULL, akw)) {
		return false;
	}

	if (!akw[kw_inherit_archiver].set && akw[kw_inherit_static_linker].set) {
		akw[kw_inherit_archiver].set = true;
		akw[kw_inherit_archiver].val = akw[kw_inherit_static_linker].val;
		akw[kw_inherit_archiver].node = akw[kw_inherit_static_linker].node;

		vm_deprecation_at(wk, akw[kw_inherit_static_linker].node, "0.6.0", "static_linker has been renamed to archiver");
	}

	if (wk->vm.in_analyzer) {
		*res = make_typeinfo(wk, tc_compiler);
		return true;
	}

	*res = make_obj(wk, obj_compiler);
	struct obj_compiler *c = get_obj_compiler(wk, *res);
	c->ver[toolchain_component_compiler] = make_str(wk, "unknown");
	c->libdirs = make_obj(wk, obj_array);

	struct {
		const char *old, *new;
		enum toolchain_component component;
	} deprecated_name_map[] = {
		{ "clang-apple", "clang", toolchain_component_compiler },
		{ "ld-apple", "lld-apple", toolchain_component_linker },
		{ "posix", "ar-posix", toolchain_component_archiver },
		{ "ar", "ar-gnu", toolchain_component_archiver },
	};

	{
		const struct {
			enum toolchain_component component;
			uint32_t kw;
		} toolchain_elem[] = {
			{ toolchain_component_compiler, kw_inherit_compiler },
			{ toolchain_component_linker, kw_inherit_linker },
			{ toolchain_component_archiver, kw_inherit_archiver },
		};

		uint32_t i;
		for (i = 0; i < ARRAY_LEN(toolchain_elem); ++i) {
			const enum toolchain_component component = toolchain_elem[i].component;

			if (!akw[toolchain_elem[i].kw].set) {
				if (akw[kw_inherit].set) {
					akw[toolchain_elem[i].kw].val = akw[kw_inherit].val;
					akw[toolchain_elem[i].kw].node = akw[kw_inherit].node;
				} else {
					continue;
				}
			}

			uint32_t type;
			obj override = 0;
			obj cmd_array = 0;

			if (get_obj_type(wk, akw[toolchain_elem[i].kw].val) == obj_string) {
				const char *s = get_cstr(wk, akw[toolchain_elem[i].kw].val);
				for (uint32_t i = 0; i < ARRAY_LEN(deprecated_name_map); ++i) {
					if (deprecated_name_map[i].component == component && strcmp(deprecated_name_map[i].old, s) == 0) {
						s = deprecated_name_map[i].new;
						vm_deprecation_at(wk,
							akw[kw_inherit_static_linker].node,
							"0.6.0",
							"%s has been renamed to %s",
							deprecated_name_map[i].old,
							deprecated_name_map[i].new);
						break;
					}
				}

				uint32_t compiler_type;
				if (!toolchain_component_type_from_s(wk, component, s, &compiler_type)) {
					vm_error_at(wk,
						akw[toolchain_elem[i].kw].node,
						"unknown %s type: %o",
						toolchain_component_to_s(component),
						akw[toolchain_elem[i].kw].val);
					return false;
				}

				const struct arr *registry = &wk->toolchain_registry.components[component];
				type = compiler_type;
				const struct toolchain_registry_component *rc = arr_get(registry, compiler_type);
				override = rc->overrides;
			} else {
				const struct obj_compiler *base = get_obj_compiler(wk, akw[toolchain_elem[i].kw].val);
				type = base->type[component];
				override = base->overrides[component];
				cmd_array = base->cmd_arr[component];
			}

			c->type[component] = type;
			c->overrides[component] = override;
			c->cmd_arr[component] = cmd_array;
		}
	}

	return true;
}

FUNC_IMPL(module_toolchain, parse_triple, tc_dict, .desc = "parse a target triple")
{
	struct args_norm an[] = { { tc_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	struct target_triple t = { 0 };
	machine_parse_triple(get_str(wk, an[0].val), &t);
	*res = machine_parsed_triple_to_obj(wk, &t);

	return true;
}

static bool
func_modue_toolchain_register_component_common(struct workspace *wk, enum toolchain_component component, obj *res)
{
	type_tag exe_type = component == toolchain_component_compiler ? complex_type_preset_get(wk, tc_cx_dict_of_str) :
									tc_string;
#define EXE_DESC_COMMON "The `exe_name` is used to bootstrap toolchain detection when no executable is explicitly defined (e.g. using `CC=clang`)."
	const char *exe_desc = component == toolchain_component_compiler ?
				       "A dict mapping `langage -> exe_name`, e.g. `{'c': 'clang', 'cpp': 'clang++'}`. " EXE_DESC_COMMON:
				       EXE_DESC_COMMON;
#undef EXE_DESC_COMMON

	const struct args_norm sub_component_an[] = { { tc_compiler }, { ARG_TYPE_NULL } };
	const struct typecheck_closure_sig sub_component_sig = {
		.an = sub_component_an,
		.return_type = tc_string,
	};

	const struct args_norm detect_an[] = { { tc_string }, { ARG_TYPE_NULL } };
	const struct typecheck_closure_sig detect_sig = {
		.an = detect_an,
		.return_type = tc_number,
	};

	const struct args_norm on_detect_an[] = { { tc_compiler }, { ARG_TYPE_NULL } };
	const struct typecheck_closure_sig on_detect_sig = {
		.an = on_detect_an,
	};

	TSTR(detect_desc);
	TSTR(handlers_desc);
	TSTR(linker_desc);
	TSTR(archiver_desc);
	TSTR(on_detect_desc);
	if (wk->vm.dumping_docs) {
		tstr_pushs(wk, &detect_desc, "If set, defines a function `");
		typecheck_closure_type_to_s(wk, &detect_desc, &detect_sig);
		tstr_pushs(wk,
			&detect_desc,
			"` that will be called with the version output."
			" This function is passed a string containing the combined stdout and stderr resulting from executing the candidate exe with this components version argument."
			" If no version argument is defined then the empty string is passed."
			" The return value should be a number indicating the confidence of a match, anything <= 0 will be skipped."
			" If no detect function is specified the default score of 1 will be applied."
			" After all candidate's detections are executed, the candidate with the highest score will be selected.");

		tstr_pushs(wk,
			&handlers_desc,
			"A dict mapping handler names to functions."
			" If the handler returns a constant value, then that may be used instead of a function."
			" The full list of supported handlers for this component are listed in the table below."
			" For some handlers, the description shows a common value for that handler."
			" For examples of toolchain definitons, see `src/script/runtime/toolchains.meson` in muon's source tree.\n");
		toolchain_overrides_doc(wk, component, &handlers_desc);
		tstr_pushs(wk, &handlers_desc, "\n");

		const struct {
			struct tstr *buf;
			enum toolchain_component component;
		} sub_components[] = {
			{ &linker_desc, toolchain_component_linker },
			{ &archiver_desc, toolchain_component_archiver },
		};
		for (uint32_t i = 0; i < ARRAY_LEN(sub_components); ++i) {
			tstr_pushs(wk, sub_components[i].buf, "If set, defines a string or function `");
			typecheck_closure_type_to_s(wk, sub_components[i].buf, &sub_component_sig);
			tstr_pushf(wk,
				sub_components[i].buf,
				"` that will be used to determine the %s id for this compiler.",
				toolchain_component_to_s(sub_components[i].component));
		}

		tstr_pushs(wk, &on_detect_desc, "If set, defines a function `");
		typecheck_closure_type_to_s(wk, &on_detect_desc, &detect_sig);
		tstr_pushs(wk, &on_detect_desc, "` that will be called when this toolchain is successfully detected");
	}

	struct args_norm an[] = {
		{ tc_string, .desc = "The id of this component" },
		ARG_TYPE_NULL,
	};
	enum kwargs {
		kw_public_id,
		kw_inherit,
		kw_detect,
		kw_handlers,
		kw_exe,
		kw_linker,
		kw_archiver,
		kw_on_detect,
	};
	struct args_kw akw[] = {
		[kw_public_id] = { "public_id", tc_string, .desc = "Returned by `compiler.get_id()` and `compiler.get_linker_id()`.  Defaults to `id` if not set." },
		[kw_inherit] = { "inherit", tc_string, .desc = "If set, inherit properties from a previously defined component." },
		[kw_exe] = { "exe", exe_type, .desc = exe_desc },
		[kw_detect] = { "detect", tc_closure, .desc = detect_desc.buf },
		[kw_handlers] = { "handlers", COMPLEX_TYPE_PRESET(tc_cx_toolchain_overrides), .desc = handlers_desc.buf },
		[kw_linker] = { component == toolchain_component_compiler ? "linker" : 0, tc_string | tc_closure, .desc = linker_desc.buf },
		[kw_archiver] = { "archiver", tc_string | tc_closure, .desc = archiver_desc.buf },
		[kw_on_detect] = { "on_detect", tc_closure, .desc = on_detect_desc.buf },
		0,
	};
	if (!pop_args(wk, an, akw)) {
		return false;
	}

	const struct {
		struct args_kw *kw;
		enum toolchain_component component;
	} sub_components[] = {
		{ &akw[kw_linker], toolchain_component_linker },
		{ &akw[kw_archiver], toolchain_component_archiver },
	};

	for (uint32_t i = 0; i < ARRAY_LEN(sub_components); ++i) {
		const struct args_kw *kw = sub_components[i].kw;
		if (kw->set) {
			assert(component == toolchain_component_compiler);

			if (get_obj_type(wk, kw->val) == obj_closure) {
				if (!typecheck_closure(wk, kw->node, kw->val, &sub_component_sig, kw->key)) {
					return false;
				}
			}
		}
	}

	if (akw[kw_detect].set) {
		if (!typecheck_closure(wk, akw[kw_detect].node, akw[kw_detect].val, &detect_sig, "detect")) {
			return false;
		}
	}

	if (akw[kw_on_detect].set) {
		if (!typecheck_closure(wk, akw[kw_on_detect].node, akw[kw_on_detect].val, &on_detect_sig, "on_detect")) {
			return false;
		}
	}

	if (akw[kw_handlers].set) {
		if (!toolchain_overrides_validate(wk, akw[kw_handlers].node, akw[kw_handlers].val, component)) {
			return false;
		}
	}

	if (wk->vm.in_analyzer) {
		return true;
	}

	const struct toolchain_registry_component *inherit = 0;
	struct toolchain_registry_component base;
	{
		const char *id = get_cstr(wk, an[0].val);
		const char *public_id = akw[kw_public_id].set ? get_cstr(wk, akw[kw_public_id].val) : id;

		base = (struct toolchain_registry_component){ .id = { .id = id, .public_id = public_id } };
	}

	{
		const struct arr *registry = &wk->toolchain_registry.components[component];

		uint32_t inherit_type = 0;
		if (akw[kw_inherit].set) {
			if (!toolchain_component_type_from_s(wk, component, get_cstr(wk, akw[kw_inherit].val), &inherit_type)) {
				vm_error_at(wk, akw[kw_inherit].node, "unknown %s %o", toolchain_component_to_s(component), akw[kw_inherit].val);
				return false;
			}
		}

		inherit = arr_get(registry, inherit_type);

		base.detect = inherit->detect;
		base.exe = inherit->exe;
		base.on_detect = inherit->on_detect;
		memcpy(base.sub_components, inherit->sub_components, sizeof(inherit->sub_components));
	}

	if (akw[kw_detect].set) {
		base.detect = akw[kw_detect].val;
	}

	for (uint32_t i = 0; i < ARRAY_LEN(sub_components); ++i) {
		const struct args_kw *kw = sub_components[i].kw;
		const enum toolchain_component sub_component = sub_components[i].component;

		if (kw->set) {
			if (get_obj_type(wk, kw->val) == obj_string) {
				uint32_t type;
				if (!toolchain_component_type_from_s(wk, sub_component, get_cstr(wk, kw->val), &type)) {
					vm_error(wk, "unknown %s type %s", toolchain_component_to_s(sub_component), get_cstr(wk, kw->val));
					return false;
				}
				base.sub_components[sub_component].fn = 0;
				base.sub_components[sub_component].type = type;
			} else {
				base.sub_components[sub_component].fn = kw->val;
				base.sub_components[sub_component].type = 0;
			}
		}
	}

	if (akw[kw_handlers].set) {
		obj overrides = akw[kw_handlers].val;
		if (inherit && inherit->overrides) {
			obj merged;
			obj_dict_merge(wk, inherit->overrides, overrides, &merged);
			overrides = merged;
		}
		base.overrides = overrides;
	} else if (inherit) {
		base.overrides = inherit->overrides;
	}

	if (akw[kw_exe].set) {
		if (get_obj_type(wk, akw[kw_exe].val) == obj_string) {
			base.exe = akw[kw_exe].val;
		} else {
			base.exe = make_obj(wk, obj_dict);

			obj k, v;
			obj_dict_for(wk, akw[kw_exe].val, k, v) {
				struct compiler_language l;
				if (!s_to_compiler_language(wk, get_str(wk, k), &l)) {
					vm_error(wk, "unknown language %o", k);
					return false;
				}
				obj_dict_set_language(wk, base.exe, l, v);
			}
		}
	}

	if (!base.exe) {
		vm_error(wk, "exe not set manually or through inheritance");
		return false;
	}

	if (akw[kw_on_detect].val) {
		base.on_detect = akw[kw_on_detect].val;
	}

	return toolchain_register_component(wk, component, &base);
}

FUNC_IMPL(module_toolchain, register_compiler, 0, .desc = "Register a new compiler type")
{
	return func_modue_toolchain_register_component_common(wk, toolchain_component_compiler, res);
}

FUNC_IMPL(module_toolchain, register_linker, 0, .desc = "Register a new linker type")
{
	return func_modue_toolchain_register_component_common(wk, toolchain_component_linker, res);
}

FUNC_IMPL(module_toolchain, register_archiver, 0, .desc = "Register a new static linker type")
{
	return func_modue_toolchain_register_component_common(wk, toolchain_component_archiver, res);
}

FUNC_IMPL(module_toolchain, handler, tc_closure | tc_array, func_impl_flag_impure, .desc = "Retrieve a previously defined handler")
{
	struct args_norm an[] = {
		{ tc_string },
		{ tc_string },
		{ tc_string },
		ARG_TYPE_NULL,
	};
	if (!pop_args(wk, an, 0)) {
		return false;
	}


	uint32_t component;
	if (!toolchain_component_from_s(wk, get_cstr(wk, an[0].val), &component)) {
		vm_error_at(wk, an[0].node, "unknown component %o", an[0].val);
		return false;
	}

	uint32_t type;
	if (!toolchain_component_type_from_s(wk, component, get_cstr(wk, an[1].val), &type)) {
		vm_error_at(wk, an[1].node, "unknown %s %o", toolchain_component_to_s(component), an[1].val);
		return false;
	}

	const struct arr *registry = &wk->toolchain_registry.components[component];
	const struct toolchain_registry_component *c = arr_get(registry, type);
	if (!obj_dict_index(wk, c->overrides, an[2].val, res)) {
		vm_error_at(wk, an[1].node, "unknown %s handler %o", toolchain_component_to_s(component), an[2].val);
		return false;
	}

	return true;
}

struct module_toolchain_register_language_toolchain_options
{
	obj compiler, linker, archiver;
};

FUNC_IMPL(module_toolchain, register_language, .desc = "Register a language")
{
	if (vm_enum(wk, enum language_family)) {
		 vm_enum_value_prefixed(wk, language_family, c);
		 vm_enum_value_prefixed(wk, language_family, unknown);
	}

	if (vm_struct(wk, module_toolchain_register_language_toolchain_options)) {
		vm_struct_member(wk, module_toolchain_register_language_toolchain_options, compiler, vm_struct_type_obj);
		vm_struct_member(wk, module_toolchain_register_language_toolchain_options, linker, vm_struct_type_obj);
		vm_struct_member(wk, module_toolchain_register_language_toolchain_options, archiver, vm_struct_type_obj);
	}

	struct args_norm an[] = {
		{ tc_string, .desc = "The name of this language" },
		ARG_TYPE_NULL,
	};
	enum kwargs {
		kw_family,
		kw_file_extensions,
		kw_toolchain_options,
		kw_options,
		kw_coalesce_link_language,
		kw_on_added,
		kw_link_language,
		kw_header_language,
		kw_is_header,
		kw_is_linkable,
		kw_inherit_compile_args,
	};
	struct args_kw akw[] = {
		[kw_family] = { "family", complex_type_enum_get(wk, enum language_family) },
		[kw_file_extensions] = { "file_extensions", COMPLEX_TYPE_PRESET(tc_cx_list_of_str) },
		[kw_toolchain_options] = { "toolchain_options",
			COMPLEX_TYPE_PRESET(tc_cx_dict_of_str),
			.desc = vm_struct_docs(wk,
				module_toolchain_register_language_toolchain_options,
				"accepted properties:\n```\n%s\n```") },
		[kw_options] = { "options", tc_dict }, // TODO: better type
		[kw_link_language] = { "link_language", tc_string },
		[kw_header_language] = { "header_language", tc_string },
		[kw_coalesce_link_language] = { "coalesce_link_language", tc_closure },
		[kw_on_added] = { "on_added", tc_closure },
		[kw_is_header] = { "is_header", tc_bool },
		[kw_is_linkable] = { "is_linkable", tc_bool },
		[kw_inherit_compile_args] = { "inherit_compile_args", COMPLEX_TYPE_PRESET(tc_cx_list_of_str) },
		0,
	};

	if (!pop_args(wk, an, akw)) {
		return false;
	}

	if (akw[kw_coalesce_link_language].set)
	{
		const struct args_norm an[] = { { tc_string }, { ARG_TYPE_NULL } };
		const struct typecheck_closure_sig sig = {
			.an = an,
			.return_type = tc_string,
		};
		if (!typecheck_closure(wk,
			    akw[kw_coalesce_link_language].node,
			    akw[kw_coalesce_link_language].val,
			    &sig,
			    "coalesce_link_language")) {
			return false;
		}
	}

	if (akw[kw_on_added].set) {
		const struct args_norm an[] = { { tc_compiler }, { ARG_TYPE_NULL } };
		const struct typecheck_closure_sig sig = { .an = an };
		if (!typecheck_closure(wk, akw[kw_on_added].node, akw[kw_on_added].val, &sig, "on_added")) {
			return false;
		}
	}

	if (get_obj_type(wk, an[0].val) == obj_typeinfo) {
		vm_error_at(wk, an[0].node, "registered language names must be static");
		return false;
	}

	const struct compiler_language lang_id = { wk->language_registry.languages.len };
	struct language *lang = arr_grow_by(wk->a, &wk->language_registry.languages, 1);
	lang->name = *get_str(wk, an[0].val);

	if (akw[kw_options].set && get_obj_type(wk, akw[kw_options].val) != obj_typeinfo) {
		obj k, v;
		obj_dict_for(wk, akw[kw_options].val, k, v) {
			// I'm not actually sure if it is possible for k to be typeinfo
			if (get_obj_type(wk, k) == obj_typeinfo || get_obj_type(wk, v) == obj_typeinfo) {
				continue;
			}
			struct obj_option *o = get_obj_option(wk, v);
			o->name = make_strf(wk, "%s_%s", lang->name.s, get_str(wk, k)->s);
			o->yield = true;
			obj_array_push(wk, wk->queued_options, v);
		}
	}

	if (akw[kw_family].set) {
		if (!vm_obj_to_enum(wk, akw[kw_family].node, enum language_family, akw[kw_family].val, &lang->family)) {
			return false;
		}
	}

	if (wk->vm.in_analyzer) {
		return true;
	}

	lang->exts = akw[kw_file_extensions].val;
	lang->coalesce_link_language = akw[kw_coalesce_link_language].val;
	lang->on_added = akw[kw_on_added].val;
	lang->is_header = get_obj_bool_with_default(wk, akw[kw_is_header].val, false);
	lang->is_linkable = get_obj_bool_with_default(wk, akw[kw_is_linkable].val, false);
	lang->inherit_compile_args = akw[kw_inherit_compile_args].val;

	struct {
		const struct args_kw *src;
		struct compiler_language *dest;
	} related_langs[] = {
		{ &akw[kw_link_language], &lang->link_language },
		{ &akw[kw_header_language], &lang->header_language },
	};

	for (uint32_t i = 0; i < ARRAY_LEN(related_langs); ++i) {
		const struct args_kw *kw = related_langs[i].src;
		struct compiler_language *dest = related_langs[i].dest;

		if (kw->set) {
			if (!s_to_compiler_language(wk, get_str(wk, kw->val), dest)) {
				vm_error_at(wk, kw->node, "unknown language %o", kw->val);
				return false;
			}
		}
	}

	if (akw[kw_toolchain_options].set) {
		struct module_toolchain_register_language_toolchain_options opts = { 0 };
		if (!vm_obj_to_struct(wk,
			    module_toolchain_register_language_toolchain_options,
			    akw[kw_toolchain_options].val,
			    &opts)) {
			return false;
		}

		if (opts.compiler) {
			lang->env_option_names[toolchain_component_compiler] = get_cstr(wk, opts.compiler);
		}
		if (opts.linker) {
			lang->env_option_names[toolchain_component_linker] = get_cstr(wk, opts.linker);

			if (!akw[kw_link_language].set) {
				lang->link_language = lang_id;
			}
		}
		if (opts.archiver) {
			lang->env_option_names[toolchain_component_archiver] = get_cstr(wk, opts.archiver);
		}
	}

	L("registered language %s", lang->name.s);

	return true;
}

FUNC_IMPL(module_toolchain, option, tc_option, .desc = "Create a toolchain option object")
{
	struct func_option_common_result r;
	stack_push(&wk->stack, initializing_builtin_options_state, initializing_builtin_options_state_project);
	bool ok = func_option_common(wk, 0, &r);
	stack_pop(&wk->stack, initializing_builtin_options_state);
	*res = r.option;
	return ok;
}

FUNC_REGISTER(module_toolchain)
{
	if (lang_mode == language_internal) {
		FUNC_IMPL_REGISTER(module_toolchain, create);
		FUNC_IMPL_REGISTER(module_toolchain, parse_triple);
		FUNC_IMPL_REGISTER(module_toolchain, register_compiler);
		FUNC_IMPL_REGISTER(module_toolchain, register_linker);
		FUNC_IMPL_REGISTER(module_toolchain, register_archiver);
		FUNC_IMPL_REGISTER(module_toolchain, handler);
		FUNC_IMPL_REGISTER(module_toolchain, register_language);
		FUNC_IMPL_REGISTER(module_toolchain, option);
	}
}

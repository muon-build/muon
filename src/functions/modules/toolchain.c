/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "buf_size.h"
#include "functions/modules/toolchain.h"
#include "lang/object_iterators.h"
#include "lang/typecheck.h"
#include "log.h"

FUNC_IMPL(module_toolchain,
	create,
	tc_compiler,
	.desc
	= "Creates a new compiler object that can be passed in to the `toolchain` keyword on `add_languages` or inherited from when coreating a new toolchain.  The toolchain object is reffered to as a `compiler` for historical reasons, although it also contains other information required to compile programs such as linker metadata.")
{
	enum kwargs {
		kw_inherit,
		kw_inherit_compiler,
		kw_inherit_linker,
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
		[kw_inherit_static_linker] = { "inherit_static_linker",
			tc_string | tc_compiler,
			.desc
			= "The static linker component to inherit from.  Can be either a compiler object or static linker type name." },
		0,
	};

	if (!pop_args(wk, NULL, akw)) {
		return false;
	}

	*res = make_obj(wk, obj_compiler);
	struct obj_compiler *c = get_obj_compiler(wk, *res);
	c->ver[toolchain_component_compiler] = make_str(wk, "unknown");
	c->libdirs = make_obj(wk, obj_array);

	{
		const struct {
			enum toolchain_component component;
			uint32_t kw;
		} toolchain_elem[] = {
			{ toolchain_component_compiler, kw_inherit_compiler },
			{ toolchain_component_linker, kw_inherit_linker },
			{ toolchain_component_static_linker, kw_inherit_static_linker },
		};

		uint32_t i;
		for (i = 0; i < ARRAY_LEN(toolchain_elem); ++i) {
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
				uint32_t compiler_type;
				if (!toolchain_component_type_from_s(wk, toolchain_elem[i].component, get_cstr(wk, akw[toolchain_elem[i].kw].val), &compiler_type)) {
					vm_error_at(wk,
						akw[toolchain_elem[i].kw].node,
						"unknown %s type: %o",
						toolchain_component_to_s(toolchain_elem[i].component),
						akw[toolchain_elem[i].kw].val);
					return false;
				}

				type = compiler_type;
			} else {
				const struct obj_compiler *base = get_obj_compiler(wk, akw[toolchain_elem[i].kw].val);
				type = base->type[i];
				override = base->overrides[i];
				cmd_array = base->cmd_arr[i];
			}

			c->type[i] = type;
			c->overrides[i] = override;
			c->cmd_arr[i] = cmd_array;
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

	struct args_norm an[] = {
		{ tc_string },
		ARG_TYPE_NULL,
	};
	enum kwargs {
		kw_public_id,
		kw_inherit,
		kw_detect,
		kw_handlers,
		kw_exe,
		kw_linker,
		kw_static_linker,
	};
	struct args_kw akw[] = {
		[kw_public_id] = { "public_id", tc_string },
		[kw_inherit] = { "inherit", tc_string },
		[kw_exe] = { "exe", exe_type },
		[kw_detect] = { "detect", tc_capture },
		[kw_handlers] = { "handlers", COMPLEX_TYPE_PRESET(tc_cx_toolchain_overrides) },
		[kw_linker] = { component == toolchain_component_compiler ? "linker" : 0, tc_string | tc_capture },
		[kw_static_linker] = { "static_linker", tc_string | tc_capture },
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
		{ &akw[kw_static_linker], toolchain_component_static_linker },
	};

	for (uint32_t i = 0; i < ARRAY_LEN(sub_components); ++i) {
		const struct args_kw *kw = sub_components[i].kw;
		if (kw->set) {
			assert(component == toolchain_component_compiler);

			if (get_obj_type(wk, kw->val) == obj_capture) {
				struct args_norm an[] = { { tc_compiler }, { ARG_TYPE_NULL } };
				if (!typecheck_capture(wk, kw->node, kw->val, an, 0, tc_string, kw->key)) {
					return false;
				}
			}
		}
	}

	if (akw[kw_detect].set) {
		struct args_norm detect_an[] = { { tc_string }, { ARG_TYPE_NULL } };
		if (!typecheck_capture(wk, akw[kw_detect].node, akw[kw_detect].val, detect_an, 0, tc_number, "detect")) {
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
				base.sub_components[sub_component].type = type;
			} else {
				base.sub_components[sub_component].fn = kw->val;
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
				enum compiler_language l;
				if (!s_to_compiler_language(get_cstr(wk, k), &l)) {
					vm_error(wk, "unknown language %o", k);
					return false;
				}
				obj_dict_seti(wk, base.exe, l, v);
			}
		}
	}

	if (!base.exe) {
		vm_error(wk, "exe not set manually or through inheritance");
		return false;
	}

	return toolchain_register_component(wk, component, &base);
}

FUNC_IMPL(module_toolchain, register_compiler, tc_dict, .desc = "Register a new compiler type")
{
	return func_modue_toolchain_register_component_common(wk, toolchain_component_compiler, res);
}

FUNC_IMPL(module_toolchain, register_linker, tc_dict, .desc = "Register a new linker type")
{
	return func_modue_toolchain_register_component_common(wk, toolchain_component_linker, res);
}

FUNC_IMPL(module_toolchain, register_static_linker, tc_dict, .desc = "Register a new static linker type")
{
	return func_modue_toolchain_register_component_common(wk, toolchain_component_static_linker, res);
}

FUNC_IMPL(module_toolchain, handler, tc_capture | tc_array, func_impl_flag_impure, .desc = "Retrieve a previously defined handler")
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
	if (!toolchain_component_from_s(get_cstr(wk, an[0].val), &component)) {
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

FUNC_REGISTER(module_toolchain)
{
	if (lang_mode == language_internal) {
		FUNC_IMPL_REGISTER(module_toolchain, create);
		FUNC_IMPL_REGISTER(module_toolchain, parse_triple);
		FUNC_IMPL_REGISTER(module_toolchain, register_compiler);
		FUNC_IMPL_REGISTER(module_toolchain, register_linker);
		FUNC_IMPL_REGISTER(module_toolchain, register_static_linker);
		FUNC_IMPL_REGISTER(module_toolchain, handler);
	}
}

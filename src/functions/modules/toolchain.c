/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "buf_size.h"
#include "log.h"
#include "functions/modules/toolchain.h"
#include "lang/typecheck.h"

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
	c->ver = make_str(wk, "unknown");
	c->libdirs = make_obj(wk, obj_array);

	{
		const struct {
			const char *name;
			uint32_t kw;
			bool (*lookup_name)(struct workspace *wk, const char *, uint32_t *);
		} toolchain_elem[] = {
			{ "compiler", kw_inherit_compiler, compiler_type_from_s },
			{ "linker", kw_inherit_linker, linker_type_from_s },
			{ "static_linker", kw_inherit_static_linker, static_linker_type_from_s },
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
				if (!toolchain_elem[i].lookup_name(
					    wk, get_cstr(wk, akw[toolchain_elem[i].kw].val), &compiler_type)) {
					vm_error_at(wk,
						akw[toolchain_elem[i].kw].node,
						"unknown %s type: %o",
						toolchain_elem[i].name,
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

static void
module_toolchain_set_triple_value(struct workspace *wk, obj d, const char *key, const struct str *val)
{
	obj_dict_set(wk, d, make_str(wk, key), val->len ? make_strn(wk, val->s, val->len) : make_str(wk, "unknown"));
}

FUNC_IMPL(module_toolchain, parse_triple, tc_dict, .desc = "parse a target triple")
{
	struct args_norm an[] = { { tc_string }, ARG_TYPE_NULL };
	if (!pop_args(wk, an, NULL)) {
		return false;
	}

	struct target_triple t = { 0 };
	machine_parse_triple(get_str(wk, an[0].val), &t);

	*res = make_obj(wk, obj_dict);
	module_toolchain_set_triple_value(wk, *res, "arch", &t.arch);
	module_toolchain_set_triple_value(wk, *res, "vendor", &t.vendor);
	module_toolchain_set_triple_value(wk, *res, "system", &t.system);
	module_toolchain_set_triple_value(wk, *res, "env", &t.env);

	return true;
}

FUNC_IMPL(module_toolchain, register, tc_dict, .desc = "Register a new toolchain type")
{
	struct args_norm an[] = {
		{ tc_string },
		ARG_TYPE_NULL,
	};
	enum kwargs {
		kw_component,
		kw_public_id,
		kw_inherit,
		kw_default_linker,
		kw_default_static_linker,
		kw_detect,
		kw_handlers,
	};
	struct args_kw akw[] = {
		[kw_component]
		= { "component", complex_type_preset_get(wk, tc_cx_enum_toolchain_component), .required = true },
		[kw_public_id] = { "public_id", tc_string },
		[kw_inherit] = { "inherit", tc_string },
		[kw_default_linker] = { "default_linker", tc_string },
		[kw_default_static_linker] = { "default_static_linker", tc_string },
		[kw_detect] = { "detect", tc_capture, .required = true },
		[kw_handlers] = { "handlers", COMPLEX_TYPE_PRESET(tc_cx_toolchain_overrides) },
		0,
	};
	if (!pop_args(wk, an, akw)) {
		return false;
	}

	const char *id = get_cstr(wk, an[0].val);
	const char *public_id = akw[kw_public_id].set ? get_cstr(wk, akw[kw_public_id].val) : id;

	uint32_t component;
	if (!toolchain_component_from_s(get_cstr(wk, akw[kw_component].val), &component)) {
		vm_error(wk, "unknown toolchain component %o", akw[kw_component].val);
		return false;
	}

	struct toolchain_registry_component base = { .id = { .id = id, .public_id = public_id } };
	union {
		struct compiler compiler;
		struct linker linker;
		struct static_linker static_linker;
	} data = { 0 };

	{
		uint32_t inherit_type = 0;
		if (akw[kw_inherit].set) {
			if (!toolchain_type_from_s(wk, component, get_cstr(wk, akw[kw_inherit].val), &inherit_type)) {
				vm_error_at(wk, akw[kw_inherit].node, "unknown %s %o", toolchain_component_to_s(component), akw[kw_inherit].val);
				return false;
			}
		}

		const struct arr *registry = &wk->toolchain_registry.components[component];

		switch (component) {
		case toolchain_component_compiler:
			data.compiler = ((struct toolchain_registry_component_compiler *)arr_get(registry, inherit_type))->comp;
			break;
		case toolchain_component_linker:
			data.linker = ((struct toolchain_registry_component_linker *)arr_get(registry, inherit_type))->comp;
			break;
		case toolchain_component_static_linker:
			data.static_linker = ((struct toolchain_registry_component_static_linker *)arr_get(registry, inherit_type))->comp;
			break;
		}
	}

	{
		struct args_norm detect_an[] = { { tc_string }, { tc_string }, { ARG_TYPE_NULL } };
		if (!typecheck_capture(wk, akw[kw_detect].node, akw[kw_detect].val, detect_an, 0, tc_bool, "detect")) {
			return false;
		}
		base.detect = akw[kw_detect].val;
	}

	if (akw[kw_default_linker].set) {
		if (component != toolchain_component_compiler) {
			vm_error(wk, "default_linker is only valid for a compiler component");
			return false;
		}

		uint32_t default_linker;
		if (!linker_type_from_s(wk, get_cstr(wk, akw[kw_default_linker].val), &default_linker)) {
			vm_error(wk, "unknown linker type %s", get_cstr(wk, akw[kw_default_linker].val));
			return false;
		}
		data.compiler.default_linker = default_linker;
	}

	if (akw[kw_default_static_linker].set) {
		if (component != toolchain_component_compiler) {
			vm_error(wk, "default_static_linker is only valid for a compiler component");
			return false;
		}

		uint32_t default_static_linker;
		if (!static_linker_type_from_s(wk, get_cstr(wk, akw[kw_default_static_linker].val), &default_static_linker)) {
			vm_error(wk, "unknown static linker type %s", get_cstr(wk, akw[kw_default_static_linker].val));
			return false;
		}
		data.compiler.default_linker = default_static_linker;
	}

	if (akw[kw_handlers].set) {
		if (!toolchain_overrides_validate(wk, akw[kw_handlers].node, akw[kw_handlers].val, component)) {
			return false;
		}

		base.overrides = akw[kw_handlers].val;
	}

	if (wk->vm.in_analyzer) {
		return true;
	}

	return toolchain_register_component(wk, component, &base, &data);
}

FUNC_REGISTER(module_toolchain)
{
	if (lang_mode == language_internal) {
		FUNC_IMPL_REGISTER(module_toolchain, create);
		FUNC_IMPL_REGISTER(module_toolchain, parse_triple);
		FUNC_IMPL_REGISTER(module_toolchain, register);
	}
}

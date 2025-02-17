/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include "buf_size.h"
#include "functions/modules/toolchain.h"
#include "lang/typecheck.h"

static bool
func_module_toolchain_create(struct workspace *wk, obj self, obj *res)
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
			bool (*lookup_name)(const char *, uint32_t *);
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
					    get_cstr(wk, akw[toolchain_elem[i].kw].val), &compiler_type)) {
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

const struct func_impl impl_tbl_module_toolchain[] = {
	{ "create",
		func_module_toolchain_create,
		tc_compiler,
		.desc
		= "Creates a new compiler object that can be passed in to the `toolchain` keyword on `add_languages` or inherited from when coreating a new toolchain.  The toolchain object is reffered to as a `compiler` for historical reasons, although it also contains other information required to compile programs such as linker metadata." },
	{ NULL, NULL },
};

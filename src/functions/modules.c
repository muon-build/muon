/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "buf_size.h"
#include "embedded.h"
#include "functions/modules.h"
#include "functions/modules/curl.h"
#include "functions/modules/fs.h"
#include "functions/modules/getopt.h"
#include "functions/modules/json.h"
#include "functions/modules/keyval.h"
#include "functions/modules/pkgconfig.h"
#include "functions/modules/python.h"
#include "functions/modules/sourceset.h"
#include "functions/modules/subprojects.h"
#include "functions/modules/toolchain.h"
#include "lang/func_lookup.h"
#include "lang/object_iterators.h"
#include "lang/typecheck.h"
#include "platform/assert.h"
#include "platform/filesystem.h"
#include "platform/path.h"

#define MODULE_INFO(mod, _path, _implemented) \
	{ .name = #mod, .path = _path, .implemented = _implemented },
const struct module_info module_info[module_count] = { FOREACH_BUILTIN_MODULE(MODULE_INFO) };
#undef MODULE_INFO

static bool
module_lookup_builtin(const char *name, enum module *res, bool *has_impl)
{
	enum module i;
	for (i = 0; i < module_count; ++i) {
		if (strcmp(name, module_info[i].path) == 0) {
			*res = i;
			*has_impl = module_info[i].implemented;
			return true;
		}
	}

	return false;
}

struct module_lookup_script_opts {
	bool embedded;
	bool encapsulate;
};

static bool
module_lookup_script(struct workspace *wk,
	struct tstr *path,
	struct obj_module *m,
	const struct module_lookup_script_opts *opts)
{
	bool ret = false;
	bool stack_popped = false;
	stack_push(&wk->stack, wk->vm.lang_mode, language_extended);
	stack_push(&wk->stack, wk->vm.scope_stack, wk->vm.behavior.scope_stack_dup(wk, wk->vm.default_scope_stack));

	obj res;
	if (opts->embedded) {
		struct source src;
		if (!(embedded_get(path->buf, &src))) {
			goto ret;
		}
		src.label = get_cstr(wk, tstr_into_str(wk, path));
		src.len = strlen(src.src);
		if (!eval(wk, &src, build_language_meson, 0, &res)) {
			goto ret;
		}
	} else {
		if (!wk->vm.behavior.eval_project_file(wk, path->buf, build_language_meson, 0, &res)) {
			goto ret;
		}
	}

	if (!typecheck_custom(wk,
		    0,
		    res,
		    make_complex_type(wk, complex_type_nested, tc_dict, tc_capture),
		    "expected %s, got %s for module return type")) {
		goto ret;
	}

	if (opts->encapsulate) {
		m->found = true;
		m->has_impl = true;
		m->exports = res;
	} else {
		stack_pop(&wk->stack, wk->vm.scope_stack);
		stack_pop(&wk->stack, wk->vm.lang_mode);
		stack_popped = true;
		obj k, v;
		obj_dict_for(wk, res, k, v) {
			wk->vm.behavior.assign_variable(wk, get_cstr(wk, k), v, 0, assign_local);
		}
	}

	ret = true;
ret:
	if (!stack_popped) {
		stack_pop(&wk->stack, wk->vm.scope_stack);
		stack_pop(&wk->stack, wk->vm.lang_mode);
	}
	return ret;
}

const char *module_paths[] = {
	[language_external] = "embedded:modules/%.meson;builtin:public/%",
	[language_internal] = "embedded:lib/%.meson;builtin:private/%;builtin:public/%",
	[language_opts] = "",
	[language_extended] = "embedded:lib/%.meson;builtin:private/%;builtin:public/%",
};

bool
module_import(struct workspace *wk, const char *name, bool encapsulate, obj *res)
{
	struct obj_module *m = 0;

	{
		enum {
			schema_type_file,
			schema_type_embedded,
			schema_type_builtin,
		} schema;

		const char *schema_type_str[] = {
			[schema_type_file] = "file",
			[schema_type_embedded] = "embedded",
			[schema_type_builtin] = "builtin",
		};

		bool loop = true;
		struct str path;
		TSTR(path_interpolated);
		TSTR(module_path);
		const char *p, *sep;

		{
			struct project *proj;
			if (wk->vm.lang_mode == language_external && (proj = current_project(wk)) && proj->module_dir) {
				tstr_pushs(wk, &module_path, module_paths[wk->vm.lang_mode]);

				tstr_pushs(wk, &module_path, ";file:");

				TSTR(new_module_path);
				path_push(wk, &new_module_path, get_cstr(wk, proj->source_root));
				path_push(wk, &new_module_path, get_cstr(wk, proj->module_dir));
				path_push(wk, &new_module_path, "%.meson");
				tstr_pushn(wk, &module_path, new_module_path.buf, new_module_path.len);
				p = module_path.buf;
			} else {
				p = module_paths[wk->vm.lang_mode];
			}
		}

		while (loop) {
			path.s = p;
			if ((sep = strchr(path.s, ';'))) {
				path.len = sep - path.s;
				p = sep + 1;
			} else {
				path.len = strlen(path.s);
				loop = false;
			}

			{ // Parse schema if given
				if ((sep = memchr(path.s, ':', path.len))) {
					const struct str schema_str = { path.s, sep - path.s };
					for (schema = 0; (uint32_t)schema < ARRAY_LEN(schema_type_str); ++schema) {
						if (schema_type_str[schema]
							&& str_eql(&STRL(schema_type_str[schema]), &schema_str)) {
							break;
						}
					}

					if (schema == ARRAY_LEN(schema_type_str)) {
						goto missing_schema;
					}

					path.s = sep + 1;
					path.len -= (schema_str.len + 1);
				} else {
missing_schema:
					vm_error(wk,
						"missing or invalid schema in module path: %.*s",
						path.len, path.s);
					return false;
				}
			}

			{ // Interpolate path
				tstr_clear(&path_interpolated);

				uint32_t i;
				for (i = 0; i < path.len; ++i) {
					if (path.s[i] == '%') {
						tstr_pushs(wk, &path_interpolated, name);
					} else {
						tstr_push(wk, &path_interpolated, path.s[i]);
					}
				}
			}

			switch (schema) {
			case schema_type_file:
			case schema_type_embedded: {
				struct module_lookup_script_opts opts = {
					.encapsulate = encapsulate,
					.embedded = schema == schema_type_embedded,
				};

				if (encapsulate) {
					*res = make_obj(wk, obj_module);
					m = get_obj_module(wk, *res);

					if (obj_dict_index_strn(wk,
						    wk->vm.modules,
						    path_interpolated.buf,
						    path_interpolated.len,
						    res)) {
						return true;
					}
				}

				if (module_lookup_script(wk, &path_interpolated, m, &opts)) {
					if (encapsulate) {
						obj_dict_set(wk,
							wk->vm.modules,
							tstr_into_str(wk, &path_interpolated),
							*res);
					}

					if (schema == schema_type_file) {
						workspace_add_regenerate_dep(wk, tstr_into_str(wk, &path_interpolated));
					}

					if (wk->vm.error) {
						return false;
					}
					return true;
				}
				break;
			}
			case schema_type_builtin: {
				enum module mod_type;
				bool has_impl = false;
				if (module_lookup_builtin(path_interpolated.buf, &mod_type, &has_impl)) {
					if (!encapsulate) {
						vm_error(wk,
							"builtin modules cannot be imported into the current scope");
						return false;
					}

					*res = make_obj(wk, obj_module);
					m = get_obj_module(wk, *res);
					m->module = mod_type;
					m->found = has_impl;
					m->has_impl = has_impl;
					return true;
				}
				break;
			}
			}
		}
	}

	return false;
}

static bool
func_module_found(struct workspace *wk, obj self, obj *res)
{
	if (!pop_args(wk, NULL, NULL)) {
		return false;
	}

	*res = make_obj_bool(wk, get_obj_module(wk, self)->found);
	return true;
}

// clang-format off
struct func_impl_group module_func_impl_groups[module_count][language_mode_count] = {
	[module_fs]          = { { impl_tbl_module_fs },        { impl_tbl_module_fs_internal }        },
	[module_keyval]      = { { impl_tbl_module_keyval },    { 0 }                                  },
	[module_pkgconfig]   = { { impl_tbl_module_pkgconfig }, { 0 }                                  },
	[module_python3]     = { { impl_tbl_module_python3 },   { 0 }                                  },
	[module_python]      = { { impl_tbl_module_python },    { 0 }                                  },
	[module_sourceset]   = { { impl_tbl_module_sourceset }, { 0 }                                  },
	[module_toolchain]   = { { 0 },                         { impl_tbl_module_toolchain }          },
	[module_subprojects] = { { 0 },                         { impl_tbl_module_subprojects }        },
	[module_getopt]      = { { 0 },                         { impl_tbl_module_getopt }             },
	[module_curl]        = { { 0 },                         { impl_tbl_module_curl }             },
	[module_json]        = { { 0 },                         { impl_tbl_module_json }             },
};

const struct func_impl impl_tbl_module[] = {
	{ "found", func_module_found, tc_bool, },
	{ 0 },
};
// clang-format on

bool
module_func_lookup(struct workspace *wk, const char *name, enum module mod, uint32_t *idx)
{
	return func_lookup_for_group(module_func_impl_groups[mod], wk->vm.lang_mode, name, idx);
}
